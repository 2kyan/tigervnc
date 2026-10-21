#include "tigervnc_core.h"

#include "VncClientImpl.h"

using tigervnc_core::VncClientImpl;

namespace {
inline VncClientImpl* impl(VncClient* c) {
  return reinterpret_cast<VncClientImpl*>(c);
}
} // namespace

extern "C" {

VncClient* vnc_client_create(const VncClientCallbacks* callbacks)
{
  static const VncClientCallbacks empty{};
  return reinterpret_cast<VncClient*>(
      new VncClientImpl(callbacks ? *callbacks : empty));
}

void vnc_client_destroy(VncClient* client)
{
  delete impl(client);
}

void vnc_client_connect(VncClient* client, const VncConnectOptions* options)
{
  static const VncConnectOptions empty{nullptr, 0, 0};
  impl(client)->connect(options ? *options : empty);
}

void vnc_client_disconnect(VncClient* client)
{
  impl(client)->disconnect();
}

void vnc_client_provide_credentials(VncClient* client, const char* username,
                                     const char* password)
{
  impl(client)->provideCredentials(username, password);
}

void vnc_client_verify_response(VncClient* client, int accept)
{
  impl(client)->verifyResponse(accept != 0);
}

void vnc_client_send_pointer_event(VncClient* client, int x, int y,
                                    uint32_t button_mask)
{
  impl(client)->sendPointerEvent(x, y, button_mask);
}

void vnc_client_send_key_event(VncClient* client, uint32_t keysym, int down)
{
  impl(client)->sendKeyEvent(keysym, down != 0);
}

void vnc_client_send_clipboard_text(VncClient* client, const char* text)
{
  impl(client)->sendClipboardText(text);
}

void vnc_client_request_clipboard(VncClient* client)
{
  impl(client)->requestClipboard();
}

void vnc_client_request_refresh(VncClient* client)
{
  impl(client)->requestRefresh();
}

void vnc_client_request_resize(VncClient* client, int width, int height)
{
  impl(client)->requestResize(width, height);
}

void vnc_client_set_audio_enabled(VncClient* client, int enabled)
{
  impl(client)->setAudioEnabled(enabled != 0);
}

} // extern "C"
