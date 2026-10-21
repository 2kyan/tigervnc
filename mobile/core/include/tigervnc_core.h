/* Public C ABI for the TigerVNC mobile core wrapper.
 *
 * Wraps common/rfb::CConnection (the client-side RFB
 * protocol engine) behind a small, stable C surface so it can be bound
 * from Kotlin (via JNI) and Swift (via a C interop / bridging header)
 * without either language needing to touch the underlying C++.
 *
 * Threading model: a VncClient owns one background thread per connection
 * that performs the blocking connect, runs the RFB protocol read loop,
 * and invokes every callback below. None of the vnc_client_* functions
 * block waiting on network I/O -- they either act immediately (queuing
 * outgoing data) or hand off to that thread. Callbacks are therefore
 * always invoked off the caller's thread; the embedder is responsible
 * for hopping back to its UI thread before touching UI state. All
 * vnc_client_* functions are safe to call from any thread, including
 * from within a callback.
 */

#ifndef TIGERVNC_CORE_H
#define TIGERVNC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VncClient VncClient;

typedef enum {
  VNC_STATE_CONNECTING,     /* connect() called, handshake in progress */
  VNC_STATE_CONNECTED,      /* session established, frames will follow */
  VNC_STATE_DISCONNECTED,   /* closed normally (disconnect() or server-initiated) */
  VNC_STATE_ERROR           /* closed abnormally; see message in the callback */
} VncState;

typedef enum {
  VNC_LOG_DEBUG,
  VNC_LOG_INFO,
  VNC_LOG_ERROR
} VncLogLevel;

typedef enum {
  VNC_RESIZE_OK,           /* server resized; a frame_update with the new
                             * dimensions follows (or already arrived) */
  VNC_RESIZE_PROHIBITED,   /* server understood but refused (policy) */
  VNC_RESIZE_NO_RESOURCES, /* server understood but couldn't allocate it */
  VNC_RESIZE_INVALID,      /* server rejected the requested layout/size */
  VNC_RESIZE_UNSUPPORTED   /* server never advertised remote-resize support;
                             * nothing was sent over the wire */
} VncResizeResult;

/* Pointer button mask bits for vnc_client_send_pointer_event(). */
#define VNC_BUTTON_LEFT   (1u << 0)
#define VNC_BUTTON_MIDDLE (1u << 1)
#define VNC_BUTTON_RIGHT  (1u << 2)
#define VNC_WHEEL_UP      (1u << 3)
#define VNC_WHEEL_DOWN    (1u << 4)

typedef struct {
  void* context; /* passed back as the first argument to every callback */

  /* A framebuffer update has completed. `pixels` points at the top-left
   * pixel of a `width` x `height` buffer, `stride` bytes per row, one
   * packed 32-bit RGBA8888 sample per pixel (byte 0 = R, 1 = G, 2 = B,
   * byte 3 unused -- VNC has no alpha channel). Valid only for the
   * duration of this call; copy it if you need it afterward. Called
   * after every batch of changes, including the very first full-screen
   * update and any resize (check width/height each time). */
  void (*frame_update)(void* context, const uint8_t* pixels,
                        int width, int height, int stride);

  /* Connection lifecycle changed. `message` is non-null only for
   * VNC_STATE_ERROR (a short, English, not-for-parsing description). */
  void (*state_changed)(void* context, VncState state, const char* message);

  /* The server requires credentials. If need_username is 0, only a
   * password is expected (leave username unset in the response).
   * Respond -- from any thread, synchronously or later -- with
   * vnc_client_provide_credentials(). */
  void (*credentials_needed)(void* context, int need_username);

  /* The server presented a certificate/host key. `fingerprint` is a
   * short human-readable string (e.g. a hex hash) suitable for a
   * confirmation prompt. Respond with vnc_client_verify_response(). */
  void (*verify_server)(void* context, const char* fingerprint);

  /* The server's clipboard changed and its contents were fetched (after
   * vnc_client_request_clipboard()), or the server pushed new text. */
  void (*clipboard_text)(void* context, const char* text);

  /* Diagnostic logging; safe to ignore (pass nullptr) in production. */
  void (*log)(void* context, VncLogLevel level, const char* message);

  /* Answers a previous vnc_client_request_resize() call. Optional --
   * frame_update already reflects any successful resize (dimensions
   * will differ), so this is only useful for surfacing an explicit
   * failure/unsupported reason to the user instead of silently doing
   * nothing. */
  void (*resize_result)(void* context, VncResizeResult result);

  /* Remote audio. Only fires if the server advertises support for the
   * TigerVNC-native audio extension (or, as a fallback, QEMU's own VNC
   * audio extension) -- most servers advertise neither, in which case
   * these three never fire at all and nothing else changes. There may
   * be many begin/end pairs in a single session, as the server only
   * sends audio while something is actually playing. Format is always
   * fixed: 16-bit signed little-endian PCM, 2 channels (interleaved),
   * 48000 Hz -- there is no per-connection negotiation in this version. */
  void (*audio_begin)(void* context);
  void (*audio_end)(void* context);

  /* `pcm` points at `length` bytes of raw audio, in the fixed format
   * documented above. Valid only for the duration of this call; copy
   * it if you need it afterward (same contract as frame_update). */
  void (*audio_data)(void* context, const uint8_t* pcm, size_t length);
} VncClientCallbacks;

