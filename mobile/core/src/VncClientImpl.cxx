#include "VncClientImpl.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <nettle/sha2.h>
#include <nettle/version.h>

#include <core/Exception.h>
#include <core/Rect.h>
#include <network/TcpSocket.h>
#include <rdr/FdInStream.h>
#include <rdr/FdOutStream.h>
#include <rfb/CMsgWriter.h>
#include <rfb/Exception.h>
#include <rfb/PixelBuffer.h>
#include <rfb/ScreenSet.h>
#include <rfb/screenTypes.h>
#include <rdr/InStream.h>

namespace tigervnc_core {

namespace {

// Fixed in-memory pixel format all frame_update callbacks are delivered
// in: 32bpp, byte order R,G,B,<unused> -- see tigervnc_core.h.
rfb::PixelFormat framebufferFormat()
{
  return rfb::PixelFormat(32, 24, false, true, 255, 255, 255, 0, 8, 16);
}

std::string hexDigest(const uint8_t* data, size_t len)
{
  struct sha256_ctx ctx;
  uint8_t digest[SHA256_DIGEST_SIZE];
  sha256_init(&ctx);
  sha256_update(&ctx, len, data);
#if NETTLE_VERSION_MAJOR >= 4
  sha256_digest(&ctx, digest);
#else
  sha256_digest(&ctx, sizeof(digest), digest);
#endif

  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(sizeof(digest) * 2);
  for (uint8_t b : digest) {
    out.push_back(hex[b >> 4]);
    out.push_back(hex[b & 0xf]);
  }
  return out;
}

} // namespace

VncClientImpl::VncClientImpl(const VncClientCallbacks& callbacks)
  : callbacks_(callbacks)
{
  supportsLocalCursor = false;
  supportsCursorPosition = false;
  supportsDesktopResize = true;
  supportsLEDState = false;
  supportsAudio = false;

  // Created here, synchronously, before any worker thread can possibly
  // exist -- disconnect() (called from any thread, e.g. immediately
  // after connect() spawns the worker) must always be able to reach the
  // pipe. Creating it lazily inside the worker thread instead is racy:
  // disconnect() could run, see wakeWriteFd_ == -1, and silently drop
  // the wake byte before the worker gets around to creating the pipe.
  int pipeFds[2] = {-1, -1};
  if (pipe(pipeFds) == 0) {
    wakeReadFd_ = pipeFds[0];
    wakeWriteFd_ = pipeFds[1];
    fcntl(wakeReadFd_, F_SETFL, O_NONBLOCK);
    fcntl(wakeWriteFd_, F_SETFL, O_NONBLOCK);
  }
}

VncClientImpl::~VncClientImpl()
{
  disconnect();
  if (thread_.joinable())
    thread_.join();
  if (wakeReadFd_ >= 0) ::close(wakeReadFd_);
  if (wakeWriteFd_ >= 0) ::close(wakeWriteFd_);
}

void VncClientImpl::connect(const VncConnectOptions& options)
{
  thread_ = std::thread(&VncClientImpl::runThread, this,
                         std::string(options.host ? options.host : ""),
                         options.port, options.shared != 0);
}

void VncClientImpl::disconnect()
{
  cancelled_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wantDisconnect_ = true;
  }
  cv_.notify_all();
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::provideCredentials(const char* username,
                                        const char* password)
{
  std::lock_guard<std::mutex> lock(mutex_);
  pendingUsername_ = username ? username : "";
  pendingPassword_ = password ? password : "";
  decisionReady_ = true;
  cv_.notify_all();
}

void VncClientImpl::verifyResponse(bool accept)
{
  std::lock_guard<std::mutex> lock(mutex_);
  pendingAccept_ = accept;
  decisionReady_ = true;
  cv_.notify_all();
}

void VncClientImpl::sendPointerEvent(int x, int y, uint32_t buttonMask)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    havePendingPointer_ = true;
    pendingPointer_ = {x, y, buttonMask};
  }
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::sendKeyEvent(uint32_t keysym, bool down)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingKeys_.push_back({keysym, down});
  }
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::sendClipboardText(const char* text)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingClipboard_.push_back(text ? text : "");
  }
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::requestClipboard()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wantRequestClipboard_ = true;
  }
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::requestRefresh()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wantRefresh_ = true;
  }
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::requestResize(int width, int height)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wantResize_ = true;
    resizeWidth_ = width;
    resizeHeight_ = height;
  }
  if (wakeWriteFd_ >= 0) {
    char b = 0;
    (void)write(wakeWriteFd_, &b, 1);
  }
}

void VncClientImpl::reportState(VncState state, const char* message)
{
  if (callbacks_.state_changed)
    callbacks_.state_changed(callbacks_.context, state, message);
}

void VncClientImpl::log(VncLogLevel level, const std::string& message)
{
  if (callbacks_.log)
    callbacks_.log(callbacks_.context, level, message.c_str());
}

