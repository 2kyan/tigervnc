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
#include <stdint.h>

#include <opus/opus.h>

#include "audio_test_tone.h"

/* TigerVNC-native audio extension wire constants (see
 * common/rfb/{encodings,msgTypes,audioTypes}.h) --
 * duplicated here rather than included, since this file is built
 * against libvncserver, a completely different server-side stack that
 * has no idea these exist. Hand-crafting raw bytes for them (bypassing
 * libvncserver's own message layer entirely) is the whole point: it
 * exercises the *client's* parsing/playback path in isolation, without
 * needing a real patched tigervncserver (out of scope -- see DESIGN.md). */
#define PSEUDO_ENCODING_TIGER_AUDIO (-262)
#define MSG_TYPE_AUDIO_STREAM 153
#define MSG_TYPE_AUDIO_CONTROL 154
#define AUDIO_OP_BEGIN 0
#define AUDIO_OP_DATA 1
#define AUDIO_OP_END 2

/* Opus wire-format constants (see common/rfb/audioTypes.h)
 * -- duplicated for the same reason as the constants above: this file
 * doesn't link against rfb's C++ namespace. Must match AudioCapturePulse's
 * real encoder settings exactly, since this mock exists to exercise the
 * client's real decode path (VncClientImpl::handleAudioData()), not a
 * simplified stand-in for it. */
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 2
#define AUDIO_OPUS_FRAME_SAMPLES 960 /* 20ms @ 48000 Hz */
#define AUDIO_OPUS_BITRATE 64000
#define AUDIO_OPUS_MAX_PACKET_BYTES 4000

static void putU16(uint8_t* p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xff);
}

static void putU32(uint8_t* p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)(v & 0xff);
}

/* Sends a single-rect FramebufferUpdate whose rect carries only the
 * pseudoEncodingTigerAudio capability announcement (empty x/y/w/h,
 * same trick pseudoEncodingQEMUAudio/pseudoEncodingLEDState use in the
 * real protocol) -- this is what makes a real patched client's
 * CMsgReader/CConnection::supportsTigerAudio() fire and, in turn,
 * trigger its own automatic reply enabling audio. */
static void sendAudioCapability(rfbClientPtr cl)
{
  uint8_t msg[16];
  msg[0] = 0;    /* msgTypeFramebufferUpdate */
  msg[1] = 0;    /* padding */
  putU16(msg + 2, 1); /* numberOfRects */
  putU16(msg + 4, 0);  /* x */
  putU16(msg + 6, 0);  /* y */
  putU16(msg + 8, 0);  /* w */
  putU16(msg + 10, 0); /* h */
  putU32(msg + 12, (uint32_t)PSEUDO_ENCODING_TIGER_AUDIO); /* encoding (S32) */
  rfbWriteExact(cl, (const char*)msg, sizeof(msg));
}

/* Sends a full begin/data/end audio stream containing the deterministic
 * test tone (audio_test_tone.h), Opus-encoded frame-by-frame exactly the
 * way the real AudioCapturePulse encoder does (see
 * unix/x0vncserver/AudioCapturePulse.cxx::encodeAvailableFrames()) --
 * one 20ms/960-sample frame per wire `data` op, not one giant blob. This
 * is what lets the smoke test exercise the client's *real* Opus decode
 * path (VncClientImpl::handleAudioData()) rather than a simplified
 * stand-in for it. */
static void sendAudioStream(rfbClientPtr cl)
{
  uint8_t begin[2] = {MSG_TYPE_AUDIO_STREAM, AUDIO_OP_BEGIN};
  uint8_t end[2] = {MSG_TYPE_AUDIO_STREAM, AUDIO_OP_END};
  uint8_t header[6];
  uint8_t opusBuf[AUDIO_OPUS_MAX_PACKET_BYTES];
  uint8_t* tone = (uint8_t*)malloc(AUDIO_TEST_TONE_BYTES);
  int frameBytes = AUDIO_OPUS_FRAME_SAMPLES * AUDIO_CHANNELS * 2;
  int offset;
  int error = OPUS_OK;
  OpusEncoder* encoder;

  if (!tone)
    return;
  audio_test_tone_fill(tone);

  encoder = opus_encoder_create(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS,
                                 OPUS_APPLICATION_AUDIO, &error);
  if (error != OPUS_OK || !encoder) {
    fprintf(stderr, "sendAudioStream: opus_encoder_create failed: %s\n",
            opus_strerror(error));
    free(tone);
    return;
  }
  opus_encoder_ctl(encoder, OPUS_SET_BITRATE(AUDIO_OPUS_BITRATE));

  rfbWriteExact(cl, (const char*)begin, sizeof(begin));

  for (offset = 0; offset + frameBytes <= AUDIO_TEST_TONE_BYTES;
       offset += frameBytes) {
    opus_int32 encoded = opus_encode(
        encoder, (const opus_int16*)(tone + offset), AUDIO_OPUS_FRAME_SAMPLES,
        opusBuf, AUDIO_OPUS_MAX_PACKET_BYTES);
    if (encoded < 0) {
      fprintf(stderr, "sendAudioStream: opus_encode failed: %s\n",
              opus_strerror(encoded));
      continue;
    }
    header[0] = MSG_TYPE_AUDIO_STREAM;
    header[1] = AUDIO_OP_DATA;
    putU32(header + 2, (uint32_t)encoded);
    rfbWriteExact(cl, (const char*)header, sizeof(header));
    rfbWriteExact(cl, (const char*)opusBuf, (size_t)encoded);
  }

  rfbWriteExact(cl, (const char*)end, sizeof(end));

  opus_encoder_destroy(encoder);
  free(tone);
}

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

