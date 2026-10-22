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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <cstring>

#include <core/LogWriter.h>

#include <rfb/audioTypes.h>

#include "AudioCapturePulse.h"

static core::LogWriter vlog("AudioCapturePulse");

// One Opus-encoded frame's worth of raw PCM, in bytes -- the unit
// encodeAvailableFrames() consumes from pcmAccum. Same value
// AudioCapturePulse's PulseAudio record buffer already targets
// per-callback (see openStream()'s attr.fragsize), so this is
// ordinarily a no-op realignment, not a real accumulation delay --
// pcmAccum only matters because PulseAudio's actual delivery chunks
// aren't *guaranteed* to match that target exactly every time.
static const size_t opusFrameBytes =
  (size_t)rfb::audioOpusFrameSamples * rfb::audioChannels *
  (rfb::audioBitsPerSample / 8);

// libopus's own examples/docs use 4000 bytes as a safe max output
// buffer size for any reasonable bitrate/frame-size combination --
// our actual encoded frames average far smaller (~160 bytes at
// audioOpusBitrate for a 20ms frame), VBR spikes included.
static const opus_int32 opusMaxPacketBytes = 4000;

// Same reasoning as AudioOutputPulse::connectTimeoutMs -- do not let a
// sound server that never answers hold up startup.
static const unsigned connectTimeoutMs = 500;

// How often to retry connecting while the sound server isn't
// reachable (e.g. this process started before PulseAudio/PipeWire did
// -- a real scenario on a freshly-provisioned VM). Frequent enough
// that audio starts working within a few seconds of the sound server
// actually coming up, infrequent enough not to spam connection
// attempts indefinitely on a machine that genuinely has none. Each
// attempt still costs up to connectTimeoutMs blocking the main server
// thread (same as the very first attempt at startup already does), so
// this isn't free -- a deliberate, bounded tradeoff, not zero-cost
// polling.
static const unsigned audioCaptureRetryMs = 5000;

// Floor on how often retryNow() will actually perform a fresh blocking
// connect attempt, regardless of how often it's called -- a client can
// trigger it on every mute toggle (see VNCSConnectionST::
// handleAudioControl()), and without this a client rapidly toggling
// mute could force back-to-back connectTimeoutMs-long stalls of the
// main server thread. One second comfortably exceeds connectTimeoutMs
// itself while still feeling instant for the real use case (a single
// toggle after the sound server actually became available).
static const unsigned manualRetryMinIntervalMs = 1000;

// How many consecutive near-silent fragments to see before deciding
// the audio has actually stopped (and emitting onEnd()), rather than
// treating every brief pause as a full begin/end cycle. At ~20ms
// fragments (see fillSampleSpec()/the record buffer attr below), 50
// fragments is roughly one second.
static const unsigned silenceFragmentsToEnd = 50;
// Amplitude threshold below which a sample counts as silence -- a
// small margin above true digital zero to tolerate DAC/mixer noise
// floor, well below anything a human would call "audible".
static const int16_t silenceThreshold = 64;

static void fillSampleSpec(pa_sample_spec* spec)
{
  spec->format = PA_SAMPLE_S16NE;
  spec->rate = rfb::audioSampleRate;
  spec->channels = rfb::audioChannels;
}

static bool isSilent(const void* data, size_t length)
{
  const int16_t* samples = (const int16_t*)data;
  size_t count = length / sizeof(int16_t);

  for (size_t i = 0; i < count; i++) {
    if (samples[i] > silenceThreshold || samples[i] < -silenceThreshold)
      return false;
  }
  return true;
}

AudioCapturePulse::AudioCapturePulse(DataCallback onData,
                                     BeginCallback onBegin,
                                     EndCallback onEnd)
  : onData_(onData), onBegin_(onBegin), onEnd_(onEnd),
    available(false), opened(false), timedOut(false),
    mainloop(nullptr), context(nullptr), stream(nullptr),
    streaming(false), silentFragments(0), encoder(nullptr),
    retryTimer(this)
{
  int error = OPUS_OK;
  encoder = opus_encoder_create(rfb::audioSampleRate, rfb::audioChannels,
                                OPUS_APPLICATION_AUDIO, &error);
  if (error != OPUS_OK || encoder == nullptr) {
    vlog.error("Could not create Opus encoder: %s", opus_strerror(error));
    encoder = nullptr;
    return; // not a connectivity problem -- retrying won't help
  }
  opus_encoder_ctl(encoder, OPUS_SET_BITRATE(rfb::audioOpusBitrate));

  attemptConnect();
  if (!available) {
    vlog.info("No sound server found yet -- will retry every %ums",
              audioCaptureRetryMs);
    retryTimer.start(audioCaptureRetryMs);
  }
}

