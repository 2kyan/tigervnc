// End-to-end smoke test for tigervnc_core's C ABI: connects to
// test_server.c (a real RFB server) and exercises connect/disconnect,
// frame delivery + pixel format conversion, pointer/key/clipboard
// sending, and the error and cancellation-race paths that are easy to
// get wrong with the threaded design (see VncClientImpl.h). Not a
// substitute for testing against real-world VNC servers, but catches
// regressions in the wrapper itself. Build with -DTIGERVNC_CORE_BUILD_TESTS=ON
// (requires `brew install libvncserver`); run the resulting
// smoke_test_server and smoke_test binaries.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "tigervnc_core.h"
#include "audio_test_tone.h"

// Goertzel single-bin DFT magnitude at `freqHz`, over the left channel of
// interleaved S16LE stereo PCM. Used (not a sample-domain RMS diff) because
// Opus's encoder has an algorithmic look-ahead delay of a few ms -- for a
// pure tone whose period (440Hz @ 48kHz = ~109 samples) is on that same
// order, even a perceptually-flawless round trip lands samples out of
// phase with the un-delayed reference, which a plain memcmp/RMS diff
// mistakes for total corruption (empirically ~86% "error" on a correct
// round trip). A single-bin DFT magnitude is phase-blind by construction
// -- exactly the property needed here -- while still failing hard on a
// genuine bug (silence, garbage, wrong pitch, decoder never invoked).
static double goertzelMagnitude(const std::vector<uint8_t>& pcm, double freqHz,
                                 double sampleRateHz)
{
  size_t frames = pcm.size() / 4; // stereo, 16-bit -> 4 bytes/frame
  if (frames == 0)
    return 0.0;
  double k = std::round((double)frames * freqHz / sampleRateHz);
  double w = 2.0 * M_PI * k / (double)frames;
  double coeff = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = 0; i < frames; i++) {
    int16_t sample;
    std::memcpy(&sample, &pcm[i * 4], 2); // left channel
    double s0 = (double)sample + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return std::sqrt(power > 0.0 ? power : 0.0);
}

struct Ctx {
  std::atomic<bool> gotFrame{false};
  std::atomic<int> frameCount{0};
  std::atomic<bool> connected{false};
  std::atomic<bool> disconnected{false};
  std::atomic<bool> gotError{false};
  std::atomic<int> resizeResultCount{0};
  VncResizeResult lastResizeResult = VNC_RESIZE_OK;
  int frameW = 0, frameH = 0;
  uint8_t firstPixel[4] = {0, 0, 0, 0};

  std::atomic<int> audioBeginCount{0};
  std::atomic<int> audioEndCount{0};
  std::atomic<bool> audioComplete{false}; // true once end arrives
  std::vector<uint8_t> audioBytes;
};

static void onFrame(void* ctx, const uint8_t* pixels, int w, int h, int stride)
{
  auto* c = static_cast<Ctx*>(ctx);
  c->frameW = w;
  c->frameH = h;
  // Sample the center, not (0,0): we set supportsLocalCursor = false, so
  // the server bakes its (default top-left-positioned) cursor directly
  // into the pixel stream, permanently covering the corner regardless of
  // how many updates arrive.
  const uint8_t* p = pixels + (size_t)(h / 2) * stride + (size_t)(w / 2) * 4;
  std::memcpy(c->firstPixel, p, 4);
  c->frameCount++;
  c->gotFrame = true;
}

static void onState(void* ctx, VncState state, const char* message)
{
  auto* c = static_cast<Ctx*>(ctx);
  const char* name = "?";
  switch (state) {
    case VNC_STATE_CONNECTING: name = "CONNECTING"; break;
    case VNC_STATE_CONNECTED: name = "CONNECTED"; c->connected = true; break;
    case VNC_STATE_DISCONNECTED: name = "DISCONNECTED"; c->disconnected = true; break;
    case VNC_STATE_ERROR: name = "ERROR"; c->gotError = true; break;
  }
  std::printf("[state] %s%s%s\n", name, message ? ": " : "", message ? message : "");
}

static void onLog(void* ctx, VncLogLevel level, const char* message)
{
  (void)ctx; (void)level;
  std::printf("[log] %s\n", message);
}

static void onClipboard(void* ctx, const char* text)
{
  (void)ctx;
  std::printf("[clipboard] %s\n", text ? text : "(null)");
}

static void onResizeResult(void* ctx, VncResizeResult result)
{
  auto* c = static_cast<Ctx*>(ctx);
  c->lastResizeResult = result;
  c->resizeResultCount++;
  std::printf("[resize_result] %d\n", (int)result);
}

static void onAudioBegin(void* ctx)
{
  auto* c = static_cast<Ctx*>(ctx);
  c->audioBeginCount++;
  std::printf("[audio_begin]\n");
}

static void onAudioEnd(void* ctx)
{
  auto* c = static_cast<Ctx*>(ctx);
  c->audioEndCount++;
  c->audioComplete = true;
  std::printf("[audio_end] (%zu bytes accumulated)\n", c->audioBytes.size());
}

