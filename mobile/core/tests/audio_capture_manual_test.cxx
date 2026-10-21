// Manual verification tool -- NOT part of the automated smoke test
// suite (see smoke_test.cxx for that, against the mock test_server.c).
// Connects to a REAL RFB server (e.g. build-server/unix/x0vncserver/
// x0vncserver from scripts/build-server.sh, TigerVNC's x0vncserver with
// the TigerVNC-native audio extension's real PulseAudio capture
// backend) and records whatever audio it receives to a .wav
// file, so a human can play it back and confirm real system audio
// round-tripped through the whole protocol end-to-end -- not just a
// byte-exact match against a synthetic test tone.
//
// Usage: audio_capture_manual_test <host> <port> <output.wav> <duration_seconds>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#include "tigervnc_core.h"

namespace {

struct Ctx {
  std::vector<uint8_t> pcm;
  std::atomic<int> beginCount{0};
  std::atomic<int> endCount{0};
};

void onState(void* /*ctx*/, VncState state, const char* message)
{
  const char* name = "?";
  switch (state) {
    case VNC_STATE_CONNECTING: name = "CONNECTING"; break;
    case VNC_STATE_CONNECTED: name = "CONNECTED"; break;
    case VNC_STATE_DISCONNECTED: name = "DISCONNECTED"; break;
    case VNC_STATE_ERROR: name = "ERROR"; break;
  }
  std::printf("[state] %s%s%s\n", name, message ? ": " : "", message ? message : "");
}

void onAudioBegin(void* ctx)
{
  static_cast<Ctx*>(ctx)->beginCount++;
  std::printf("[audio_begin]\n");
}

void onAudioEnd(void* ctx)
{
  static_cast<Ctx*>(ctx)->endCount++;
  std::printf("[audio_end]\n");
}

void onAudioData(void* ctx, const uint8_t* pcm, size_t length)
{
  auto* c = static_cast<Ctx*>(ctx);
  c->pcm.insert(c->pcm.end(), pcm, pcm + length);
}

void onLog(void* /*ctx*/, VncLogLevel /*level*/, const char* message)
{
  std::printf("[log] %s\n", message);
}

// Minimal 16-bit PCM WAV header -- just enough for any media player to
// open the captured audio directly.
void writeWavHeader(std::ofstream& f, uint32_t dataBytes)
{
  uint32_t sampleRate = 48000;
  uint16_t channels = 2;
  uint16_t bitsPerSample = 16;
  uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  uint16_t blockAlign = (uint16_t)(channels * bitsPerSample / 8);
  uint32_t chunkSize = 36 + dataBytes;
  uint32_t subchunk1Size = 16;
  uint16_t audioFormat = 1; // PCM

  f.write("RIFF", 4);
  f.write((const char*)&chunkSize, 4);
  f.write("WAVE", 4);
  f.write("fmt ", 4);
  f.write((const char*)&subchunk1Size, 4);
  f.write((const char*)&audioFormat, 2);
  f.write((const char*)&channels, 2);
  f.write((const char*)&sampleRate, 4);
  f.write((const char*)&byteRate, 4);
  f.write((const char*)&blockAlign, 2);
  f.write((const char*)&bitsPerSample, 2);
  f.write("data", 4);
  f.write((const char*)&dataBytes, 4);
}

} // namespace

int main(int argc, char** argv)
{
  if (argc != 5) {
    std::fprintf(stderr,
                 "usage: %s <host> <port> <output.wav> <duration_seconds>\n",
                 argv[0]);
    return 1;
  }

  const char* host = argv[1];
  int port = std::atoi(argv[2]);
  const char* outPath = argv[3];
  int durationSec = std::atoi(argv[4]);

  Ctx ctx;
  VncClientCallbacks cb{};
  cb.context = &ctx;
  cb.state_changed = onState;
  cb.audio_begin = onAudioBegin;
  cb.audio_end = onAudioEnd;
  cb.audio_data = onAudioData;
  cb.log = onLog;

  VncClient* client = vnc_client_create(&cb);
  VncConnectOptions opts{host, port, 1};
  vnc_client_connect(client, &opts);

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(durationSec))
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

  vnc_client_disconnect(client);
  vnc_client_destroy(client);

  std::printf("captured %zu bytes of PCM, audio_begin=%d audio_end=%d\n",
              ctx.pcm.size(), ctx.beginCount.load(), ctx.endCount.load());

  std::ofstream f(outPath, std::ios::binary);
  writeWavHeader(f, (uint32_t)ctx.pcm.size());
  f.write((const char*)ctx.pcm.data(), (std::streamsize)ctx.pcm.size());
  f.close();
  std::printf("wrote %s\n", outPath);

  return 0;
}
