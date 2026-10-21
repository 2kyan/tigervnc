package com.tigervnc.mobile.viewer

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusDirection
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

@Composable
fun ConnectScreen(bridge: VncBridge) {
    var host by remember { mutableStateOf(bridge.lastHost) }
    var port by remember { mutableStateOf(bridge.lastPort.toString()) }
    val focusManager = LocalFocusManager.current
    val connecting = bridge.state == VncConnState.CONNECTING
    val attemptConnect = { if (host.isNotBlank() && !connecting) bridge.connect(host, port.toIntOrNull() ?: 5900) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text("TigerVNC Viewer", style = MaterialTheme.typography.headlineMedium)
        androidx.compose.foundation.layout.Spacer(Modifier.padding(top = 24.dp))

        OutlinedTextField(
            value = host,
            onValueChange = { host = it },
            label = { Text("Host") },
            singleLine = true,
            keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = ImeAction.Next),
            keyboardActions = KeyboardActions(onNext = { focusManager.moveFocus(FocusDirection.Down) }),
            modifier = Modifier.fillMaxWidth(),
        )
        androidx.compose.foundation.layout.Spacer(Modifier.padding(top = 12.dp))
        OutlinedTextField(
            value = port,
            onValueChange = { port = it.filter(Char::isDigit) },
            label = { Text("Port") },
            singleLine = true,
            // Enter/Return here (physical keyboard or the soft keyboard's
            // action button) submits, same as tapping Connect below.
            keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                keyboardType = KeyboardType.Number,
                imeAction = ImeAction.Done,
            ),
            keyboardActions = KeyboardActions(onDone = { attemptConnect() }),
            modifier = Modifier.fillMaxWidth(),
        )
        androidx.compose.foundation.layout.Spacer(Modifier.padding(top = 20.dp))

        Button(
            onClick = attemptConnect,
            enabled = host.isNotBlank() && !connecting,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(if (connecting) "Connecting…" else "Connect")
        }

        bridge.errorMessage?.let { message ->
            androidx.compose.foundation.layout.Spacer(Modifier.padding(top = 16.dp))
            Text(message, color = MaterialTheme.colorScheme.error)
        }
    }

    if (bridge.credentialsNeeded) {
        CredentialsDialog(
            needUsername = bridge.credentialsNeedUsername,
            onSubmit = { username, password -> bridge.provideCredentials(username, password) },
            onCancel = { bridge.disconnect() },
        )
    }

    bridge.pendingVerifyFingerprint?.let { fingerprint ->
        AlertDialog(
            onDismissRequest = { bridge.verifyResponse(false) },
            title = { Text("Verify Server") },
            text = { Text("Certificate fingerprint:\n$fingerprint") },
            confirmButton = {
                TextButton(onClick = { bridge.verifyResponse(true) }) { Text("Accept") }
            },
            dismissButton = {
                TextButton(onClick = { bridge.verifyResponse(false) }) { Text("Reject") }
            },
        )
    }
}

@Composable
private fun CredentialsDialog(
    needUsername: Boolean,
    onSubmit: (username: String?, password: String) -> Unit,
    onCancel: () -> Unit,
) {
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    val focusManager = LocalFocusManager.current
    val submit = { onSubmit(if (needUsername) username else null, password) }

    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Credentials Required") },
        text = {
            Column {
                if (needUsername) {
                    OutlinedTextField(
                        value = username,
                        onValueChange = { username = it },
                        label = { Text("Username") },
                        singleLine = true,
                        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = ImeAction.Next),
                        keyboardActions = KeyboardActions(onNext = { focusManager.moveFocus(FocusDirection.Down) }),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    androidx.compose.foundation.layout.Spacer(Modifier.padding(top = 8.dp))
                }
                OutlinedTextField(
                    value = password,
                    onValueChange = { password = it },
                    label = { Text("Password") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    // Enter/Return here (physical keyboard or the soft
                    // keyboard's action button) submits, same as tapping OK.
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { submit() }),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = submit) {
                Text("OK")
            }
        },
        dismissButton = {
            TextButton(onClick = onCancel) { Text("Cancel") }
        },
    )
}