static void onAudioData(void* ctx, const uint8_t* pcm, size_t length)
{
  auto* c = static_cast<Ctx*>(ctx);
  // audio_data's pointer is only valid for this call -- copy now.
  c->audioBytes.insert(c->audioBytes.end(), pcm, pcm + length);
}

static bool waitFor(std::atomic<bool>& flag, int timeoutMs)
{
  auto start = std::chrono::steady_clock::now();
  while (!flag.load()) {
    if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  int failures = 0;

  // --- Test 1: real connection to the local test server, expect a frame.
  {
    Ctx ctx;
    VncClientCallbacks cb{};
    cb.context = &ctx;
    cb.frame_update = onFrame;
    cb.state_changed = onState;
    cb.log = onLog;
    cb.clipboard_text = onClipboard;
    cb.resize_result = onResizeResult;
    cb.audio_begin = onAudioBegin;
    cb.audio_end = onAudioEnd;
    cb.audio_data = onAudioData;

    VncClient* client = vnc_client_create(&cb);
    VncConnectOptions opts{"127.0.0.1", 15900, 1};
    vnc_client_connect(client, &opts);

    if (!waitFor(ctx.connected, 5000)) {
      std::printf("FAIL: never got CONNECTED\n");
      failures++;
    }
    if (!waitFor(ctx.gotFrame, 5000)) {
      std::printf("FAIL: never got a frame_update\n");
      failures++;
    } else {
      // The first update or two may be a partial/incremental repaint
      // (e.g. an empty resize-triggered update, or the cursor arriving
      // before the background fill catches up near it) -- normal VNC
      // behavior, not something a client should assume away. Force an
      // explicit non-incremental refresh and use *that* result, which
      // by definition can't be a partial diff.
      ctx.gotFrame = false;
      vnc_client_request_refresh(client);
      if (!waitFor(ctx.gotFrame, 3000)) {
        std::printf("FAIL: never got a frame_update after request_refresh\n");
        failures++;
      }
      // One more short settle in case this rendered as several rects.
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      std::printf("frame_update called %d time(s); frame: %dx%d first pixel R=%d G=%d B=%d\n",
                   ctx.frameCount.load(), ctx.frameW, ctx.frameH,
                   ctx.firstPixel[0], ctx.firstPixel[1], ctx.firstPixel[2]);
      if (ctx.frameW != 320 || ctx.frameH != 240) {
        std::printf("FAIL: unexpected frame size\n");
        failures++;
      }
      // Test server paints every pixel R=0x80,G=0x40,B=0x00 in its own
      // native format (see test_server.c); our wrapper always delivers
      // RGBA regardless of the server's internal format, so this should
      // read back unchanged: byte0=R=0x80, byte1=G=0x40, byte2=B=0x00.
      if (ctx.firstPixel[0] != 0x80 || ctx.firstPixel[1] != 0x40 ||
          ctx.firstPixel[2] != 0x00) {
        std::printf("FAIL: unexpected pixel data (expected 0x80,0x40,0x00, "
                     "got 0x%02x,0x%02x,0x%02x)\n",
                     ctx.firstPixel[0], ctx.firstPixel[1], ctx.firstPixel[2]);
        failures++;
      }
    }

    vnc_client_send_pointer_event(client, 10, 10, VNC_BUTTON_LEFT);
    vnc_client_send_pointer_event(client, 10, 10, 0);
    vnc_client_send_key_event(client, 0x61 /* 'a' */, 1);
    vnc_client_send_key_event(client, 0x61, 0);
    vnc_client_request_refresh(client);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Regression check for a real bug found during review: the worker
    // thread's internal call to "send this to the server" used to
    // resolve (via ordinary C++ name hiding) to VncClientImpl's own
    // same-named queuing wrapper instead of CConnection's real protocol
    // call, spinning forever instead of ever sending the request. If
    // that regresses, this call not returning promptly (or the
    // subsequent resize check below timing out because the event loop
    // is pegged) is the signal.
    vnc_client_request_clipboard(client);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Resize (remote-resize / SetDesktopSize): ask the server for a
    // different size than the one it started with, and confirm a
    // frame_update with the new dimensions actually arrives -- this
    // exercises the full round trip (write request -> server resizes ->
    // ExtendedDesktopSize + new pixel data comes back -> our
    // setExtendedDesktopSize/resizeFramebuffer/framebufferUpdateEnd).
    ctx.gotFrame = false;
    vnc_client_request_resize(client, 400, 300);
    if (!waitFor(ctx.gotFrame, 5000)) {
      std::printf("FAIL: never got a frame_update after request_resize\n");
      failures++;
    } else if (ctx.frameW != 400 || ctx.frameH != 300) {
      std::printf("FAIL: resize did not take effect (frame is %dx%d, expected 400x300)\n",
                   ctx.frameW, ctx.frameH);
      failures++;
    } else {
      std::printf("resize succeeded: frame is now %dx%d\n", ctx.frameW, ctx.frameH);
    }
    if (ctx.resizeResultCount.load() > 0)
      std::printf("resize_result fired %d time(s), last=%d\n",
                   ctx.resizeResultCount.load(), (int)ctx.lastResizeResult);

    // Audio: test_server.c injects a capability announcement (pseudo-
    // encoding -262) plus a full begin/data/end stream carrying a known
    // synthetic tone, Opus-encoded frame-by-frame just like the real
    // AudioCapturePulse encoder, a few seconds after connect (see its
    // main loop). Exercises CMsgReader::readAudioStreamMessage()
    // parsing, CConnection's auto-negotiation (supportsTigerAudio() ->
    // requestTigerAudio() -> writeAudioControl(true), already proven
    // not to break the connection against the mock -- see its
    // rfbProtocolExtension), VncClientImpl's real Opus decode
    // (handleAudioData()), and its audio_begin/audio_data/audio_end
    // forwarding -- entirely host-side with no audio hardware. The
    // comparison below is tolerance-based, not byte-exact: Opus is a
    // lossy codec, so a correct round trip differs slightly from the
    // source by design (see relativeRmsError()'s comment above main()).
    if (!waitFor(ctx.audioComplete, 5000)) {
      std::printf("FAIL: never got a complete audio_begin..audio_end sequence\n");
      failures++;
    } else {
      if (ctx.audioBeginCount.load() != 1 || ctx.audioEndCount.load() != 1) {
        std::printf("FAIL: expected exactly one audio_begin/audio_end pair, "
                     "got begin=%d end=%d\n",
                     ctx.audioBeginCount.load(), ctx.audioEndCount.load());
        failures++;
      }
      std::vector<uint8_t> expected(AUDIO_TEST_TONE_BYTES);
      audio_test_tone_fill(expected.data());
      const double kSampleRate = 48000.0, kToneHz = 440.0;
      double gotMag = goertzelMagnitude(ctx.audioBytes, kToneHz, kSampleRate);
      double expMag = goertzelMagnitude(expected, kToneHz, kSampleRate);
      double ratio = expMag > 0.0 ? gotMag / expMag : 0.0;
      // A genuine correct round trip lands very close to 1.0; [0.7, 1.3]
      // leaves headroom for Opus's lossy encoding and the Goertzel bin's
      // edge effects from the stream's short (300ms) length, while still
      // catching silence (ratio ~0), garbage (ratio far off or noisy), or
      // wrong pitch (energy landing outside the 440Hz bin entirely).
      const double kMinRatio = 0.7, kMaxRatio = 1.3;
      if (ctx.audioBytes.size() != expected.size()) {
        std::printf("FAIL: decoded audio_data size mismatch "
                     "(got %zu bytes, expected %zu)\n",
                     ctx.audioBytes.size(), expected.size());
        failures++;
      } else if (ratio < kMinRatio || ratio > kMaxRatio) {
        std::printf("FAIL: decoded 440Hz tone energy too far from reference "
                     "(got/expected magnitude ratio %.4f, want [%.2f, %.2f])\n",
                     ratio, kMinRatio, kMaxRatio);
        failures++;
      } else {
        std::printf("audio stream verified: %zu bytes, 440Hz magnitude ratio %.4f "
                     "(Opus round trip, want [%.2f, %.2f])\n",
                     ctx.audioBytes.size(), ratio, kMinRatio, kMaxRatio);
      }
    }

    // Mute path: disable, then re-enable -- test_server.c's
    // rfbProtocolExtension logs each control byte it receives (see its
    // stderr output) so a human/CI log inspection can confirm the wire
    // message actually arrived; there's no callback-visible effect on
    // this end since v1 has no ack message, only the request send path
    // is under test here.
    vnc_client_set_audio_enabled(client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    vnc_client_set_audio_enabled(client, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    vnc_client_disconnect(client);
    if (!waitFor(ctx.disconnected, 5000)) {
      std::printf("FAIL: never got DISCONNECTED after disconnect()\n");
      failures++;
    }
    vnc_client_destroy(client);
    std::printf("test 1 (real connection) done\n\n");
  }

  // --- Test 2: connect to a closed port, expect a clean ERROR, no hang.
  {
    Ctx ctx;
    VncClientCallbacks cb{};
    cb.context = &ctx;
    cb.state_changed = onState;

    VncClient* client = vnc_client_create(&cb);
    VncConnectOptions opts{"127.0.0.1", 1, 0}; // port 1: nothing listens
    vnc_client_connect(client, &opts);

    if (!waitFor(ctx.gotError, 5000)) {
      std::printf("FAIL: never got ERROR for refused connection\n");
      failures++;
    }
    vnc_client_destroy(client); // must not hang
    std::printf("test 2 (refused connection) done\n\n");
  }

  // --- Test 3: disconnect() called immediately, before any state change.
  {
    Ctx ctx;
    VncClientCallbacks cb{};
    cb.context = &ctx;
    cb.state_changed = onState;

    VncClient* client = vnc_client_create(&cb);
    VncConnectOptions opts{"127.0.0.1", 15900, 1};
    vnc_client_connect(client, &opts);
    vnc_client_disconnect(client); // race with connect() on purpose
    vnc_client_destroy(client);    // must not hang or crash
    std::printf("test 3 (immediate disconnect race) done\n\n");
  }

  if (failures == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d TEST(S) FAILED\n", failures);
  return 1;
}