AudioCapturePulse::~AudioCapturePulse()
{
  teardown();

  if (encoder != nullptr)
    opus_encoder_destroy(encoder);
}

void AudioCapturePulse::teardown()
{
  // Same ordering/reasoning as AudioOutputPulse::~AudioOutputPulse():
  // nothing may be running on the mainloop's thread while we take the
  // stream apart. Called both from the destructor and from the start
  // of connect() itself, so every retry attempt -- not just final
  // teardown -- starts from a clean slate instead of leaking the
  // previous attempt's mainloop/context/stream.
  if (mainloop != nullptr)
    pa_threaded_mainloop_stop(mainloop);

  if (stream != nullptr) {
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream = nullptr;
  }

  if (context != nullptr) {
    pa_context_disconnect(context);
    pa_context_unref(context);
    context = nullptr;
  }

  if (mainloop != nullptr) {
    pa_threaded_mainloop_free(mainloop);
    mainloop = nullptr;
  }

  // Reset per-attempt state too -- otherwise a stale timedOut=true
  // from a previous failed attempt would make the very next attempt's
  // wait loop (in connect()) bail out instantly instead of actually
  // waiting up to connectTimeoutMs.
  opened = false;
  timedOut = false;
}

void AudioCapturePulse::attemptConnect()
{
  lastAttemptTime = std::chrono::steady_clock::now();

  available = connect();
  if (available) {
    retryTimer.stop();
    // The failure path already logs ("No sound server found yet...",
    // "Could not connect..."); this is its silent counterpart's fix --
    // without it, a successful (re)connect produces no log output at
    // all, making it impossible to tell from the log alone whether a
    // retry (automatic or client-triggered) ever actually succeeded.
    vlog.info("Sound server connection established");
  }
}

void AudioCapturePulse::handleTimeout(core::Timer* t)
{
  // Only ever reached via core::Timer::checkTimeouts(), driven by each
  // server's own main-thread event loop (x0vncserver's select() loop,
  // w0vncserver's GLib loop via RFBTimerSource, XserverDesktop's
  // blockHandler()) -- never from the PulseAudio mainloop thread,
  // which is what makes it safe to touch available/retryTimer here
  // with no extra locking, same as every other non-static method on
  // this class.
  attemptConnect();
  if (!available)
    t->repeat();
}

void AudioCapturePulse::retryNow()
{
  if (available)
    return;

  auto sinceLast = std::chrono::steady_clock::now() - lastAttemptTime;
  if (sinceLast <
      std::chrono::milliseconds(manualRetryMinIntervalMs)) {
    // Too soon after some previous attempt (automatic or manual) --
    // let the already-running retryTimer handle it instead of
    // blocking the caller again right away. See manualRetryMinIntervalMs.
    return;
  }

  attemptConnect();
  if (!available)
    retryTimer.start(audioCaptureRetryMs);
}

