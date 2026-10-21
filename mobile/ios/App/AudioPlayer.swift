import AVFoundation

/// Plays the PCM stream delivered by VncBridge's audio_begin/audio_data/
/// audio_end callbacks (see tigervnc_core.h) -- remote audio from the
/// TigerVNC-native RFB audio extension, fixed format S16LE/stereo/48kHz.
///
/// Unlike frame_update (last-value-wins -- a stale frame is simply
/// discarded), audio is a real ordered stream: dropping or reordering
/// chunks is audible. `begin()`/`enqueue(_:)`/`end()` all run on a
/// private serial queue, decoupled from the `@Published`/SwiftUI
/// observation path VncBridge otherwise uses everywhere -- that queue
/// *is* the ordering/decoupling mechanism here, not a hand-rolled ring
/// buffer, since `AVAudioPlayerNode` already queues and plays scheduled
/// buffers strictly in the order `scheduleBuffer` was called, on its own
/// internal render thread.
final class AudioPlayer {
    private static let sampleRate: Double = 48000
    private static let channels: AVAudioChannelCount = 2

    private let engine = AVAudioEngine()
    private let playerNode = AVAudioPlayerNode()
    private let format = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                        sampleRate: AudioPlayer.sampleRate,
                                        channels: AudioPlayer.channels,
                                        interleaved: true)!
    private let queue = DispatchQueue(label: "com.tigervnc.mobile.viewer.audio")
    private var started = false

    init() {
        engine.attach(playerNode)
        engine.connect(playerNode, to: engine.mainMixerNode, format: format)
    }

    /// Called from VncBridge's audio_begin callback. There may be many
    /// begin/end pairs in one session (the server only sends audio while
    /// something is actually playing) -- idempotent, so repeats are safe.
    func begin() {
        queue.async { [self] in
            if !started {
                engine.prepare()
                try? engine.start()
                started = true
            }
            playerNode.play()
        }
    }

    /// Called from VncBridge's audio_data callback with one PCM chunk.
    /// `data` must already be a copy (the C callback's pointer is only
    /// valid for the duration of that call).
    func enqueue(_ data: Data) {
        queue.async { [self] in
            guard started, let buffer = AudioPlayer.makeBuffer(data: data, format: format) else { return }
            playerNode.scheduleBuffer(buffer, completionHandler: nil)
        }
    }

    /// Called from VncBridge's audio_end callback. Deliberately does not
    /// stop anything -- already-scheduled buffers finish playing
    /// naturally, and the server may resume shortly.
    func end() {}

    /// Immediate local mute/unmute -- muting the server (a separate
    /// control message VncBridge sends alongside this) stops new audio
    /// arriving, but does nothing about PCM already scheduled into
    /// `playerNode` via earlier `enqueue(_:)` calls, which would
    /// otherwise keep audibly playing for a moment after mute.
    /// `stop()` cancels all pending scheduled buffers immediately;
    /// `play()` resumes accepting/playing newly scheduled ones on unmute.
    func setMuted(_ muted: Bool) {
        queue.async { [self] in
            guard started else { return }
            if muted {
                playerNode.stop()
            } else {
                playerNode.play()
            }
        }
    }

    /// Called from VncBridge.disconnect()/deinit -- unlike end(), this
    /// actually tears the engine down.
    func teardown() {
        queue.async { [self] in
            guard started else { return }
            playerNode.stop()
            engine.stop()
            started = false
        }
    }

    private static func makeBuffer(data: Data, format: AVAudioFormat) -> AVAudioPCMBuffer? {
        let bytesPerFrame = Int(format.streamDescription.pointee.mBytesPerFrame)
        guard bytesPerFrame > 0, data.count % bytesPerFrame == 0 else { return nil }
        let frameCount = AVAudioFrameCount(data.count / bytesPerFrame)
        guard frameCount > 0,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount)
        else { return nil }
        buffer.frameLength = frameCount
        data.withUnsafeBytes { raw in
            guard let base = raw.baseAddress else { return }
            memcpy(buffer.int16ChannelData![0], base, data.count)
        }
        return buffer
    }
}
