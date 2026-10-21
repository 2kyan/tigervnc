import SwiftUI
import UIKit

/// Renders the decoded VNC frame and owns all pointer input for it: touch
/// (tap/drag = primary click), trackpad/mouse hover (continuous pointer
/// movement with no button held), and trackpad/mouse secondary click
/// (right-click). Built as one UIKit view rather than SwiftUI gestures
/// layered over an Image because hover and button-masked clicks aren't
/// expressible in SwiftUI's gesture system, and mixing a UIKit gesture
/// overlay on top of a separate SwiftUI touch target risks the two
/// fighting over hit-testing.
struct RemoteScreenView: UIViewRepresentable {
    var image: CGImage?
    var onPointer: (_ x: Int, _ y: Int, _ buttonMask: UInt32) -> Void
    /// Called (debounced) with the view's own size whenever it changes
    /// -- e.g. a Split View divider drag, Stage Manager resize, or
    /// device rotation -- so the caller can ask the server to match it
    /// ("remote resize"). Units depend on `useReadableResize`: points
    /// (readable -- RFB's SetDesktopSize has no DPI concept, so
    /// requesting raw physical pixels makes the server render its
    /// desktop UI at its own default DPI assumption but at a huge pixel
    /// count, making remote text/controls physically tiny) or raw
    /// pixels (sharper, smaller remote UI).
    var onResize: ((_ width: Int, _ height: Int) -> Void)? = nil
    var useReadableResize: Bool = true

    func makeUIView(context: Context) -> RemoteScreenUIView {
        let view = RemoteScreenUIView()
        view.onPointer = onPointer
        view.onResize = onResize
        view.useReadableResize = useReadableResize
        return view
    }

    func updateUIView(_ uiView: RemoteScreenUIView, context: Context) {
        uiView.onPointer = onPointer
        uiView.onResize = onResize
        uiView.useReadableResize = useReadableResize
        uiView.image = image
    }
}

final class RemoteScreenUIView: UIView {
    var onPointer: ((_ x: Int, _ y: Int, _ buttonMask: UInt32) -> Void)?
    var onResize: ((_ width: Int, _ height: Int) -> Void)?
    var useReadableResize: Bool = true {
        didSet {
            guard useReadableResize != oldValue else { return }
            // Force a fresh request in the new unit immediately, rather
            // than waiting for the next layout pass (bounds may not have
            // changed at all if the user just toggled the setting).
            lastScheduledSize = nil
            scheduleResizeIfNeeded()
        }
    }

    var image: CGImage? {
        didSet {
            guard image !== oldValue else { return }
            imageSize = image.map { CGSize(width: $0.width, height: $0.height) }
            layer.contents = image
        }
    }

    private var imageSize: CGSize?
    // Debounced so a live Split View/Stage Manager drag (many rapid
    // layout passes) sends one resize request once it settles, not one
    // per pixel of movement.
    private var resizeDebounceTimer: Timer?
    private var lastScheduledSize: CGSize?
    /// True while a touch or trackpad primary click is down, so hover
    /// events (which iPadOS keeps delivering the instant a click starts
    /// on some trackpads) don't stomp the held-button state with a
    /// spurious "no buttons down" move.
    private var primaryDown = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black
        layer.contentsGravity = .resizeAspect
        isMultipleTouchEnabled = false

