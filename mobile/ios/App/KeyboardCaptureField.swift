import SwiftUI
import UIKit

/// An invisible text field used purely to bring up the system keyboard
/// and capture individual key presses, without ever actually holding
/// text -- every edit is rejected (return false) so there's nothing to
/// diff or reconcile, and backspace is caught via an override rather
/// than by inferring it from a text delta.
struct KeyboardCaptureField: UIViewRepresentable {
    @Binding var isFocused: Bool
    var onCharacter: (Character) -> Void
    var onBackspace: () -> Void

    func makeUIView(context: Context) -> BackspaceCapturingTextField {
        let field = BackspaceCapturingTextField()
        field.onBackspace = onBackspace
        field.delegate = context.coordinator
        field.autocorrectionType = .no
        field.autocapitalizationType = .none
        field.smartQuotesType = .no
        field.smartDashesType = .no
        field.smartInsertDeleteType = .no
        return field
    }

    func updateUIView(_ uiView: BackspaceCapturingTextField, context: Context) {
        uiView.onBackspace = onBackspace
        if isFocused, !uiView.isFirstResponder {
            uiView.becomeFirstResponder()
        } else if !isFocused, uiView.isFirstResponder {
            uiView.resignFirstResponder()
        }
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(onCharacter: onCharacter)
    }

    final class Coordinator: NSObject, UITextFieldDelegate {
        let onCharacter: (Character) -> Void
        init(onCharacter: @escaping (Character) -> Void) {
            self.onCharacter = onCharacter
        }

        func textField(_ textField: UITextField, shouldChangeCharactersIn range: NSRange,
                        replacementString string: String) -> Bool {
            for ch in string { onCharacter(ch) }
            return false
        }
    }
}

final class BackspaceCapturingTextField: UITextField {
    var onBackspace: (() -> Void)?

    // The field's text is always empty (every edit is rejected in the
    // delegate), but the keyboard's backspace key only calls
    // deleteBackward() when hasText is true -- so lie about it.
    override var hasText: Bool { true }

    override func deleteBackward() {
        onBackspace?()
    }
}
