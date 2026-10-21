package com.tigervnc.mobile.viewer

import android.content.Context
import android.graphics.Bitmap
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import java.util.concurrent.LinkedBlockingQueue

/** Sentinels pushed onto VncBridge's audio queue alongside PCM ByteArray
 * chunks -- see the class doc comment on the queue field itself. */
private object AudioBeginMarker
private object AudioEndMarker
private object AudioStopMarker

/** Kotlin state mirroring tigervnc_core.h's VncState. */
enum class VncConnState { CONNECTING, CONNECTED, DISCONNECTED, ERROR }

/** Pointer button mask bits, matching tigervnc_core.h's VNC_BUTTON_/VNC_WHEEL_ constants. */
object VncButtons {
    const val LEFT = 1 shl 0
    const val MIDDLE = 1 shl 1
    const val RIGHT = 1 shl 2
    const val WHEEL_UP = 1 shl 3
    const val WHEEL_DOWN = 1 shl 4
}

/**
 * Kotlin/JNI bridge to tigervnc_core (see ../../../../../core/include/tigervnc_core.h
 * and cpp/VncBridge.cpp). One instance owns one VncClient.
 *
 * Threading: every native callback below arrives on tigervnc_core's own
 * background thread (see tigervnc_core.h's threading model comment), so
 * each one hops to the main thread before touching Compose state --
 * mirroring what the iOS bridge does for its @Published properties.
 */
class VncBridge(context: Context) {
    private val mainHandler = Handler(Looper.getMainLooper())
    private var nativeHandle: Long = 0
    private val prefs = context.applicationContext
        .getSharedPreferences("tigervnc_prefs", Context.MODE_PRIVATE)

    var state by mutableStateOf(VncConnState.DISCONNECTED)
        private set
    var errorMessage by mutableStateOf<String?>(null)
        private set
    var frameBitmap by mutableStateOf<Bitmap?>(null)
        private set
    var credentialsNeeded by mutableStateOf(false)
        private set
    var credentialsNeedUsername by mutableStateOf(false)
        private set
    var pendingVerifyFingerprint by mutableStateOf<String?>(null)
        private set
    /** Flips true the first time a requestResize() comes back as
     * unsupported, so the UI can stop bothering to ask again instead of
     * silently no-op'ing on every subsequent view-size change. */
    var resizeUnsupported by mutableStateOf(false)
        private set
    /** Whether remote-resize requests use dp (density-independent, so
     * remote desktop UI stays readable) or raw physical pixels (sharper
     * per-pixel detail, but remote UI can render physically tiny on a
     * high-density display -- RFB's SetDesktopSize has no DPI concept,
     * so the server has no way to know otherwise). Persisted across app
     * restarts; default true since that's the correct-by-default mode. */
    var useReadableResize by mutableStateOf(prefs.getBoolean(KEY_USE_READABLE_RESIZE, true))
        private set
    /** Host/port most recently passed to [connect] -- persisted across
     * app restarts so ConnectScreen can pre-fill them instead of always
     * starting blank. */
    var lastHost by mutableStateOf(prefs.getString(KEY_LAST_HOST, "") ?: "")
        private set
    var lastPort by mutableStateOf(prefs.getInt(KEY_LAST_PORT, 5900))
        private set
    /** UI-facing mute state (drives a button icon in SessionScreen). Only
     * fine to be Compose state because nothing performance-sensitive
     * reads it -- the actual audio path below never touches it directly,
     * reading [audioMutedFlag] instead (see that field's own comment). */
    var audioMuted by mutableStateOf(false)
        private set

    /** Remote audio: server->client PCM chunks arrive on tigervnc_core's
     * background thread (via onAudioBegin/onAudioEnd/onAudioData below)
     * as a real stream that must be queued and played in order -- unlike
     * frame_update's single most-recent-frame semantics, dropping or
     * reordering audio is audible. A dedicated [audioThread] drains this
     * queue into an [AudioTrack] in MODE_STREAM, entirely independent of
     * Compose's main-thread state (`mainHandler.post`) used everywhere
     * else in this class. Bounded so a stalled/slow consumer can't grow
     * memory unboundedly; onAudioData must never block (it runs on
     * tigervnc_core's worker thread), so overflow drops the oldest
     * chunk instead. */
    private val audioQueue = LinkedBlockingQueue<Any>(64)
    private var audioThread: Thread? = null
    /** Belt-and-suspenders local mute, checked on the playback thread
     * itself -- independent of [audioMuted] (UI state) and of the
     * server-facing enable/disable wire message nativeSetAudioEnabled
     * sends. Volatile: written from the UI-triggered setter, read from
     * audioThread. */
    @Volatile
    private var audioMutedFlag = false

