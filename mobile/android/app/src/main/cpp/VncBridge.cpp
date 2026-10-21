// JNI glue between com.tigervnc.mobile.viewer.VncBridge (Kotlin) and
// tigervnc_core's C ABI (../../../../../core/include/tigervnc_core.h).
// Mirrors ios/App/VncBridge.swift's role: trampolines the C callbacks
// (which fire on tigervnc_core's own background thread) into the host
// language, attaching that thread to the JVM as needed.

#include <jni.h>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "tigervnc_core.h"

namespace {

struct BridgeContext {
  JavaVM* jvm = nullptr;
  jobject bridge = nullptr;  // global ref to the Kotlin VncBridge instance
  jclass bridgeClass = nullptr;  // global ref
  jmethodID onStateChanged = nullptr;
  jmethodID onFrameUpdate = nullptr;
  jmethodID onCredentialsNeeded = nullptr;
  jmethodID onVerifyServer = nullptr;
  jmethodID onClipboardText = nullptr;
  jmethodID onLog = nullptr;
  jmethodID onResizeResult = nullptr;
};

// Attaches the calling native thread to the JVM for the duration of one
// callback. tigervnc_core's worker thread outlives any single callback,
// so we attach/detach each time rather than leaking a permanent
// attachment (VncClient's worker thread is not JNI-created, so nothing
// else would detach it on thread exit).
class ScopedEnv {
 public:
  explicit ScopedEnv(JavaVM* jvm) : jvm_(jvm) {
    if (jvm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_EDETACHED) {
      jvm_->AttachCurrentThread(&env_, nullptr);
      attached_ = true;
    }
  }
  ~ScopedEnv() {
    if (attached_) jvm_->DetachCurrentThread();
  }
  JNIEnv* get() const { return env_; }

 private:
  JavaVM* jvm_;
  JNIEnv* env_ = nullptr;
  bool attached_ = false;
};

jstring ToJString(JNIEnv* env, const char* s) {
  return env->NewStringUTF(s != nullptr ? s : "");
}

void OnFrameUpdate(void* context, const uint8_t* pixels, int width, int height, int stride) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;

  // Convert packed RGBA8888 (R,G,B,unused; see tigervnc_core.h) rows,
  // which may be padded to `stride` bytes, into a tightly-packed
  // Android "color int" array (0xAARRGGBB per pixel, alpha forced
  // opaque -- VNC framebuffers have no alpha channel).
  std::vector<jint> argb(static_cast<size_t>(width) * height);
  for (int y = 0; y < height; y++) {
    const uint8_t* row = pixels + static_cast<size_t>(y) * stride;
    jint* out = argb.data() + static_cast<size_t>(y) * width;
    for (int x = 0; x < width; x++) {
      const uint8_t r = row[x * 4 + 0];
      const uint8_t g = row[x * 4 + 1];
      const uint8_t b = row[x * 4 + 2];
      out[x] = static_cast<jint>(0xFF000000u | (r << 16) | (g << 8) | b);
    }
  }

  jintArray array = env.get()->NewIntArray(width * height);
  env.get()->SetIntArrayRegion(array, 0, width * height, argb.data());
  env.get()->CallVoidMethod(ctx->bridge, ctx->onFrameUpdate, array, width, height);
  env.get()->DeleteLocalRef(array);
}

void OnStateChanged(void* context, VncState state, const char* message) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;
  jstring jmessage = message != nullptr ? ToJString(env.get(), message) : nullptr;
  env.get()->CallVoidMethod(ctx->bridge, ctx->onStateChanged, static_cast<jint>(state), jmessage);
  if (jmessage != nullptr) env.get()->DeleteLocalRef(jmessage);
}

void OnCredentialsNeeded(void* context, int need_username) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;
  env.get()->CallVoidMethod(ctx->bridge, ctx->onCredentialsNeeded,
                             static_cast<jboolean>(need_username != 0));
}

void OnVerifyServer(void* context, const char* fingerprint) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;
  jstring jfingerprint = ToJString(env.get(), fingerprint);
  env.get()->CallVoidMethod(ctx->bridge, ctx->onVerifyServer, jfingerprint);
  env.get()->DeleteLocalRef(jfingerprint);
}

void OnClipboardText(void* context, const char* text) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;
  jstring jtext = ToJString(env.get(), text);
  env.get()->CallVoidMethod(ctx->bridge, ctx->onClipboardText, jtext);
  env.get()->DeleteLocalRef(jtext);
}

void OnLog(void* context, VncLogLevel level, const char* message) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;
  jstring jmessage = ToJString(env.get(), message);
  env.get()->CallVoidMethod(ctx->bridge, ctx->onLog, static_cast<jint>(level), jmessage);
  env.get()->DeleteLocalRef(jmessage);
}