typedef struct {
  const char* host;
  int port;
  int shared; /* non-zero: request a shared (non-exclusive) session */
} VncConnectOptions;

/* Creates a client. `callbacks` is copied; it may be stack-allocated.
 * Any of its function pointers may be nullptr to ignore that event. */
VncClient* vnc_client_create(const VncClientCallbacks* callbacks);

/* Disconnects if needed and frees the client. Blocks until the
 * background thread has exited. Do not call from within a callback. */
void vnc_client_destroy(VncClient* client);

/* Starts connecting on the background thread; returns immediately.
 * Only valid on a freshly-created or fully-disconnected client. */
void vnc_client_connect(VncClient* client, const VncConnectOptions* options);

/* Requests a disconnect; the background thread will finish tearing
 * down and report VNC_STATE_DISCONNECTED. Safe to call at any time,
 * including if not connected. */
void vnc_client_disconnect(VncClient* client);

/* Answers a pending credentials_needed callback. `username` may be
 * nullptr if none was requested. */
void vnc_client_provide_credentials(VncClient* client,
                                     const char* username,
                                     const char* password);

/* Answers a pending verify_server callback. Non-zero accepts. */
void vnc_client_verify_response(VncClient* client, int accept);

/* x/y are in framebuffer pixel coordinates; button_mask is a bitwise OR
 * of the VNC_BUTTON_ and VNC_WHEEL_ constants for buttons currently held
 * down. */
void vnc_client_send_pointer_event(VncClient* client, int x, int y,
                                    uint32_t button_mask);

/* keysym is an X11 keysym value (see keysymdef.h conventions); `down`
 * is non-zero for key-press, zero for key-release. */
void vnc_client_send_key_event(VncClient* client, uint32_t keysym, int down);

/* Informs the server of a local clipboard change; the server may later
 * request it (not surfaced yet -- see DESIGN.md phase 5). */
void vnc_client_send_clipboard_text(VncClient* client, const char* text);

/* Asks the server for its current clipboard; result arrives via the
 * clipboard_text callback. */
void vnc_client_request_clipboard(VncClient* client);

/* Forces a full-screen (non-incremental) refresh. */
void vnc_client_request_refresh(VncClient* client);

/* Requests the server resize its virtual desktop to the given pixel
 * dimensions -- e.g. to match the current client window/view size
 * ("remote resize", the same feature TigerVNC's own desktop viewer
 * offers). A no-op, reported as VNC_RESIZE_UNSUPPORTED, if the server
 * never advertised support for it (learned from the server sometime
 * around the first framebuffer update; safe to call before that's known
 * -- it'll just resolve as unsupported rather than block). Repeated
 * calls before the server responds to an earlier one are coalesced to
 * only the latest size -- safe to call on every layout pass without
 * your own debouncing, though avoiding a flood of calls during e.g. a
 * live window drag is still courteous to the server. */
void vnc_client_request_resize(VncClient* client, int width, int height);

/* Enables or disables remote audio (see audio_begin/audio_end/audio_data
 * above). Enabled by default once a connection completes -- this is only
 * needed to mute/unmute. A no-op (never sent over the wire) if the
 * server's audio support isn't known to be present yet at the moment
 * this is acted on (same "resolves against current state" semantics as
 * vnc_client_request_resize() -- safe to call at any time, coalesces to
 * only the latest requested state, but a call made before the server's
 * support is confirmed can resolve as a no-op rather than being
 * retried once support is learned; intended for toggling mid-session,
 * e.g. from a mute button, not for pre-connect configuration). */
void vnc_client_set_audio_enabled(VncClient* client, int enabled);

#ifdef __cplusplus
}
#endif

#endif /* TIGERVNC_CORE_H */
