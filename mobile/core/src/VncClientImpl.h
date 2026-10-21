#ifndef TIGERVNC_CORE_VNCCLIENTIMPL_H
#define TIGERVNC_CORE_VNCCLIENTIMPL_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opus/opus.h>

#include <rfb/CConnection.h>

#include "tigervnc_core.h"

namespace tigervnc_core {

// C++ implementation behind the VncClient opaque handle. One instance per
// connection/thread; not reused across reconnects (vnc_client_connect()
// is only valid once per instance -- see tigervnc_core.h).
class VncClientImpl : public rfb::CConnection {
public:
  explicit VncClientImpl(const VncClientCallbacks& callbacks);
  ~VncClientImpl() override;

  void connect(const VncConnectOptions& options);
  void disconnect();

  void provideCredentials(const char* username, const char* password);
  void verifyResponse(bool accept);

  void sendPointerEvent(int x, int y, uint32_t buttonMask);
  void sendKeyEvent(uint32_t keysym, bool down);
  void sendClipboardText(const char* text);
  void requestClipboard();
  void requestRefresh();
  void requestResize(int width, int height);
  void setAudioEnabled(bool enabled);

protected:
  // rfb::CConnection / rfb::CMsgHandler overrides.
  void getUserPasswd(bool secure, std::string* user,
                      std::string* password) override;
  bool verifyCertificate(unsigned int status, const uint8_t* certificate,
                         size_t length) override;
  bool verifyHostKey(const uint8_t* key, size_t length,
                     const char* fingerprint) override;
  void initDone() override;
  void resizeFramebuffer() override;
  void framebufferUpdateEnd() override;
  void setExtendedDesktopSize(unsigned reason, unsigned result, int w, int h,
                              const rfb::ScreenSet& layout) override;
  void handleClipboardData(const char* data) override;
  void handleClipboardAnnounce(bool available) override;
  void bell() override;
  void handleAudioBegin() override;
  void handleAudioEnd() override;
  void handleAudioData(const uint8_t* data, size_t length) override;

private:
  void runThread(std::string host, int port, bool shared);
  void reportState(VncState state, const char* message);
  void log(VncLogLevel level, const std::string& message);
  void resetFramebuffer(int width, int height);
  // Builds and sends the actual SetDesktopSize request; must run on the
  // worker thread (touches the CConnection/CMsgWriter directly, same as
  // every other outgoing message). Mirrors vncviewer's DesktopWindow::
  // remoteResize() windowed-mode (single-screen) case -- this app has no
  // multi-monitor/fullscreen concept to worry about.
  void sendResizeRequest(int width, int height);

  // Sends the actual audio enable/disable control message; must run on
  // the worker thread, same reasoning as sendResizeRequest() above.
  void sendAudioControl(bool enable);

  // Blocks the worker thread until either a decision is provided via
  // provideCredentials()/verifyResponse(), or disconnect() cancels it.
  // Returns false if cancelled.
  bool waitForDecision(std::unique_lock<std::mutex>& lock);

  VncClientCallbacks callbacks_;

  // Decodes audio_data's Opus-encoded payload back to the fixed PCM
  // format documented in tigervnc_core.h before invoking
  // callbacks_.audio_data() -- Opus is purely a wire-format detail; the
  // C ABI (and every platform consumer) only ever sees decoded PCM, same
  // contract as before Opus was introduced. Created lazily, on first
  // use, from handleAudioData(); persists for the connection's lifetime
  // rather than being recreated per audioBegin()/audioEnd() pair, since
  // those just mark periods of silence within one logical stream.
  OpusDecoder* opusDecoder_ = nullptr;

  std::thread thread_;
  std::atomic<bool> cancelled_{false};
  // Self-pipe so the worker's select() loop can be woken for outgoing
  // events or a disconnect request without polling.
  int wakeReadFd_ = -1;
  int wakeWriteFd_ = -1;

  // Guards every field below, all shared between the calling thread(s)
  // and the worker thread.
  std::mutex mutex_;
  std::condition_variable cv_;
  bool decisionPending_ = false;
  bool decisionReady_ = false;
  std::string pendingUsername_;
  std::string pendingPassword_;
  bool pendingAccept_ = false;

  struct OutgoingPointer { int x, y; uint32_t buttonMask; };
  struct OutgoingKey { uint32_t keysym; bool down; };
  bool wantDisconnect_ = false;
  bool wantRefresh_ = false;
  bool wantRequestClipboard_ = false;
  bool havePendingPointer_ = false;
  OutgoingPointer pendingPointer_{};
  std::vector<OutgoingKey> pendingKeys_;
  std::vector<std::string> pendingClipboard_;

  // Only the latest requested size matters, so a resize request
  // coalesces (like pendingPointer_) rather than queuing like
  // pendingKeys_/pendingClipboard_.
  bool wantResize_ = false;
  int resizeWidth_ = 0;
  int resizeHeight_ = 0;

  // Only the latest requested mute/unmute state matters, same
  // coalescing reasoning as wantResize_ above. Defaults to wanting
  // audio enabled, matching supportsAudio's default-on stance.
  bool wantAudioEnabledChange_ = false;
  bool audioEnabledTarget_ = true;
};

} // namespace tigervnc_core

#endif
