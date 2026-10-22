/* Copyright 2026 mobile_tigervncviewer contributors
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

#ifndef __AUDIOCAPTUREPULSE_H__
#define __AUDIOCAPTUREPULSE_H__

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <pulse/pulseaudio.h>

#include <opus/opus.h>

#include <core/Timer.h>

/* Identical to unix/x0vncserver/AudioCapturePulse.h -- kept as a
 * separate copy here (not a shared/relative-path source reference)
 * because Xvnc's hw/vnc is built by a completely different, autotools
 * (Makefile.am) build system from x0vncserver's CMake, and cross-
 * directory source references are fragile under automake's object-file
 * naming without subdir-objects. If this ever needs a real behavior
 * change, check the other copy too.
 *
 * Captures this machine's system audio (the default sink's *monitor*
 * source -- i.e. what would come out of the speakers, not a
 * microphone) via PulseAudio's async API, Opus-encodes it (see
 * rfb/audioTypes.h -- audioOpusBitrate/audioOpusFrameSamples), and
 * feeds the encoded frames to the TigerVNC-native RFB audio extension.
 * Captures raw PCM at a fixed format (S16LE, stereo, 48000 Hz)
 * requested directly from PulseAudio -- no resampling code needed here;
 * PulseAudio resamples on its side if the sink's own rate differs, and
 * that's also exactly the format/rate Opus is configured for, so no
 * conversion happens between capture and encode either.
 *
 * Mirrors vncviewer/AudioOutputPulse.cxx's pa_threaded_mainloop
 * pattern (connect asynchronously, do everything else via callbacks on
 * the mainloop's own thread) but for recording instead of playback:
 * pa_stream_connect_record() against "<default sink>.monitor" instead
 * of pa_stream_connect_playback(), and pa_stream_peek()/pa_stream_drop()
 * in a read callback instead of pa_stream_write() in a write callback.
 *
 * Unlike playback, there is no local buffering/backpressure to manage
 * here -- each encoded frame is handed to onData() immediately; the
 * caller (x0vncserver.cxx) is responsible for getting it out to
 * connected clients promptly (see VNCServerST::sendAudioData()). There
 * *is* a small accumulation buffer (see pcmAccum below), but only to
 * re-align PulseAudio's own arbitrarily-sized delivery chunks to
 * Opus's fixed frame size -- not for backpressure.
 *
 * The monitor source delivers a continuous stream of samples whether
 * or not anything is actually playing (silence when idle). Since the
 * protocol's begin/data/end messages are meant to bracket periods
 * where something is actually playing (matching the QEMU audio
 * extension's own documented semantics -- "the server only sends
 * audio whilst something is playing"), this class does simple
 * amplitude-threshold silence detection to decide when to call
 * onBegin()/onEnd(), rather than treating the capture stream's own
 * lifetime as the signal.
 *
 * If the system's sound server isn't reachable yet at construction
 * (e.g. this server started before PulseAudio/PipeWire did, a real
 * scenario on a freshly-provisioned VM), this keeps retrying
 * automatically on a timer (see audioCaptureRetryMs) rather than
 * giving up forever. retryNow() is also exposed as an immediate,
 * on-demand retry -- wired to fire whenever a client (re-)enables
 * audio, so toggling mute off/on is a free manual "try again right
 * now" trigger instead of waiting for the next periodic attempt (see
 * VNCServerST::requestAudioRetry()/VNCSConnectionST::handleAudioControl()).
 * retryNow() is debounced (see manualRetryMinIntervalMs) so a client
 * that toggles mute rapidly can't force back-to-back blocking connect
 * attempts on the main server thread.
 */
class AudioCapturePulse : public core::Timer::Callback
{
public:
  // All three callbacks are invoked on the PulseAudio mainloop's own
  // thread -- same constraint AudioOutputPulse's callbacks operate
  // under: do the minimum necessary there (e.g. hand off to whatever
  // owns pushing data to VNC clients) rather than anything blocking or
  // PulseAudio-unsafe.
  using DataCallback = std::function<void(const uint8_t* data, size_t length)>;
  using BeginCallback = std::function<void()>;
  using EndCallback = std::function<void()>;

  AudioCapturePulse(DataCallback onData, BeginCallback onBegin,
                    EndCallback onEnd);
  ~AudioCapturePulse();

  bool isAvailable() const { return available; }

  // Requests an immediate reconnect attempt if not currently
  // available; a cheap no-op otherwise, including when called too
  // soon after a previous attempt (see manualRetryMinIntervalMs in the
  // .cxx). See class comment.
  void retryNow();

private:
  bool connect();
  // Tears down whatever connect() may have partially (or fully) set
  // up -- shared by the destructor and by connect() itself (called at
  // the top of connect(), so every retry attempt, not just the first,
  // starts from a clean slate instead of leaking the previous attempt's
  // mainloop/context).
  void teardown();
  // Wraps connect(), arming/disarming retryTimer around it. Called
  // from the constructor, from handleTimeout() (periodic retry), and
  // from retryNow() (manual retry) -- always from the main server
  // thread, never from the PulseAudio mainloop thread (see
  // handleTimeout()'s own comment for why that matters).
  void attemptConnect();
  void handleTimeout(core::Timer* t) override;

  // Called once server info tells us the default sink, and again
  // whenever the default sink changes
  void openStream();

  void handleData(const void* data, size_t length);
  // Consumes exactly-frame-sized chunks off the front of pcmAccum,
  // Opus-encoding and dispatching each via onData_/silence detection.
  void encodeAvailableFrames();

  static void contextStateCallback(pa_context* context, void* userdata);
  static void serverInfoCallback(pa_context* context,
                                 const pa_server_info* info, void* userdata);
  static void subscribeCallback(pa_context* context,
                                pa_subscription_event_type_t type,
                                uint32_t index, void* userdata);
  static void streamStateCallback(pa_stream* stream, void* userdata);
  static void streamReadCallback(pa_stream* stream, size_t length,
                                 void* userdata);
  static void timeoutCallback(pa_mainloop_api* api, pa_time_event* event,
                              const struct timeval* tv, void* userdata);

  DataCallback onData_;
  BeginCallback onBegin_;
  EndCallback onEnd_;

  bool available, opened, timedOut;

  pa_threaded_mainloop* mainloop;
  pa_context* context;
  pa_stream* stream;
  std::string monitorSourceName;

  // Silence-gated begin/end tracking (see class comment). Touched only
  // from the mainloop thread, inside streamReadCallback().
  bool streaming;
  unsigned silentFragments;

  // Opus encoder + frame-alignment buffer -- see encodeAvailableFrames().
  OpusEncoder* encoder;
  std::vector<uint8_t> pcmAccum;

  // Drives automatic reconnect attempts while unavailable -- see class
  // comment and attemptConnect().
  core::Timer retryTimer;

  // Debounces retryNow(): a client can trigger it on every mute
  // toggle (see VNCSConnectionST::handleAudioControl()), and each
  // attempt blocks the calling thread for up to connectTimeoutMs --
  // in Xvnc specifically, that is the whole X server. Sitting on this
  // timestamp caps how often a client can force a fresh blocking
  // attempt, regardless of how fast it toggles. See retryNow().
  std::chrono::steady_clock::time_point lastAttemptTime;
};

#endif