/* State for the audio-extension test injection below: captured on
 * connect, reset per-client so repeated smoke_test invocations against
 * one long-running server process each get exactly one audio sequence. */
static rfbClientPtr audioClient = NULL;
static int audioIterationsWaited = 0;
static int audioSent = 0;

static void onClientGone(rfbClientPtr cl)
{
  if (cl == audioClient)
    audioClient = NULL;
}

/* A real, patched client automatically replies to the capability
 * announcement above by sending msgTypeAudioControl (154) back --
 * that's the whole point of the extension (see requestTigerAudio() in
 * common/rfb/CConnection.cxx). libvncserver's own
 * message dispatch has no idea what type 154 is and, empirically,
 * closes the connection on any unrecognised client message type rather
 * than ignoring it -- unlike pseudo-encodings, which libvncserver (and
 * the RFB spec) do tolerate being unrecognised. Registering a minimal
 * rfbProtocolExtension is libvncserver's supported way to teach it
 * about one more client-to-server message type without that happening;
 * it also lets this test observe the control byte the client actually
 * sent, for the mute-path assertion in smoke_test.cxx. */
static int lastAudioControlOp = -1;

static rfbBool onAudioExtNewClient(rfbClientPtr cl, void** data)
{
  (void)cl;
  (void)data;
  return TRUE; /* keep this extension active for every client */
}

static rfbBool onAudioExtMessage(rfbClientPtr cl, void* data,
                                  const rfbClientToServerMsg* message)
{
  uint8_t op;
  (void)data;

  if (message->type != MSG_TYPE_AUDIO_CONTROL)
    return FALSE;

  if (rfbReadExact(cl, (char*)&op, 1) <= 0)
    return TRUE; /* connection is going away; nothing more to do */
  lastAudioControlOp = op;
  fprintf(stderr, "audioControl: op=%d (%s)\n", op,
          op ? "enable" : "disable");
  return TRUE;
}

static rfbProtocolExtension audioExtension = {
  onAudioExtNewClient, /* newClient */
  NULL,                /* init */
  NULL,                /* pseudoEncodings -- we send our capability rect
                        * ourselves; nothing for libvncserver to track */
  NULL,                /* enablePseudoEncoding */
  onAudioExtMessage,   /* handleMessage */
  NULL,                /* close */
  NULL,                /* usage */
  NULL,                /* processArgument */
  NULL                 /* next -- set by rfbRegisterProtocolExtension */
};

static enum rfbNewClientAction onNewClient(rfbClientPtr cl)
{
  audioClient = cl;
  audioIterationsWaited = 0;
  audioSent = 0;
  cl->clientGoneHook = onClientGone; /* per-client, not server-level */
  return RFB_CLIENT_ACCEPT;
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
  server->newClientHook = onNewClient;
  rfbRegisterProtocolExtension(&audioExtension);
  rfbInitServer(server);

  while (rfbIsActive(server)) {
    rfbProcessEvents(server, 100000);

    /* Once a client has been connected for a few iterations (giving it
     * time to finish its own SetEncodings/first FramebufferUpdateRequest
     * round trip), inject the audio-extension capability announcement
     * followed by a full begin/data/end stream -- see sendAudioCapability()/
     * sendAudioStream() above for why this bypasses libvncserver's own
     * message layer entirely. Fires at most once per connected client. */
    if (audioClient && !audioSent) {
      audioIterationsWaited++;
      if (audioIterationsWaited >= 10) {
        sendAudioCapability(audioClient);
        sendAudioStream(audioClient);
        audioSent = 1;
      }
    }
  }

  return 0;
}