    private fun offerAudioQueueItem(item: Any) {
        if (!audioQueue.offer(item)) {
            audioQueue.poll()
            audioQueue.offer(item)
        }
    }

    private fun startAudioThread() {
        audioThread = Thread {
            var track: AudioTrack? = null
            loop@ while (true) {
                when (val item = audioQueue.take()) {
                    AudioStopMarker -> break@loop
                    AudioBeginMarker -> {
                        if (track == null) {
                            track = AudioTrack.Builder()
                                .setAudioAttributes(
                                    AudioAttributes.Builder()
                                        .setUsage(AudioAttributes.USAGE_MEDIA)
                                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                                        .build()
                                )
                                .setAudioFormat(
                                    AudioFormat.Builder()
                                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                        .setSampleRate(48000)
                                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                                        .build()
                                )
                                .setTransferMode(AudioTrack.MODE_STREAM)
                                .build()
                        }
                        track.play()
                    }
                    AudioEndMarker -> {
                        // Leave the track allocated and playing, just idle
                        // (no more writes until the next begin) -- the
                        // server may resume shortly, and there may be
                        // many such periods in one session (see
                        // tigervnc_core.h's audio_begin/audio_end doc).
                    }
                    is ByteArray -> {
                        if (!audioMutedFlag) {
                            track?.write(item, 0, item.size, AudioTrack.WRITE_BLOCKING)
                        }
                    }
                }
            }
            track?.stop()
            track?.release()
        }.also { it.start() }
    }

    private fun stopAudioThread() {
        offerAudioQueueItem(AudioStopMarker)
        audioThread?.join()
        audioThread = null
        audioQueue.clear()
    }

    /** Mutes/unmutes remote audio: flips the local playback mute (belt-
     * and-suspenders, takes effect immediately even if the server never
     * answers) and asks the server to stop/resume sending, via the same
     * TigerVNC-native audio extension negotiated on connect. */
    fun updateAudioMuted(muted: Boolean) {
        audioMuted = muted
        audioMutedFlag = muted
        nativeSetAudioEnabled(nativeHandle, !muted)
    }

    fun updateUseReadableResize(value: Boolean) {
        useReadableResize = value
        prefs.edit().putBoolean(KEY_USE_READABLE_RESIZE, value).apply()
    }

    init {
        nativeHandle = nativeCreate()
    }

    fun connect(host: String, port: Int, shared: Boolean = true) {
        errorMessage = null
        resizeUnsupported = false
        lastHost = host
        lastPort = port
        prefs.edit().putString(KEY_LAST_HOST, host).putInt(KEY_LAST_PORT, port).apply()

        // vnc_client_connect() is only valid once per VncClient instance
        // (see tigervnc_core.h / VncClientImpl.h) -- its worker thread is
        // start()ed once and only ever joined by nativeDestroy(), so
        // calling nativeConnect() again on a handle that already went
        // through a connect/disconnect cycle reassigns a live
        // std::thread and crashes (std::terminate, confirmed via a
        // repeated-reconnect stress pass). MainActivity's TigerVncApp()
        // keeps one VncBridge for the whole activity lifetime (`remember`),
        // so a fresh native handle per connect() call -- not per
        // VncBridge instance -- is what actually satisfies that contract.
        nativeDestroy(nativeHandle)
        nativeHandle = nativeCreate()

        audioMutedFlag = audioMuted
        startAudioThread()
        nativeConnect(nativeHandle, host, port, shared)
    }

    fun disconnect() {
        nativeDisconnect(nativeHandle)
        stopAudioThread()
    }

    fun provideCredentials(username: String?, password: String) {
        credentialsNeeded = false
        nativeProvideCredentials(nativeHandle, username, password)
    }

    fun verifyResponse(accept: Boolean) {
        pendingVerifyFingerprint = null
        nativeVerifyResponse(nativeHandle, accept)
    }

    fun sendPointerEvent(x: Int, y: Int, buttonMask: Int) =
        nativeSendPointerEvent(nativeHandle, x, y, buttonMask)