bool VncClientImpl::waitForDecision(std::unique_lock<std::mutex>& lock)
{
  cv_.wait(lock, [this] {
    return decisionReady_ || cancelled_.load() || wantDisconnect_;
  });
  bool ok = decisionReady_ && !cancelled_.load() && !wantDisconnect_;
  decisionReady_ = false;
  return ok;
}

void VncClientImpl::getUserPasswd(bool /*secure*/, std::string* user,
                                   std::string* password)
{
  if (callbacks_.credentials_needed)
    callbacks_.credentials_needed(callbacks_.context, user != nullptr ? 1 : 0);

  std::unique_lock<std::mutex> lock(mutex_);
  if (!waitForDecision(lock))
    throw rfb::auth_cancelled();
  if (user)
    *user = pendingUsername_;
  if (password)
    *password = pendingPassword_;
}

bool VncClientImpl::verifyCertificate(unsigned int status,
                                       const uint8_t* certificate,
                                       size_t length)
{
  std::string fingerprint = "SHA256:" + hexDigest(certificate, length);
  if (status != 0) {
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                   " (unvalidated, status=0x%x)", status);
    fingerprint += buf;
  }

  if (callbacks_.verify_server)
    callbacks_.verify_server(callbacks_.context, fingerprint.c_str());

  std::unique_lock<std::mutex> lock(mutex_);
  if (!waitForDecision(lock))
    return false;
  return pendingAccept_;
}

bool VncClientImpl::verifyHostKey(const uint8_t* /*key*/, size_t /*length*/,
                                   const char* fingerprint)
{
  if (callbacks_.verify_server)
    callbacks_.verify_server(callbacks_.context, fingerprint);

  std::unique_lock<std::mutex> lock(mutex_);
  if (!waitForDecision(lock))
    return false;
  return pendingAccept_;
}

void VncClientImpl::resetFramebuffer(int width, int height)
{
  // Resize the existing buffer in place rather than swapping in a new
  // one via setFramebuffer(). CConnection::setExtendedDesktopSize()
  // unconditionally calls resizeFramebuffer() -- including once right
  // after the very first update, even when nothing actually changed --
  // and any rect decode already in flight for that update holds a
  // pointer to the buffer fetched at framebufferUpdateStart(). Replacing
  // the object out from under it would orphan that pointer, so the
  // real pixel data lands in a buffer we never look at again while we
  // read from a fresh, still-blank one.
  auto* buffer = static_cast<rfb::ManagedPixelBuffer*>(getFramebuffer());
  if (!buffer) {
    buffer = new rfb::ManagedPixelBuffer(framebufferFormat(), width, height);
    setFramebuffer(buffer);
    return;
  }
  buffer->setPF(framebufferFormat());
  buffer->setSize(width, height);
}

void VncClientImpl::initDone()
{
  setPF(framebufferFormat());
  resetFramebuffer(server.width(), server.height());
  // Only now has the RFB state machine actually reached RFBSTATE_NORMAL
  // (version exchange, security type, auth, and ServerInit all already
  // succeeded) -- see the reportState(VNC_STATE_CONNECTED) call this
  // replaced in runThread() for why doing it any earlier is wrong.
  reportState(VNC_STATE_CONNECTED, nullptr);
}

void VncClientImpl::resizeFramebuffer()
{
  resetFramebuffer(server.width(), server.height());
}

void VncClientImpl::setExtendedDesktopSize(unsigned reason, unsigned result,
                                            int w, int h,
                                            const rfb::ScreenSet& layout)
{
  // Base impl updates server.dimensions()/screenLayout() and calls
  // resizeFramebuffer() when appropriate -- must run first so our own
  // state (and the frame_update callback that follows from it) reflects
  // the new size before we report the outcome below.
  CConnection::setExtendedDesktopSize(reason, result, w, h, layout);

  if (reason != rfb::reasonClient)
    return; // a resize we didn't ask for (server- or other-client-driven)
  if (!callbacks_.resize_result)
    return;

  VncResizeResult mapped;
  switch (result) {
  case rfb::resultSuccess:      mapped = VNC_RESIZE_OK; break;
  case rfb::resultProhibited:   mapped = VNC_RESIZE_PROHIBITED; break;
  case rfb::resultNoResources:  mapped = VNC_RESIZE_NO_RESOURCES; break;
  default:                      mapped = VNC_RESIZE_INVALID; break;
  }
  callbacks_.resize_result(callbacks_.context, mapped);
}