bool AudioCapturePulse::connect()
{
  pa_mainloop_api* api;
  pa_proplist* proplist;
  pa_time_event* timeout;
  bool ready;

  teardown();

  mainloop = pa_threaded_mainloop_new();
  if (mainloop == nullptr)
    return false;

  if (pa_threaded_mainloop_start(mainloop) < 0) {
    pa_threaded_mainloop_free(mainloop);
    mainloop = nullptr;
    return false;
  }

  pa_threaded_mainloop_lock(mainloop);

  api = pa_threaded_mainloop_get_api(mainloop);

  proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "TigerVNC");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "x0vncserver");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "tigervnc");

  context = pa_context_new_with_proplist(api, "TigerVNC", proplist);

  pa_proplist_free(proplist);

  if (context == nullptr) {
    pa_threaded_mainloop_unlock(mainloop);
    return false;
  }

  pa_context_set_state_callback(context, contextStateCallback, this);

  if (pa_context_connect(context, nullptr,
                         PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
    vlog.error("Could not connect to sound server: %s",
               pa_strerror(pa_context_errno(context)));
    pa_threaded_mainloop_unlock(mainloop);
    return false;
  }

  timeout = pa_context_rttime_new(context,
                                  pa_rtclock_now() +
                                    connectTimeoutMs * PA_USEC_PER_MSEC,
                                  timeoutCallback, this);

  while (true) {
    pa_context_state_t state;

    state = pa_context_get_state(context);
    if (state == PA_CONTEXT_READY) {
      ready = true;
      break;
    }
    if (!PA_CONTEXT_IS_GOOD(state)) {
      vlog.error("Could not connect to sound server: %s",
                 pa_strerror(pa_context_errno(context)));
      ready = false;
      break;
    }
    if (timedOut) {
      vlog.error("Timed out connecting to sound server");
      ready = false;
      break;
    }

    pa_threaded_mainloop_wait(mainloop);
  }

  if (timeout != nullptr)
    api->time_free(timeout);

  if (ready) {
    // Need the default sink's name before we can open a record stream
    // on its monitor source -- see serverInfoCallback()/openStream().
    pa_operation* op = pa_context_get_server_info(context,
                                                  serverInfoCallback, this);
    if (op != nullptr)
      pa_operation_unref(op);

    // Server change events cover the default sink changing (e.g.
    // scripts/setup-audio.sh switching to a virtual sink while we're
    // running) -- see subscribeCallback().
    pa_context_set_subscribe_callback(context, subscribeCallback, this);
    op = pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SERVER,
                              nullptr, nullptr);
    if (op != nullptr)
      pa_operation_unref(op);
  }

  pa_threaded_mainloop_unlock(mainloop);

  return ready;
}

void AudioCapturePulse::openStream()
{
  pa_sample_spec spec;
  pa_buffer_attr attr;

  fillSampleSpec(&spec);

  stream = pa_stream_new(context, "Remote audio capture", &spec, nullptr);
  if (stream == nullptr) {
    vlog.error("Could not create audio capture stream: %s",
               pa_strerror(pa_context_errno(context)));
    return;
  }

  pa_stream_set_state_callback(stream, streamStateCallback, this);
  pa_stream_set_read_callback(stream, streamReadCallback, this);

  // ~20ms fragments: low enough latency to feel live, large enough not
  // to flood the wire (and every connected client) with per-message
  // framing overhead. maxlength/-1 lets the server pick a sane default
  // for how much it buffers on its side if we fall behind reading.
  memset(&attr, 0, sizeof(attr));
  attr.maxlength = (uint32_t)-1;
  attr.fragsize = (uint32_t)(rfb::audioSampleRate * rfb::audioChannels *
                             (rfb::audioBitsPerSample / 8) / 50);

  // "<default sink>.monitor" is the standard PulseAudio device name for
  // "capture whatever that sink is currently outputting" -- this is
  // what makes it *system audio* capture rather than microphone input.
  if (pa_stream_connect_record(stream, monitorSourceName.c_str(), &attr,
                               PA_STREAM_NOFLAGS) < 0) {
    vlog.error("Could not open audio capture device: %s",
               pa_strerror(pa_context_errno(context)));
    pa_stream_unref(stream);
    stream = nullptr;
    return;
  }

  opened = true;
}

void AudioCapturePulse::handleData(const void* data, size_t length)
{
  const uint8_t* bytes = (const uint8_t*)data;
  pcmAccum.insert(pcmAccum.end(), bytes, bytes + length);
  encodeAvailableFrames();
}