    fun sendKeyEvent(keysym: Int, down: Boolean) =
        nativeSendKeyEvent(nativeHandle, keysym, down)

    fun sendClipboardText(text: String) = nativeSendClipboardText(nativeHandle, text)

    fun requestClipboard() = nativeRequestClipboard(nativeHandle)

    fun requestRefresh() = nativeRequestRefresh(nativeHandle)

    /** Asks the server to resize its desktop to the given dimensions
     * ("remote resize") -- e.g. to match the current view's size on
     * rotation or a windowed-mode resize. Units are the caller's choice
     * (SessionScreen picks dp or raw pixels based on [useReadableResize]
     * before calling this) -- this call itself is unit-agnostic, since
     * RFB's SetDesktopSize just carries whatever width/height it's given.
     * Safe to call freely; coalesces server-side to the latest size if
     * called again before an earlier request is answered. */
    fun requestResize(width: Int, height: Int) = nativeRequestResize(nativeHandle, width, height)

    /** Must be called when the owning screen is torn down. */
    fun destroy() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0
        }
        if (audioThread != null) stopAudioThread()
    }

    // --- Called from native code (VncBridge.cpp) on tigervnc_core's background thread ---

    private fun onStateChanged(stateOrdinal: Int, message: String?) {
        mainHandler.post {
            state = VncConnState.entries[stateOrdinal]
            errorMessage = message
        }
    }

    private fun onFrameUpdate(pixels: IntArray, width: Int, height: Int) {
        val bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
        mainHandler.post { frameBitmap = bitmap }
    }

    private fun onCredentialsNeeded(needUsername: Boolean) {
        mainHandler.post {
            credentialsNeedUsername = needUsername
            credentialsNeeded = true
        }
    }

    private fun onVerifyServer(fingerprint: String) {
        mainHandler.post { pendingVerifyFingerprint = fingerprint }
    }

    private fun onClipboardText(text: String) {
        // Surfaced to the app once clipboard sync is wired up (DESIGN.md phase 5).
    }

    private fun onResizeResult(result: Int) {
        // Ordinals per tigervnc_core.h's VncResizeResult: 0=OK,
        // 1=PROHIBITED, 2=NO_RESOURCES, 3=INVALID, 4=UNSUPPORTED. Only
        // UNSUPPORTED is actionable here -- success already shows up as
        // an ordinary frame_update with new dimensions, and the other
        // failures are rare/transient enough not to need their own UI.
        if (result == 4) {
            mainHandler.post { resizeUnsupported = true }
        }
    }

    private fun onAudioBegin() = offerAudioQueueItem(AudioBeginMarker)

    private fun onAudioEnd() = offerAudioQueueItem(AudioEndMarker)

    private fun onAudioData(pcm: ByteArray) = offerAudioQueueItem(pcm)

    private fun onLog(level: Int, message: String) {
        android.util.Log.println(
            when (level) {
                0 -> android.util.Log.DEBUG
                2 -> android.util.Log.ERROR
                else -> android.util.Log.INFO
            },
            "tigervnc_core",
            message,
        )
    }

    // --- Native surface (implemented in cpp/VncBridge.cpp) ---

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeConnect(handle: Long, host: String, port: Int, shared: Boolean)
    private external fun nativeDisconnect(handle: Long)
    private external fun nativeProvideCredentials(handle: Long, username: String?, password: String)
    private external fun nativeVerifyResponse(handle: Long, accept: Boolean)
    private external fun nativeSendPointerEvent(handle: Long, x: Int, y: Int, buttonMask: Int)
    private external fun nativeSendKeyEvent(handle: Long, keysym: Int, down: Boolean)
    private external fun nativeSendClipboardText(handle: Long, text: String)
    private external fun nativeRequestClipboard(handle: Long)
    private external fun nativeRequestRefresh(handle: Long)
    private external fun nativeRequestResize(handle: Long, width: Int, height: Int)
    private external fun nativeSetAudioEnabled(handle: Long, enabled: Boolean)

    companion object {
        private const val KEY_USE_READABLE_RESIZE = "use_readable_resize"
        private const val KEY_LAST_HOST = "last_host"
        private const val KEY_LAST_PORT = "last_port"

        init {
            System.loadLibrary("vncjni")
        }
    }
}
