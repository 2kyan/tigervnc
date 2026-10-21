import Combine
import CoreGraphics
import Foundation

/// Swift-friendly wrapper around tigervnc_core's C ABI (see
/// core/include/tigervnc_core.h). One instance per connection attempt --
/// create a fresh one to reconnect, matching the C API's own contract.
///
/// All @Published properties are only ever written on the main thread
/// (the C callbacks fire on tigervnc_core's background worker thread;
/// each trampoline below hops to main before touching `self`).
final class VncBridge: ObservableObject {
    enum State: Equatable {
        case idle
        case connecting
        case connected
        case disconnected
        case failed(String)
    }

    @Published private(set) var state: State = .idle
    @Published private(set) var frame: CGImage?
    @Published var pendingCredentials: Bool = false
    @Published var pendingVerification: String?
    /// Flips true the first time a requestResize() comes back as
    /// unsupported, so the UI can stop bothering to ask again (e.g. hide
    /// a "resize to fit" affordance) instead of silently no-op'ing on
    /// every subsequent view-size change.
    @Published private(set) var resizeUnsupported: Bool = false
    /// Whether remote-resize requests use points (density-independent,
    /// so remote desktop UI stays readable) or raw physical pixels
    /// (sharper per-pixel detail, but remote UI can render physically
    /// tiny on a high-density display -- RFB's SetDesktopSize has no DPI
    /// concept, so the server has no way to know otherwise). Persisted
    /// across app restarts; default true since that's the
    /// correct-by-default mode.
    @Published var useReadableResize: Bool = UserDefaults.standard.object(forKey: "useReadableResize") as? Bool ?? true {
        didSet { UserDefaults.standard.set(useReadableResize, forKey: "useReadableResize") }
    }
    /// Host/port most recently passed to [connect] -- persisted across
    /// app restarts so ConnectView can pre-fill them instead of always
    /// starting blank.
    @Published private(set) var lastHost: String = UserDefaults.standard.string(forKey: "lastHost") ?? "" {
        didSet { UserDefaults.standard.set(lastHost, forKey: "lastHost") }
    }
    @Published private(set) var lastPort: Int32 = {
        let stored = UserDefaults.standard.integer(forKey: "lastPort")
        return stored != 0 ? Int32(stored) : 5900
    }() {
        didSet { UserDefaults.standard.set(Int(lastPort), forKey: "lastPort") }
    }

    private var client: OpaquePointer?

    func connect(host: String, port: Int32, shared: Bool = true) {
        disconnect()
        resizeUnsupported = false
        lastHost = host
        lastPort = port

        var callbacks = VncClientCallbacks()
        callbacks.context = Unmanaged.passUnretained(self).toOpaque()
        callbacks.frame_update = { context, pixels, width, height, stride in
            guard let context, let pixels else { return }
            let bridge = Unmanaged<VncBridge>.fromOpaque(context).takeUnretainedValue()
            let byteCount = Int(stride) * Int(height)
            let data = Data(bytes: pixels, count: byteCount)
            let image = VncBridge.makeImage(data: data, width: Int(width),
                                             height: Int(height), stride: Int(stride))
            DispatchQueue.main.async { bridge.frame = image }
        }
        callbacks.state_changed = { context, newState, message in
            guard let context else { return }
            let bridge = Unmanaged<VncBridge>.fromOpaque(context).takeUnretainedValue()
            let text = message.map { String(cString: $0) }
            DispatchQueue.main.async {
                switch newState {
                case VNC_STATE_CONNECTING: bridge.state = .connecting
                case VNC_STATE_CONNECTED: bridge.state = .connected
                case VNC_STATE_DISCONNECTED: bridge.state = .disconnected
                case VNC_STATE_ERROR: bridge.state = .failed(text ?? "Unknown error")
                default: break
                }
            }
        }
        callbacks.credentials_needed = { context, _ in
            guard let context else { return }
            let bridge = Unmanaged<VncBridge>.fromOpaque(context).takeUnretainedValue()
            DispatchQueue.main.async { bridge.pendingCredentials = true }
        }
        callbacks.verify_server = { context, fingerprint in
            guard let context else { return }
            let bridge = Unmanaged<VncBridge>.fromOpaque(context).takeUnretainedValue()
            let text = fingerprint.map { String(cString: $0) } ?? ""
            DispatchQueue.main.async { bridge.pendingVerification = text }
        }
        callbacks.log = { _, level, message in
            guard let message else { return }
            #if DEBUG
            print("[tigervnc:\(level.rawValue)] \(String(cString: message))")
            #endif
        }
        callbacks.resize_result = { context, result in
            guard let context, result == VNC_RESIZE_UNSUPPORTED else { return }
            let bridge = Unmanaged<VncBridge>.fromOpaque(context).takeUnretainedValue()
            DispatchQueue.main.async { bridge.resizeUnsupported = true }
        }

        client = vnc_client_create(&callbacks)
        host.withCString { hostPtr in
            var options = VncConnectOptions(host: hostPtr, port: port, shared: shared ? 1 : 0)
            vnc_client_connect(client, &options)
        }
    }

    func disconnect() {
        guard let client else { return }
        vnc_client_disconnect(client)
        vnc_client_destroy(client)
        self.client = nil
    }

    func provideCredentials(username: String?, password: String) {
        pendingCredentials = false
        guard let client else { return }
        if let username {
            username.withCString { userPtr in
                password.withCString { passPtr in
                    vnc_client_provide_credentials(client, userPtr, passPtr)
                }
            }
        } else {
            password.withCString { passPtr in
                vnc_client_provide_credentials(client, nil, passPtr)
            }
        }
    }

    func verifyServer(accept: Bool) {
        pendingVerification = nil
        guard let client else { return }
        vnc_client_verify_response(client, accept ? 1 : 0)
    }

    func sendPointer(x: Int, y: Int, buttonMask: UInt32) {
        guard let client else { return }
        vnc_client_send_pointer_event(client, Int32(x), Int32(y), buttonMask)
    }

    func sendKey(keysym: UInt32, down: Bool) {
        guard let client else { return }
        vnc_client_send_key_event(client, keysym, down ? 1 : 0)
    }

    func requestRefresh() {
        guard let client else { return }
        vnc_client_request_refresh(client)
    }

    /// Asks the server to resize its desktop to the given dimensions
    /// ("remote resize"). Units are the caller's choice (RemoteScreenView
    /// picks points or raw pixels based on [useReadableResize] before
    /// calling this) -- this call itself is unit-agnostic, since RFB's
    /// SetDesktopSize just carries whatever width/height it's given.
    /// Safe to call freely; coalesces server-side to the latest size if
    /// called again before an earlier request is answered.
    func requestResize(width: Int, height: Int) {
        guard let client else { return }
        vnc_client_request_resize(client, Int32(width), Int32(height))
    }

    deinit {
        disconnect()
    }

    private static func makeImage(data: Data, width: Int, height: Int, stride: Int) -> CGImage? {
        guard width > 0, height > 0, stride > 0 else { return nil }
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        // Our buffer is packed 32bpp with byte order R,G,B,<unused> -- see
        // tigervnc_core.h. noneSkipLast + default (big-endian component)
        // byte order interprets bytes in exactly that order.
        let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue)
        return CGImage(width: width, height: height, bitsPerComponent: 8,
                        bitsPerPixel: 32, bytesPerRow: stride,
                        space: colorSpace, bitmapInfo: bitmapInfo,
                        provider: provider, decode: nil,
                        shouldInterpolate: false, intent: .defaultIntent)
    }
}
