import SwiftUI

struct SessionView: View {
    @ObservedObject var bridge: VncBridge
    var onClose: () -> Void

    @State private var keyboardFocused = false

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if let frame = bridge.frame {
                RemoteScreenView(
                    image: frame,
                    onPointer: { x, y, buttonMask in
                        bridge.sendPointer(x: x, y: y, buttonMask: buttonMask)
                    },
                    onResize: { width, height in
                        bridge.requestResize(width: width, height: height)
                    },
                    useReadableResize: bridge.useReadableResize
                )
            } else {
                ProgressView().tint(.white)
            }

            KeyboardCaptureField(
                isFocused: $keyboardFocused,
                onCharacter: { ch in
                    for scalar in String(ch).unicodeScalars where scalar.value <= 0xFF {
                        bridge.sendKey(keysym: scalar.value, down: true)
                        bridge.sendKey(keysym: scalar.value, down: false)
                    }
                },
                onBackspace: {
                    bridge.sendKey(keysym: 0xFF08, down: true) // X11 BackSpace
                    bridge.sendKey(keysym: 0xFF08, down: false)
                }
            )
            .frame(width: 1, height: 1)
            .opacity(0.01)
        }
        .overlay(alignment: .topTrailing) {
            HStack(spacing: 20) {
                Button {
                    keyboardFocused.toggle()
                } label: {
                    Image(systemName: "keyboard")
                }
                Button {
                    bridge.requestRefresh()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                Button {
                    bridge.useReadableResize.toggle()
                } label: {
                    // textformat.size = Readable (points-based remote
                    // resize, legible remote UI); viewfinder = Sharp (raw
                    // pixels, crisper but smaller remote UI on a
                    // high-density display).
                    Image(systemName: bridge.useReadableResize ? "textformat.size" : "viewfinder")
                }
                Button {
                    onClose()
                } label: {
                    Image(systemName: "xmark.circle.fill")
                }
            }
            .font(.title2)
            .foregroundColor(.white)
            .padding()
            .background(.black.opacity(0.35))
            .clipShape(Capsule())
            .padding()
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
    }
}