void VncClientImpl::sendResizeRequest(int width, int height)
{
  if (!server.supportsSetDesktopSize) {
    if (callbacks_.resize_result)
      callbacks_.resize_result(callbacks_.context, VNC_RESIZE_UNSUPPORTED);
    return;
  }
  if (width <= 0 || height <= 0)
    return;
  if (width == server.width() && height == server.height())
    return; // already this size; avoid a pointless round-trip

  // Single-screen "windowed mode" case from vncviewer's DesktopWindow::
  // remoteResize() -- this app has no fullscreen/multi-monitor concept,
  // so always report exactly one screen covering the whole framebuffer.
  rfb::ScreenSet layout = server.screenLayout();
  if (layout.num_screens() == 0) {
    layout.add_screen(rfb::Screen());
  } else if (layout.num_screens() != 1) {
    rfb::ScreenSet::iterator iter;
    while (true) {
      iter = layout.begin();
      ++iter;
      if (iter == layout.end())
        break;
      layout.remove_screen(iter->id);
    }
  }
  layout.begin()->dimensions.tl.x = 0;
  layout.begin()->dimensions.tl.y = 0;
  layout.begin()->dimensions.br.x = width;
  layout.begin()->dimensions.br.y = height;

  writer()->writeSetDesktopSize(width, height, layout);
}

void VncClientImpl::framebufferUpdateEnd()
{
  CConnection::framebufferUpdateEnd();

  if (!callbacks_.frame_update)
    return;

  rfb::ModifiablePixelBuffer* fb = getFramebuffer();
  if (!fb)
    return;

  int stride = 0;
  core::Rect whole(0, 0, fb->width(), fb->height());
  const uint8_t* pixels = fb->getBuffer(whole, &stride);
  callbacks_.frame_update(callbacks_.context, pixels,
                           fb->width(), fb->height(), stride * 4);
}

void VncClientImpl::handleClipboardData(const char* data)
{
  if (callbacks_.clipboard_text)
    callbacks_.clipboard_text(callbacks_.context, data ? data : "");
}

void VncClientImpl::handleClipboardAnnounce(bool available)
{
  // Auto-fetch, matching common desktop VNC client clipboard UX. A more
  // deliberate policy (e.g. only on explicit paste) can replace this in
  // the phase 5 feature-parity pass if needed.
  if (available)
    requestClipboard();
}

void VncClientImpl::bell()
{
  // Not surfaced yet -- see DESIGN.md phase 5.
}

void VncClientImpl::runThread(std::string host, int port, bool shared)
{
  reportState(VNC_STATE_CONNECTING, nullptr);

  try {
    network::TcpSocket sock(host.c_str(), port);

    setServerName(host.c_str());
    setShared(shared);
    setStreams(&sock.inStream(), &sock.outStream());
    initialiseProtocol();

    int sockFd = sock.getFd();
    for (;;) {
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(sockFd, &rfds);
      FD_SET(wakeReadFd_, &rfds);
      int nfds = (sockFd > wakeReadFd_ ? sockFd : wakeReadFd_) + 1;

      int n = select(nfds, &rfds, nullptr, nullptr, nullptr);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        throw core::posix_error("select", errno);
      }

      if (FD_ISSET(wakeReadFd_, &rfds)) {
        char buf[64];
        while (read(wakeReadFd_, buf, sizeof(buf)) > 0) {
        }

        std::unique_lock<std::mutex> lock(mutex_);
        bool disconnectNow = wantDisconnect_;
        bool refreshNow = wantRefresh_;
        wantRefresh_ = false;
        bool clipboardNow = wantRequestClipboard_;
        wantRequestClipboard_ = false;
        bool resizeNow = wantResize_;
        wantResize_ = false;
        int resizeW = resizeWidth_;
        int resizeH = resizeHeight_;
        bool pointerPending = havePendingPointer_;
        havePendingPointer_ = false;
        OutgoingPointer pointer = pendingPointer_;
        std::vector<OutgoingKey> keys;
        keys.swap(pendingKeys_);
        std::vector<std::string> clips;
        clips.swap(pendingClipboard_);
        lock.unlock();

        if (pointerPending)
          writer()->writePointerEvent(core::Point(pointer.x, pointer.y),
                                       static_cast<uint16_t>(pointer.buttonMask));
        for (const auto& k : keys) {
          if (k.down)
            sendKeyPress(static_cast<int>(k.keysym), 0, k.keysym);
          else
            sendKeyRelease(static_cast<int>(k.keysym));
        }
        for (const auto& text : clips) {
          announceClipboard(true);
          sendClipboardData(text.c_str());
        }
        if (clipboardNow)
          // Qualified: VncClientImpl::requestClipboard() (the public,
          // queuing wrapper below) has the same name and would
          // otherwise hide CConnection::requestClipboard() (the real
          // protocol call) via ordinary C++ name hiding, re-queuing
          // itself forever instead of ever actually sending the
          // request.
          CConnection::requestClipboard();
        if (refreshNow)
          refreshFramebuffer();
        if (resizeNow)
          sendResizeRequest(resizeW, resizeH);

        if (disconnectNow)
          break;
      }

      if (FD_ISSET(sockFd, &rfds)) {
        while (processMsg()) {
        }
      }
    }

    close();
  } catch (const std::exception& e) {
    log(VNC_LOG_ERROR, e.what());
    reportState(VNC_STATE_ERROR, e.what());
    return;
  }

  reportState(VNC_STATE_DISCONNECTED, nullptr);
}

} // namespace tigervnc_core
