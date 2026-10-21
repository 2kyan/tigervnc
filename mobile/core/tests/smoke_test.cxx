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
#include <cstdio>
#include <cstring>
#include <thread>

#include "tigervnc_core.h"

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
