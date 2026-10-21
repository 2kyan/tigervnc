import SwiftUI

struct ConnectView: View {
    private enum Field: Hashable {
        case host, port
    }

    @StateObject private var bridge: VncBridge
    @State private var host: String
    @State private var portText: String
    @State private var showSession = false
    @FocusState private var focusedField: Field?

    @State private var username = ""
    @State private var password = ""

    init() {
        // Seed host/port from the bridge's own persisted last-connected
        // values (see VncBridge.lastHost/lastPort) rather than blank/5900,
        // so the form remembers what you last connected to across app
        // restarts. Needs a manual init since @StateObject's default
        // no-arg initializer wouldn't let @State read from it.
        let bridge = VncBridge()
        _bridge = StateObject(wrappedValue: bridge)
        _host = State(initialValue: bridge.lastHost)
        _portText = State(initialValue: String(bridge.lastPort))
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Server") {
                    TextField("Host or IP", text: $host)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                        .keyboardType(.URL)
                        .focused($focusedField, equals: .host)
                        .submitLabel(.next)
                        .onSubmit { focusedField = .port }
                    TextField("Port", text: $portText)
                        .keyboardType(.numberPad)
                        .focused($focusedField, equals: .port)
                        .submitLabel(.go)
                        // Return/Enter here (physical keyboard -- the
                        // on-screen number pad has no visible Return key
                        // at all) submits, same as tapping Connect below.
                        .onSubmit { connect() }
                }

                if case .failed(let message) = bridge.state {
                    Section {
                        Text(message).foregroundColor(.red)
                    }
                }

                Section {
                    Button {
                        connect()
                    } label: {
                        if bridge.state == .connecting {
                            HStack {
                                ProgressView()
                                Text("Connecting…")
                            }
                        } else {
                            Text("Connect")
                        }
                    }
                    .disabled(host.isEmpty || bridge.state == .connecting)
                }
            }
            .navigationTitle("TigerVNC Viewer")
            .fullScreenCover(isPresented: $showSession) {
                SessionView(bridge: bridge, onClose: {
                    showSession = false
                    bridge.disconnect()
                })
            }
            .onChange(of: bridge.state) { _, newState in
                if newState == .connected {
                    showSession = true
                }
            }
            .alert("Sign In", isPresented: $bridge.pendingCredentials) {
                // .alert's text fields don't support @FocusState binding
                // the way Form fields do, so Return in the username field
                // just relies on the system's own default alert behavior
                // (advancing to the next field) rather than an explicit
                // focus change here.
                TextField("Username (optional)", text: $username)
                    .submitLabel(.next)
                SecureField("Password", text: $password)
                    .submitLabel(.go)
                    .onSubmit { submitCredentials() }
                Button("Cancel", role: .cancel) {
                    bridge.disconnect()
                }
                Button("OK") {
                    submitCredentials()
                }
            }
            .alert(
                "Verify Server",
                isPresented: Binding(
                    get: { bridge.pendingVerification != nil },
                    set: { if !$0 { bridge.pendingVerification = nil } }
                )
            ) {
                Button("Reject", role: .destructive) { bridge.verifyServer(accept: false) }
                Button("Accept") { bridge.verifyServer(accept: true) }
            } message: {
                Text(bridge.pendingVerification ?? "")
            }
            #if DEBUG
            .onAppear { autoConnectIfRequested() }
            .onChange(of: bridge.pendingCredentials) { _, needed in
                guard needed, let testPassword = ProcessInfo.processInfo.environment["VNC_TEST_PASSWORD"] else { return }
                bridge.provideCredentials(username: nil, password: testPassword)
            }
            #endif
        }
    }

    #if DEBUG
    /// Debug-only hook so `simctl launch` can drive a connection without
    /// UI automation tooling (set as `SIMCTL_CHILD_<NAME>` on the host
    /// shell running `simctl launch`, not as plain env vars or argv):
    /// `VNC_TEST_HOST`, `VNC_TEST_PORT`, and optionally `VNC_TEST_PASSWORD`
    /// to also auto-answer a credentials prompt.
    private func autoConnectIfRequested() {
        let env = ProcessInfo.processInfo.environment
        guard let testHost = env["VNC_TEST_HOST"] else { return }
        host = testHost
        if let testPort = env["VNC_TEST_PORT"] { portText = testPort }
        connect()
    }
    #endif

    private func connect() {
        let port = Int32(portText) ?? 5900
        bridge.connect(host: host, port: port)
    }

    private func submitCredentials() {
        bridge.provideCredentials(
            username: username.isEmpty ? nil : username,
            password: password)
    }
}
