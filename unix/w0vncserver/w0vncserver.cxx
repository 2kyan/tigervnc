/* Copyright 2025 Adam Halim for Cendio AB
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <vector>

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include <network/TcpSocket.h>
#include <network/UnixSocket.h>
#include <rfb/VNCServerST.h>
#include <rfb/audioTypes.h>
#include <rfb/ServerCore.h>
#include <rdr/FdOutStream.h>
#include <core/Logger_stdio.h>
#include <core/LogWriter.h>
#include <core/Timer.h>
#include <core/i18n.h>

#include "parameters.h"
#include "GSocketSource.h"
#include "RFBTimerSource.h"
#include "portals/PortalDesktop.h"
#include "wayland/WaylandDesktop.h"
#ifdef HAVE_AUDIO_CAPTURE
#include "../x0vncserver/AudioCapturePulse.h"
#endif

static core::LogWriter vlog("main");

char* programName;
static GMainLoop* loop = nullptr;
static GDBusConnection* connection = nullptr;
static bool fatalError = false;

void fatal_error(const char *error, ...) {
  char exitError[1024];

  // Prioritise the first error we get as that is probably the most
  // relevant one.
  if (fatalError)
    return;

  fatalError = true;

  va_list ap;
  va_start(ap, error);
  vsnprintf(exitError, sizeof(exitError), error, ap);
  va_end(ap);

  vlog.error("%s", exitError);

  assert(loop);
  g_main_loop_quit(loop);
}

static void printVersion(FILE *fp)
{
  fprintf(fp, _("TigerVNC server version %s, built %s\n"),
          PACKAGE_VERSION, BUILD_TIMESTAMP);
}

static void usage()
{
  printVersion(stderr);
  fprintf(stderr,
          _("\n"
            "Usage: %s [<parameters>]\n"
            "       %s --version\n"),
          programName, programName);
  fprintf(stderr,
          _("\n"
            "Parameters can be turned on with -<param> or off with -<param>=0\n"
            "Parameters which take a value can be specified as "
            "-<param> <value>\n"
            "Other valid forms are <param>=<value> -<param>=<value> "
            "--<param>=<value>\n"
            "Parameter names are case-insensitive.  The parameters are:\n\n"));
  core::Configuration::listParams(79, 14);
  exit(1);
}

int CleanupSignalHandler(void*)
{
  g_main_loop_quit(loop);
  return 1;
}

#ifdef HAVE_AUDIO_CAPTURE
// Hands captured audio off from AudioCapturePulse's callbacks (which
// fire on PulseAudio's own mainloop thread) to this server's single
// main thread, which is the only place it is safe to touch
// VNCServerST/SMsgWriter -- nothing in this codebase is thread-safe by
// design (see VNCServerST.h). Identical in spirit to
// unix/x0vncserver/x0vncserver.cxx's own AudioBridge class (a
// core::Timer::Callback draining a mutex-guarded queue), just attached
// to this file's GLib main loop instead of a bare select() loop --
// RFBTimerSource (constructed in main() below) already bridges
// core::Timer::checkTimeouts() into every GLib main loop iteration for
// every registered core::Timer, so this class needs no GLib-specific
// code of its own.
class AudioBridge : public core::Timer::Callback {
public:
  explicit AudioBridge(rfb::VNCServerST* server_) : server(server_), timer(this)
  {
    timer.start(20);
  }

  // Called from AudioCapturePulse's callbacks -- keep this minimal and
  // non-blocking, same constraint PulseAudio's own callbacks are under.
  void onBegin()
  {
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back({Event::Begin, {}});
  }
  void onData(const uint8_t* data, size_t length)
  {
    std::lock_guard<std::mutex> lock(mutex);
    // Main thread fell behind? Drop the oldest frame rather than let
    // stale audio pile up (see rfb::audioMaxQueuedFrames).
    if ((unsigned)std::count_if(events.begin(), events.end(), isData) >=
        rfb::audioMaxQueuedFrames)
      events.erase(std::find_if(events.begin(), events.end(), isData));
    events.push_back({Event::Data, std::vector<uint8_t>(data, data + length)});
  }
  void onEnd()
  {
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back({Event::End, {}});
  }

private:
  struct Event {
    enum Kind { Begin, Data, End } kind;
    std::vector<uint8_t> data;
  };

  static bool isData(const Event& e) { return e.kind == Event::Data; }

  void handleTimeout(core::Timer* t) override
  {
    std::deque<Event> drained;
    {
      std::lock_guard<std::mutex> lock(mutex);
      drained.swap(events);
    }
    for (const Event& e : drained) {
      switch (e.kind) {
      case Event::Begin: server->sendAudioBegin(); break;
      case Event::Data:  server->sendAudioData(e.data.data(), e.data.size()); break;
      case Event::End:   server->sendAudioEnd(); break;
      }
    }
    t->repeat();
  }

  rfb::VNCServerST* server;
  core::Timer timer;
  std::mutex mutex;
  std::deque<Event> events;
};
#endif

int main(int argc, char** argv)
{
  std::list<network::SocketListener*> listeners;
  int32_t sigintTag;
  int32_t sigtermTag;
  int32_t sighupTag;
  RFBTimerSource* timerSource;
  GSocketSource* monitor;
  rfb::SDesktop* desktop;
  rfb::VNCServerST* server;
#ifdef HAVE_AUDIO_CAPTURE
  AudioBridge* audioBridge = nullptr;
  AudioCapturePulse* audioCapture = nullptr;
#endif

  setlocale(LC_ALL, "");

  programName = argv[0];

  core::initStdIOLoggers();
  core::LogWriter::setLogParams("*:stderr:30");

  core::Configuration::removeParam("AcceptSetDesktopSize");

  for (int i = 1; i < argc;) {
    int ret;

    ret = core::Configuration::handleParamArg(argc, argv, i);
    if (ret > 0) {
      i += ret;
      continue;
    }

    if (strcmp(argv[i], "-h") == 0 ||
        strcmp(argv[i], "-help") == 0 ||
        strcmp(argv[i], "--help") == 0) {
      usage();
    }

    if (strcmp(argv[i], "-v") == 0 ||
        strcmp(argv[i], "-version") == 0 ||
        strcmp(argv[i], "--version") == 0) {
      printVersion(stderr);
      return 0;
    }

    if (argv[i][0] == '-') {
      fprintf(stderr, _("%s: Unrecognized option '%s'\n"),
              programName, argv[i]);
      fprintf(stderr, _("See '%s --help' for more information.\n"),
              programName);
      exit(1);
    }

    fprintf(stderr, _("%s: Extra argument '%s'\n"), programName, argv[i]);
    fprintf(stderr, _("See '%s --help' for more information.\n"),
            programName);
    exit(1);
  }

  try {
    if (rfbunixpath.getValueStr()[0] != '\0') {
      listeners.push_back(new network::UnixListener(rfbunixpath, rfbunixmode));
      vlog.info(_("Listening for VNC connections on %s (mode %04o)"),
                (const char*)rfbunixpath, (int)rfbunixmode);
    }

    if ((int)rfbport != -1) {
      std::list<network::SocketListener*> tcp_listeners;
      const char *addr = interface;

      if (strcasecmp(addr, "all") == 0)
        addr = nullptr;
      if (localhostOnly)
        createLocalTcpListeners(&tcp_listeners, (int)rfbport);
      else
        createTcpListeners(&tcp_listeners, addr, (int)rfbport);

      if (!tcp_listeners.empty()) {
        listeners.splice (listeners.end(), tcp_listeners);
        if (localhostOnly)
          vlog.info(_("Listening for VNC connections on local "
                      "interfaces, port %d"), (int)rfbport);
        else
          vlog.info(_("Listening for VNC connections on interface %s, "
                      "port %d"),
                    (const char*)interface, (int)rfbport);
      }
    }
  } catch (std::exception& e) {
    fprintf(stderr, "%s\n", e.what());
    return -1;
  }

  if (listeners.empty()) {
    vlog.error(_("No path or port configured for incoming "
                  "connections"));
    return -1;
  }

  loop = g_main_loop_new(nullptr, false);
  sigintTag = g_unix_signal_add(SIGINT, CleanupSignalHandler, nullptr);
  sigtermTag = g_unix_signal_add(SIGTERM, CleanupSignalHandler, nullptr);
  sighupTag = g_unix_signal_add(SIGHUP, CleanupSignalHandler, nullptr);


  desktop = nullptr;
  try {
    if (PortalDesktop::available())
      desktop = new PortalDesktop();
  } catch (std::exception& e) {
    vlog.debug("PortalDesktop unavailable: %s", e.what());
  }

  try {
    if (!desktop && WaylandDesktop::available())
      desktop = new WaylandDesktop(loop);
  } catch (std::exception& e) {
    vlog.debug("WaylandDesktop unavailable: %s", e.what());
  }

  if (!desktop) {
    vlog.error(_("Desktop does not support remote connections"));
    return -1;
  }

  try {
    server = new rfb::VNCServerST(desktopName, desktop);
    timerSource = new RFBTimerSource();
    monitor = new GSocketSource(server, &listeners);

#ifdef HAVE_AUDIO_CAPTURE
    // Remote audio (TigerVNC-native extension -- see rfb/audioTypes.h).
    // Optional: if no PulseAudio-compatible sound server is reachable,
    // log it and continue video-only, same "degrade gracefully"
    // philosophy as the PortalDesktop/WaylandDesktop fallback chain
    // above. See AudioBridge's own comment for why no extra GLib
    // plumbing is needed here beyond constructing these two objects.
    audioBridge = new AudioBridge(server);
    audioCapture = new AudioCapturePulse(
      [audioBridge](const uint8_t* data, size_t length) {
        audioBridge->onData(data, length);
      },
      [audioBridge]() { audioBridge->onBegin(); },
      [audioBridge]() { audioBridge->onEnd(); });
    if (!audioCapture->isAvailable())
      vlog.info(_("No sound server found -- will keep retrying in the "
                  "background"));
    // Lets a client toggling audio on (VNCSConnectionST::
    // handleAudioControl()) trigger an immediate reconnect attempt if
    // the sound server wasn't available yet -- on top of
    // AudioCapturePulse's own periodic automatic retry. See
    // AudioCapturePulse::retryNow()'s own comment.
    server->setAudioRetryCallback([audioCapture]() { audioCapture->retryNow(); });
#endif
  } catch (std::exception& e) {
    vlog.error("main: %s", e.what());
    return 1;
  }

  monitor->attach(g_main_loop_get_context(loop));
  timerSource->attach(g_main_loop_get_context(loop));
  monitor->listen();
  g_main_loop_run(loop);

  g_source_remove(sigintTag);
  g_source_remove(sigtermTag);
  g_source_remove(sighupTag);

  delete monitor;
  delete timerSource;
  // audioCapture before audioBridge: its destructor synchronously tears
  // down the PulseAudio stream/mainloop, so this guarantees no more
  // AudioBridge callbacks can fire before audioBridge itself is freed.
#ifdef HAVE_AUDIO_CAPTURE
  delete audioCapture;
  delete audioBridge;
#endif
  delete server;
  delete desktop;

  for (network::SocketListener* listener : listeners)
    delete listener;

  // The GDBusConnection is a singleton that we want to close once
  // before we exit.
  connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  if (connection)
    g_dbus_connection_close_sync(connection, nullptr, nullptr);

  g_main_loop_unref(loop);
}