void OnResizeResult(void* context, VncResizeResult result) {
  auto* ctx = static_cast<BridgeContext*>(context);
  ScopedEnv env(ctx->jvm);
  if (!env.get()) return;
  env.get()->CallVoidMethod(ctx->bridge, ctx->onResizeResult, static_cast<jint>(result));
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeCreate(JNIEnv* env, jobject thiz) {
  auto* ctx = new BridgeContext();
  env->GetJavaVM(&ctx->jvm);
  ctx->bridge = env->NewGlobalRef(thiz);
  jclass localClass = env->GetObjectClass(thiz);
  ctx->bridgeClass = static_cast<jclass>(env->NewGlobalRef(localClass));
  env->DeleteLocalRef(localClass);

  ctx->onStateChanged = env->GetMethodID(ctx->bridgeClass, "onStateChanged", "(ILjava/lang/String;)V");
  ctx->onFrameUpdate = env->GetMethodID(ctx->bridgeClass, "onFrameUpdate", "([III)V");
  ctx->onCredentialsNeeded = env->GetMethodID(ctx->bridgeClass, "onCredentialsNeeded", "(Z)V");
  ctx->onVerifyServer = env->GetMethodID(ctx->bridgeClass, "onVerifyServer", "(Ljava/lang/String;)V");
  ctx->onClipboardText = env->GetMethodID(ctx->bridgeClass, "onClipboardText", "(Ljava/lang/String;)V");
  ctx->onLog = env->GetMethodID(ctx->bridgeClass, "onLog", "(ILjava/lang/String;)V");
  ctx->onResizeResult = env->GetMethodID(ctx->bridgeClass, "onResizeResult", "(I)V");

  VncClientCallbacks callbacks{};
  callbacks.context = ctx;
  callbacks.frame_update = OnFrameUpdate;
  callbacks.state_changed = OnStateChanged;
  callbacks.credentials_needed = OnCredentialsNeeded;
  callbacks.verify_server = OnVerifyServer;
  callbacks.clipboard_text = OnClipboardText;
  callbacks.log = OnLog;
  callbacks.resize_result = OnResizeResult;

  VncClient* client = vnc_client_create(&callbacks);
  // BridgeContext outlives the VncClient (freed alongside it below) --
  // stash it in the low bits alongside the client pointer isn't needed
  // since we own both lifetimes together via nativeDestroy.
  auto* handle = new std::pair<VncClient*, BridgeContext*>(client, ctx);
  return reinterpret_cast<jlong>(handle);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeDestroy(JNIEnv* env, jobject /*thiz*/, jlong handle) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_destroy(pair->first);
  env->DeleteGlobalRef(pair->second->bridge);
  env->DeleteGlobalRef(pair->second->bridgeClass);
  delete pair->second;
  delete pair;
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeConnect(JNIEnv* env, jobject /*thiz*/, jlong handle,
                                                          jstring host, jint port, jboolean shared) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  const char* hostChars = env->GetStringUTFChars(host, nullptr);
  VncConnectOptions options{};
  options.host = hostChars;
  options.port = port;
  options.shared = shared ? 1 : 0;
  vnc_client_connect(pair->first, &options);
  env->ReleaseStringUTFChars(host, hostChars);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeDisconnect(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_disconnect(pair->first);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeProvideCredentials(JNIEnv* env, jobject /*thiz*/, jlong handle,
                                                                     jstring username, jstring password) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  const char* usernameChars = username != nullptr ? env->GetStringUTFChars(username, nullptr) : nullptr;
  const char* passwordChars = env->GetStringUTFChars(password, nullptr);
  vnc_client_provide_credentials(pair->first, usernameChars, passwordChars);
  if (usernameChars != nullptr) env->ReleaseStringUTFChars(username, usernameChars);
  env->ReleaseStringUTFChars(password, passwordChars);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeVerifyResponse(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle,
                                                                  jboolean accept) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_verify_response(pair->first, accept ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeSendPointerEvent(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle,
                                                                    jint x, jint y, jint buttonMask) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_send_pointer_event(pair->first, x, y, static_cast<uint32_t>(buttonMask));
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeSendKeyEvent(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle,
                                                                jint keysym, jboolean down) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_send_key_event(pair->first, static_cast<uint32_t>(keysym), down ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeSendClipboardText(JNIEnv* env, jobject /*thiz*/, jlong handle,
                                                                     jstring text) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  const char* textChars = env->GetStringUTFChars(text, nullptr);
  vnc_client_send_clipboard_text(pair->first, textChars);
  env->ReleaseStringUTFChars(text, textChars);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeRequestClipboard(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_request_clipboard(pair->first);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeRequestRefresh(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_request_refresh(pair->first);
}

JNIEXPORT void JNICALL
Java_com_tigervnc_mobile_viewer_VncBridge_nativeRequestResize(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle,
                                                                 jint width, jint height) {
  auto* pair = reinterpret_cast<std::pair<VncClient*, BridgeContext*>*>(handle);
  vnc_client_request_resize(pair->first, width, height);
}

}  // extern "C"
