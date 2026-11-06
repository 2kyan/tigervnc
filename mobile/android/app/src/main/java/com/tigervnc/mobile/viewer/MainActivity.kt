package com.tigervnc.mobile.viewer

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    // Edge-to-edge draws under the system bars; keep the
                    // app (and so the remote screen and the size it asks
                    // the server for) inside what's actually visible:
                    // status bar, navigation bar / tablet taskbar, the
                    // window caption bar in desktop (PC) mode, and any
                    // display cutout.
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .windowInsetsPadding(WindowInsets.systemBars.union(WindowInsets.displayCutout)),
                    ) {
                        TigerVncApp()
                    }
                }
            }
        }
    }
}

@Composable
fun TigerVncApp() {
    val context = LocalContext.current
    val bridge = remember { VncBridge(context.applicationContext) }
    DisposableEffect(Unit) {
        onDispose { bridge.destroy() }
    }

    // Tell the bridge when the app leaves/returns to the foreground, so a
    // connected session is kept alive (for a while) in the background.
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START -> bridge.setBackgrounded(false)
                Lifecycle.Event.ON_STOP -> bridge.setBackgrounded(true)
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    // Android 13+: the keep-alive notification (with its countdown and
    // Disconnect action) is hidden unless notifications are allowed. Ask
    // once, on the first successful connect; the session keeps working
    // either way.
    val notificationPermission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { }
    var askedNotificationPermission by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(bridge.state) {
        if (bridge.state == VncConnState.CONNECTED &&
            !askedNotificationPermission &&
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            askedNotificationPermission = true
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    if (bridge.state == VncConnState.CONNECTED) {
        SessionScreen(bridge)
    } else {
        ConnectScreen(bridge)
    }
}