void AudioCapturePulse::encodeAvailableFrames()
{
  uint8_t opusBuf[opusMaxPacketBytes];

  while (pcmAccum.size() >= opusFrameBytes) {
    const uint8_t* frame = pcmAccum.data();
    bool silent = isSilent(frame, opusFrameBytes);

    if (!silent) {
      silentFragments = 0;
      if (!streaming) {
        streaming = true;
        if (onBegin_)
          onBegin_();
      }
    } else if (streaming) {
      silentFragments++;
      if (silentFragments >= silenceFragmentsToEnd) {
        // This frame is the one that crossed the threshold -- matches
        // it not being forwarded below (streaming is now false),
        // consistent with it being silence anyway.
        streaming = false;
        if (onEnd_)
          onEnd_();
      }
      // else: still within the "it might just be a brief pause" grace
      // period -- streaming stays true, so this silent frame still
      // gets encoded/forwarded below, avoiding a choppy gap on the
      // client end for a short pause.
    }
    // else: already idle, more silence -- streaming stays false.

    if (streaming && onData_) {
      opus_int32 encoded = opus_encode(encoder, (const opus_int16*)frame,
                                       rfb::audioOpusFrameSamples,
                                       opusBuf, opusMaxPacketBytes);
      if (encoded < 0)
        vlog.error("Opus encode failed: %s", opus_strerror(encoded));
      else if (encoded > 0)
        onData_(opusBuf, (size_t)encoded);
    }

    pcmAccum.erase(pcmAccum.begin(), pcmAccum.begin() + opusFrameBytes);
  }
}

void AudioCapturePulse::contextStateCallback(pa_context* context,
                                             void* userdata)
{
  AudioCapturePulse* self = (AudioCapturePulse*)userdata;

  if (self->available &&
      (pa_context_get_state(context) == PA_CONTEXT_FAILED))
    vlog.error("Lost connection to sound server");

  pa_threaded_mainloop_signal(self->mainloop, 0);
}

void AudioCapturePulse::serverInfoCallback(pa_context* /*context*/,
                                           const pa_server_info* info,
                                           void* userdata)
{
  AudioCapturePulse* self = (AudioCapturePulse*)userdata;

  if (info == nullptr || info->default_sink_name == nullptr) {
    vlog.error("Could not determine default sink for audio capture");
    return;
  }

  std::string name = std::string(info->default_sink_name) + ".monitor";

  // Also reached on every server change event, most of which don't
  // touch the default sink.
  if (self->stream != nullptr && name == self->monitorSourceName)
    return;

  if (self->stream != nullptr) {
    vlog.info("Default sink changed, now capturing from %s", name.c_str());
    pa_stream_disconnect(self->stream);
    pa_stream_unref(self->stream);
    self->stream = nullptr;
  }

  self->monitorSourceName = name;
  self->openStream();
}

void AudioCapturePulse::subscribeCallback(pa_context* context,
                                          pa_subscription_event_type_t type,
                                          uint32_t /*index*/,
                                          void* userdata)
{
  if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) !=
      PA_SUBSCRIPTION_EVENT_SERVER)
    return;

  // Re-read the default sink; serverInfoCallback() reopens the capture
  // stream if it changed.
  pa_operation* op = pa_context_get_server_info(context, serverInfoCallback,
                                                userdata);
  if (op != nullptr)
    pa_operation_unref(op);
}

void AudioCapturePulse::streamStateCallback(pa_stream* stream,
                                            void* /*userdata*/)
{
  if (pa_stream_get_state(stream) == PA_STREAM_FAILED)
    vlog.error("Audio capture stream failed");
}

void AudioCapturePulse::streamReadCallback(pa_stream* stream,
                                           size_t /*length*/,
                                           void* userdata)
{
  AudioCapturePulse* self = (AudioCapturePulse*)userdata;
  const void* data;
  size_t nbytes;

  if (pa_stream_peek(stream, &data, &nbytes) < 0) {
    vlog.error("Could not read from audio capture stream: %s",
               pa_strerror(pa_context_errno(self->context)));
    return;
  }

  if (nbytes == 0)
    return;

  if (data == nullptr) {
    // A hole in the stream (e.g. we fell behind) -- nothing to hand
    // off, just acknowledge and move on.
    pa_stream_drop(stream);
    return;
  }

  self->handleData(data, nbytes);

  pa_stream_drop(stream);
}

void AudioCapturePulse::timeoutCallback(pa_mainloop_api* /*api*/,
                                        pa_time_event* /*event*/,
                                        const struct timeval* /*tv*/,
                                        void* userdata)
{
  AudioCapturePulse* self = (AudioCapturePulse*)userdata;

  self->timedOut = true;
  pa_threaded_mainloop_signal(self->mainloop, 0);
}