        let hover = UIHoverGestureRecognizer(target: self, action: #selector(handleHover(_:)))
        addGestureRecognizer(hover)

        let secondaryClick = UITapGestureRecognizer(target: self, action: #selector(handleSecondaryClick(_:)))
        secondaryClick.buttonMaskRequired = .secondary
        // buttonMaskRequired only filters *indirect* (trackpad/mouse)
        // input; without also restricting touch type, a plain one-finger
        // tap would be misread as a secondary click too.
        secondaryClick.allowedTouchTypes = [NSNumber(value: UITouch.TouchType.indirectPointer.rawValue)]
        addGestureRecognizer(secondaryClick)

        addInteraction(UIPointerInteraction(delegate: self))
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    deinit {
        resizeDebounceTimer?.invalidate()
    }

    // MARK: - Remote resize (ask the server to match our own size)

    override func layoutSubviews() {
        super.layoutSubviews()
        scheduleResizeIfNeeded()
    }

    private func scheduleResizeIfNeeded() {
        guard bounds.width > 0, bounds.height > 0 else { return }
        let size: CGSize
        if useReadableResize {
            size = CGSize(width: bounds.width.rounded(), height: bounds.height.rounded())
        } else {
            let scale = traitCollection.displayScale > 0 ? traitCollection.displayScale : 1
            size = CGSize(width: (bounds.width * scale).rounded(),
                           height: (bounds.height * scale).rounded())
        }
        guard size != lastScheduledSize else { return }
        lastScheduledSize = size

        resizeDebounceTimer?.invalidate()
        resizeDebounceTimer = Timer.scheduledTimer(withTimeInterval: 0.35, repeats: false) { [weak self] _ in
            guard let self, let size = self.lastScheduledSize else { return }
            self.onResize?(Int(size.width), Int(size.height))
        }
    }

    // MARK: - Touch = primary click (works for finger touches and for a
    // trackpad's primary click, which iPadOS delivers as a normal UITouch).

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        primaryDown = true
        forward(touches.first, buttonMask: UInt32(VNC_BUTTON_LEFT))
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        forward(touches.first, buttonMask: UInt32(VNC_BUTTON_LEFT))
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        primaryDown = false
        forward(touches.first, buttonMask: 0)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        primaryDown = false
        forward(touches.first, buttonMask: 0)
    }

    private func forward(_ touch: UITouch?, buttonMask: UInt32) {
        guard let touch else { return }
        sendPointer(at: touch.location(in: self), buttonMask: buttonMask)
    }

    // MARK: - Trackpad/mouse hover (movement with no button held)

    @objc private func handleHover(_ recognizer: UIHoverGestureRecognizer) {
        guard !primaryDown else { return } // touchesMoved already covers this
        switch recognizer.state {
        case .began, .changed, .ended:
            sendPointer(at: recognizer.location(in: self), buttonMask: 0)
        default:
            break
        }
    }

    // MARK: - Trackpad/mouse secondary (right) click

    @objc private func handleSecondaryClick(_ recognizer: UITapGestureRecognizer) {
        let location = recognizer.location(in: self)
        sendPointer(at: location, buttonMask: UInt32(VNC_BUTTON_RIGHT))
        sendPointer(at: location, buttonMask: 0)
    }

    // MARK: - Coordinate mapping (view point -> framebuffer pixel, aspect-fit)

    private func sendPointer(at point: CGPoint, buttonMask: UInt32) {
        guard let imageSize, imageSize.width > 0, imageSize.height > 0,
              bounds.width > 0, bounds.height > 0 else { return }
        let displayRect = Self.aspectFitRect(imageSize: imageSize, in: bounds.size)
        guard displayRect.width > 0, displayRect.height > 0 else { return }
        let relX = (point.x - displayRect.minX) / displayRect.width
        let relY = (point.y - displayRect.minY) / displayRect.height
        let x = Int((relX * imageSize.width).rounded())
        let y = Int((relY * imageSize.height).rounded())
        let clampedX = min(max(x, 0), Int(imageSize.width) - 1)
        let clampedY = min(max(y, 0), Int(imageSize.height) - 1)
        onPointer?(clampedX, clampedY, buttonMask)
    }

    private static func aspectFitRect(imageSize: CGSize, in containerSize: CGSize) -> CGRect {
        let scale = min(containerSize.width / imageSize.width,
                         containerSize.height / imageSize.height)
        let w = imageSize.width * scale
        let h = imageSize.height * scale
        let x = (containerSize.width - w) / 2
        let y = (containerSize.height - h) / 2
        return CGRect(x: x, y: y, width: w, height: h)
    }
}

extension RemoteScreenUIView: UIPointerInteractionDelegate {
    // A small dot rather than the default arrow: this pointer represents
    // the *remote* cursor's position, not a local hover/press affordance
    // (there's nothing here for the system's usual lift/highlight
    // pointer effects to make sense over), so a distinct minimal shape
    // avoids it reading as "hovering a button."
    func pointerInteraction(_ interaction: UIPointerInteraction,
                             styleFor region: UIPointerRegion) -> UIPointerStyle? {
        let diameter: CGFloat = 10
        let path = UIBezierPath(ovalIn: CGRect(x: 0, y: 0, width: diameter, height: diameter))
        let shape = UIPointerShape.path(path)
        return UIPointerStyle(shape: shape, constrainedAxes: [])
    }
}
