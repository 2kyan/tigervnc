/* Minimal RFB test server (libvncserver) that smoke_test.cxx connects to,
 * for exercising tigervnc_core's C ABI against a real VNC session
 * end-to-end (real TCP connect, real RFB handshake, real Tight-encoded
 * frame data, real pixel format negotiation, and a real server-side
 * remote-resize). Test-only -- not shipped; built by
 * core/tests/CMakeLists.txt when TIGERVNC_CORE_BUILD_TESTS is on and
 * libvncserver is available (`brew install libvncserver`). */
#include <rfb/rfb.h>
#include <string.h>
#include <stdlib.h>

/* Paint every pixel a fixed, unambiguous color in the server's own
 * native format (see the serverFormat log line in main()): byte 0 =
 * red, byte 1 = green, byte 2 = blue. Red=0x80, Green=0x40, Blue=0x00. */
static void paintSolid(char* buffer, int width, int height)
{
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      unsigned char* p = (unsigned char*)buffer + (y * width + x) * 4;
      p[0] = 0x80;
      p[1] = 0x40;
      p[2] = 0x00;
      p[3] = 0;
    }
  }
}

/* Called when a connected client requests a different desktop size
 * (tigervnc_core's vnc_client_request_resize() / RFB SetDesktopSize).
 * Reallocates the framebuffer at the new size and reports success --
 * libvncserver takes care of notifying the client via
 * ExtendedDesktopSize once this returns. */
static int onSetDesktopSize(int width, int height, int numScreens,
                             struct rfbExtDesktopScreen* screens,
                             rfbClientPtr cl)
{
  (void)numScreens;
  (void)screens;
  char* buffer = (char*)malloc((size_t)width * height * 4);
  if (!buffer)
    return 2; /* no resources */
  paintSolid(buffer, width, height);
  char* oldBuffer = cl->screen->frameBuffer;
  rfbNewFramebuffer(cl->screen, buffer, width, height, 8, 3, 4);
  free(oldBuffer); /* rfbNewFramebuffer doesn't free the old one itself */
  return 0; /* success */
}

/* Logs every key event the client sends (RFB KeyEvent /
 * tigervnc_core's vnc_client_send_key_event()) so tests/manual
 * verification can confirm keysyms actually arrive, without needing the
 * framebuffer to visibly react to them. */
static void onKbdAddEvent(rfbBool down, rfbKeySym keySym, rfbClientPtr cl)
{
  (void)cl;
  fprintf(stderr, "kbdAddEvent: keysym=0x%04lx down=%d\n", (unsigned long)keySym, down);
}

int main(int argc, char** argv)
{
  int width = 320, height = 240;
  rfbScreenInfoPtr server = rfbGetScreen(&argc, argv, width, height, 8, 3, 4);
  fprintf(stderr, "serverFormat: redShift=%d greenShift=%d blueShift=%d "
                  "redMax=%d greenMax=%d blueMax=%d bigEndian=%d\n",
          server->serverFormat.redShift, server->serverFormat.greenShift,
          server->serverFormat.blueShift, server->serverFormat.redMax,
          server->serverFormat.greenMax, server->serverFormat.blueMax,
          server->serverFormat.bigEndian);
  server->frameBuffer = (char*)malloc((size_t)width * height * 4);
  paintSolid(server->frameBuffer, width, height);

  server->port = 15900;
  server->desktopName = "tigervnc_core smoke test";
  server->alwaysShared = TRUE;
  server->setDesktopSizeHook = onSetDesktopSize;
  server->kbdAddEvent = onKbdAddEvent;
  rfbInitServer(server);

  while (rfbIsActive(server))
    rfbProcessEvents(server, 100000);

  return 0;
}
