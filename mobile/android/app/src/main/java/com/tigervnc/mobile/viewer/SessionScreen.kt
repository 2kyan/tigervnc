package com.tigervnc.mobile.viewer

import android.view.KeyCharacterMap
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.PointerType
import androidx.compose.ui.input.pointer.isPrimaryPressed
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.isTertiaryPressed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt
import kotlinx.coroutines.delay

/**
 * Renders the live decoded framebuffer aspect-fit in the available space
 * and maps touch to VNC pointer events. Mirrors ios/App/RemoteScreenView.swift's
 * aspect-fit coordinate mapping (recomputed from the view's own live size
 * on every event, so this tolerates any window/multi-window resize with
 * no fixed-frame assumptions).
 *
 * Also mirrors RemoteScreenView's "remote resize": whenever this
 * screen's own pixel size changes (e.g. a device rotation, or entering
 * split-screen/multi-window), it asks the server to resize its virtual
 * desktop to match, debounced the same 350ms so a live drag/rotation
 * animation settles to one request instead of flooding the server.
 *
 * `onSizeChanged` reports raw physical pixels. RFB's SetDesktopSize has
 * no DPI field, so requesting those raw pixels on a high-density display
 * makes the server render its desktop UI at its own default DPI
 * assumption but at that huge pixel count -- remote text/controls end up
 * physically tiny even though the frame itself fills the screen sharply.
 * [VncBridge.useReadableResize] (user-toggleable, persisted) picks
 * between converting to dp (readable, the default -- matches how a
 * Retina Mac's scaled "looks like" display modes work) or requesting raw
 * pixels (sharper, smaller remote UI). Either way this view's own
 * aspect-fit Canvas draw below needs no change, since it already
 * upscales whatever framebuffer resolution arrives to fill the view.
 */
@Composable
fun SessionScreen(bridge: VncBridge) {
    var viewSize by remember { mutableStateOf(IntSize.Zero) }
    val density = LocalDensity.current
    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(Unit) { focusRequester.requestFocus() }
    // Session menu: opened by the floating handle, F8, or Back. Back
    // never leaves/disconnects directly -- Disconnect is a menu item --
    // so a stray Back gesture can't drop the session.
    var menuOpen by remember { mutableStateOf(false) }
    BackHandler { menuOpen = !menuOpen }
    LaunchedEffect(viewSize, bridge.useReadableResize) {
        if (viewSize.width > 0 && viewSize.height > 0) {
            delay(350)
            val (w, h) = if (bridge.useReadableResize) {
                (viewSize.width / density.density).roundToInt() to
                    (viewSize.height / density.density).roundToInt()
            } else {
                viewSize.width to viewSize.height
            }
            bridge.requestResize(w, h)
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .onSizeChanged { viewSize = it }
            .focusRequester(focusRequester)
            .focusable()
            .onKeyEvent { event ->
                // Physical/Bluetooth keyboard input (a soft/on-screen
                // keyboard isn't wired up on Android yet -- see DESIGN.md).
                if (event.type != KeyEventType.KeyDown && event.type != KeyEventType.KeyUp) {
                    return@onKeyEvent false
                }
                // F8 opens the session menu (same menu key as TigerVNC's
                // desktop vncviewer) and is not sent to the server.
                if (event.nativeKeyEvent.keyCode == android.view.KeyEvent.KEYCODE_F8) {
                    if (event.type == KeyEventType.KeyDown && event.nativeKeyEvent.repeatCount == 0) {
                        menuOpen = !menuOpen
                    }
                    return@onKeyEvent true
                }
                val keysym = androidKeyEventToKeysym(event.nativeKeyEvent) ?: return@onKeyEvent false
                bridge.sendKeyEvent(keysym, event.type == KeyEventType.KeyDown)
                true
            },
    ) {
        val bitmap = bridge.frameBitmap
        Box(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(bitmap?.width, bitmap?.height) {
                    val bmp = bitmap ?: return@pointerInput
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull() ?: continue
                            // Re-claim keyboard focus on tap, in case a
                            // toggle/disconnect button click stole it.
                            if (event.type == PointerEventType.Press) focusRequester.requestFocus()
                            val fit = aspectFit(size.width.toFloat(), size.height.toFloat(), bmp.width, bmp.height)
                            val (fx, fy) = mapToFramebuffer(change.position, fit, bmp.width, bmp.height)
                            // Send the buttons actually held, not "anything
                            // but a lift": mouse/trackpad/stylus hover moves,
                            // Enter/Exit and wheel events have no button
                            // down and must not be sent as a left press.
                            val held = vncButtonMask(event, change)
                            if (event.type == PointerEventType.Scroll) {
                                // Compose's scrollDelta.y > 0 is wheel-down.
                                val dy = change.scrollDelta.y
                                val wheel = when {
                                    dy < 0f -> VncButtons.WHEEL_UP
                                    dy > 0f -> VncButtons.WHEEL_DOWN
                                    else -> 0
                                }
                                if (wheel != 0) {
                                    bridge.sendPointerEvent(fx, fy, held or wheel)
                                    bridge.sendPointerEvent(fx, fy, held)
                                }
                                change.consume()
                                continue
                            }
                            bridge.sendPointerEvent(fx, fy, held)
                        }
                    }
                },
        ) {
            if (bitmap != null) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val fit = aspectFit(size.width, size.height, bitmap.width, bitmap.height)
                    drawImage(
                        image = bitmap.asImageBitmap(),
                        dstOffset = IntOffset(fit.offsetX.toInt(), fit.offsetY.toInt()),
                        dstSize = IntSize(fit.width.toInt(), fit.height.toInt()),
                    )
                }
            } else {
                Text(
                    "Waiting for first frame…",
                    modifier = Modifier.align(Alignment.Center),
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }
        }

        SessionMenu(
            bridge = bridge,
            containerSize = viewSize,
            menuOpen = menuOpen,
            onMenuOpenChange = { menuOpen = it },
        )
    }
    // Hand keyboard focus back to the remote screen once the menu closes.
    LaunchedEffect(menuOpen) { if (!menuOpen) focusRequester.requestFocus() }
}

private val MENU_HANDLE_SIZE = 36.dp
private val MENU_HANDLE_MARGIN = 8.dp

/**
 * The session menu (resolution mode, handle visibility, disconnect)
 * and its small floating handle.
 *
 * The handle is the only on-screen control, so it covers as little of
 * the remote desktop as possible: it's small and translucent, it can be
 * dragged anywhere (position persisted via [VncBridge.menuHandleX]/[Y]),
 * and it can be hidden entirely -- the menu stays reachable via F8 or
 * Back (handled in [SessionScreen]). Pointer input outside the handle
 * goes straight to the remote screen beneath it.
 */
@Composable
private fun SessionMenu(
    bridge: VncBridge,
    containerSize: IntSize,
    menuOpen: Boolean,
    onMenuOpenChange: (Boolean) -> Unit,
) {
    val density = LocalDensity.current
    val handlePx = with(density) { MENU_HANDLE_SIZE.toPx() }
    val marginPx = with(density) { MENU_HANDLE_MARGIN.toPx() }
    val freeW = (containerSize.width - handlePx - 2 * marginPx).coerceAtLeast(0f)
    val freeH = (containerSize.height - handlePx - 2 * marginPx).coerceAtLeast(0f)
    val savedPos = Offset(marginPx + bridge.menuHandleX * freeW, marginPx + bridge.menuHandleY * freeH)
    val currentSavedPos by rememberUpdatedState(savedPos)
    // Live position while a drag is in progress; null otherwise.
    var dragPos by remember { mutableStateOf<Offset?>(null) }
    val pos = dragPos ?: savedPos

    Box(
        modifier = Modifier
            .offset { IntOffset(pos.x.roundToInt(), pos.y.roundToInt()) }
            .size(MENU_HANDLE_SIZE),
    ) {
        if (!bridge.menuHandleHidden) {
            Surface(
                shape = CircleShape,
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.6f),
                modifier = Modifier
                    .fillMaxSize()
                    .pointerInput(freeW, freeH) {
                        detectDragGestures(
                            onDragStart = { dragPos = currentSavedPos },
                            onDrag = { change, amount ->
                                change.consume()
                                val p = (dragPos ?: currentSavedPos) + amount
                                dragPos = Offset(
                                    p.x.coerceIn(marginPx, marginPx + freeW),
                                    p.y.coerceIn(marginPx, marginPx + freeH),
                                )
                            },
                            onDragEnd = {
                                dragPos?.let {
                                    bridge.updateMenuHandlePosition(
                                        if (freeW > 0f) (it.x - marginPx) / freeW else 1f,
                                        if (freeH > 0f) (it.y - marginPx) / freeH else 0f,
                                    )
                                }
                                dragPos = null
                            },
                            onDragCancel = { dragPos = null },
                        )
                    }
                    .pointerInput(Unit) { detectTapGestures { onMenuOpenChange(true) } },
            ) {
                Box(contentAlignment = Alignment.Center, modifier = Modifier.fillMaxSize()) {
                    Text("⋮", style = MaterialTheme.typography.titleMedium)
                }
            }
        }

        // Anchored to the handle's spot even while it's hidden, so F8/Back
        // open the menu in the same place.
        DropdownMenu(expanded = menuOpen, onDismissRequest = { onMenuOpenChange(false) }) {
            // The open menu (a focusable popup) receives all key input;
            // make Escape and F8 close it, like vncviewer's F8 menu,
            // instead of the keys silently going nowhere.
            val menuFocus = remember { FocusRequester() }
            LaunchedEffect(Unit) { menuFocus.requestFocus() }
            Column(
                modifier = Modifier
                    .focusRequester(menuFocus)
                    .focusable()
                    .onKeyEvent { event ->
                        val code = event.nativeKeyEvent.keyCode
                        if (code != android.view.KeyEvent.KEYCODE_ESCAPE &&
                            code != android.view.KeyEvent.KEYCODE_F8
                        ) {
                            return@onKeyEvent false
                        }
                        if (event.type == KeyEventType.KeyDown) onMenuOpenChange(false)
                        true
                    },
            ) {
                DropdownMenuItem(
                    // Readable: dp-based remote size, legible remote UI.
                    // Sharp: raw pixels, crisper but smaller remote UI.
                    text = { Text(if (bridge.useReadableResize) "Resolution: Readable" else "Resolution: Sharp (1:1)") },
                    onClick = {
                        bridge.updateUseReadableResize(!bridge.useReadableResize)
                        onMenuOpenChange(false)
                    },
                )
                DropdownMenuItem(
                    text = { Text(if (bridge.menuHandleHidden) "Show menu button" else "Hide menu button") },
                    onClick = {
                        bridge.updateMenuHandleHidden(!bridge.menuHandleHidden)
                        onMenuOpenChange(false)
                    },
                )
                HorizontalDivider()
                DropdownMenuItem(
                    text = { Text("Disconnect", color = MaterialTheme.colorScheme.error) },
                    onClick = {
                        onMenuOpenChange(false)
                        bridge.disconnect()
                    },
                )
                Text(
                    "Open this menu: F8 or Back",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                )
            }
        }
    }
}

/**
 * VNC button mask for [change]: a mouse reports its real buttons
 * (primary/secondary/tertiary -> LEFT/RIGHT/MIDDLE); touch and stylus
 * contact is a left press only while actually down, so stylus hover
 * stays a plain move.
 */
private fun vncButtonMask(event: PointerEvent, change: PointerInputChange): Int {
    if (change.type == PointerType.Mouse) {
        var mask = 0
        if (event.buttons.isPrimaryPressed) mask = mask or VncButtons.LEFT
        if (event.buttons.isSecondaryPressed) mask = mask or VncButtons.RIGHT
        if (event.buttons.isTertiaryPressed) mask = mask or VncButtons.MIDDLE
        return mask
    }
    return if (change.pressed) VncButtons.LEFT else 0
}

private data class AspectFit(val offsetX: Float, val offsetY: Float, val width: Float, val height: Float)

private fun aspectFit(viewWidth: Float, viewHeight: Float, srcWidth: Int, srcHeight: Int): AspectFit {
    if (viewWidth <= 0f || viewHeight <= 0f || srcWidth <= 0 || srcHeight <= 0) {
        return AspectFit(0f, 0f, viewWidth, viewHeight)
    }
    val scale = minOf(viewWidth / srcWidth, viewHeight / srcHeight)
    val width = srcWidth * scale
    val height = srcHeight * scale
    return AspectFit((viewWidth - width) / 2f, (viewHeight - height) / 2f, width, height)
}

private fun mapToFramebuffer(position: Offset, fit: AspectFit, srcWidth: Int, srcHeight: Int): Pair<Int, Int> {
    if (fit.width <= 0f || fit.height <= 0f) return 0 to 0
    val nx = ((position.x - fit.offsetX) / fit.width).coerceIn(0f, 1f)
    val ny = ((position.y - fit.offsetY) / fit.height).coerceIn(0f, 1f)
    return (nx * srcWidth).toInt() to (ny * srcHeight).toInt()
}

/**
 * Maps a physical-keyboard [android.view.KeyEvent] to an X11 keysym (see
 * tigervnc_core.h's vnc_client_send_key_event() -- keysym is an X11
 * keysym per keysymdef.h conventions), or null if the key isn't mapped.
 *
 * Non-printable/control keys use fixed keysym values from X11's
 * keysymdef.h (stable across X11 versions, part of the core protocol
 * spec) -- checked first since these keyCodes report unicodeChar == 0.
 * Printable characters use unicodeChar directly: X11 keysyms for
 * Latin-1 (0x20-0xFF) match the Unicode code point exactly, and beyond
 * that X11's "Unicode keysym" convention is 0x01000000 + code point.
 */
private fun androidKeyEventToKeysym(event: android.view.KeyEvent): Int? {
    when (event.keyCode) {
        android.view.KeyEvent.KEYCODE_DEL -> return 0xFF08 // BackSpace
        android.view.KeyEvent.KEYCODE_FORWARD_DEL -> return 0xFFFF // Delete
        android.view.KeyEvent.KEYCODE_TAB -> return 0xFF09
        android.view.KeyEvent.KEYCODE_ENTER,
        android.view.KeyEvent.KEYCODE_NUMPAD_ENTER -> return 0xFF0D
        android.view.KeyEvent.KEYCODE_ESCAPE -> return 0xFF1B
        android.view.KeyEvent.KEYCODE_DPAD_LEFT -> return 0xFF51
        android.view.KeyEvent.KEYCODE_DPAD_UP -> return 0xFF52
        android.view.KeyEvent.KEYCODE_DPAD_RIGHT -> return 0xFF53
        android.view.KeyEvent.KEYCODE_DPAD_DOWN -> return 0xFF54
        android.view.KeyEvent.KEYCODE_MOVE_HOME -> return 0xFF50
        android.view.KeyEvent.KEYCODE_MOVE_END -> return 0xFF57
        android.view.KeyEvent.KEYCODE_PAGE_UP -> return 0xFF55
        android.view.KeyEvent.KEYCODE_PAGE_DOWN -> return 0xFF56
        android.view.KeyEvent.KEYCODE_INSERT -> return 0xFF63
        android.view.KeyEvent.KEYCODE_CAPS_LOCK -> return 0xFFE5
        android.view.KeyEvent.KEYCODE_SHIFT_LEFT -> return 0xFFE1
        android.view.KeyEvent.KEYCODE_SHIFT_RIGHT -> return 0xFFE2
        android.view.KeyEvent.KEYCODE_CTRL_LEFT -> return 0xFFE3
        android.view.KeyEvent.KEYCODE_CTRL_RIGHT -> return 0xFFE4
        android.view.KeyEvent.KEYCODE_ALT_LEFT -> return 0xFFE9
        android.view.KeyEvent.KEYCODE_ALT_RIGHT -> return 0xFFEA
        android.view.KeyEvent.KEYCODE_META_LEFT -> return 0xFFEB
        android.view.KeyEvent.KEYCODE_META_RIGHT -> return 0xFFEC
        android.view.KeyEvent.KEYCODE_F1 -> return 0xFFBE
        android.view.KeyEvent.KEYCODE_F2 -> return 0xFFBF
        android.view.KeyEvent.KEYCODE_F3 -> return 0xFFC0
        android.view.KeyEvent.KEYCODE_F4 -> return 0xFFC1
        android.view.KeyEvent.KEYCODE_F5 -> return 0xFFC2
        android.view.KeyEvent.KEYCODE_F6 -> return 0xFFC3
        android.view.KeyEvent.KEYCODE_F7 -> return 0xFFC4
        android.view.KeyEvent.KEYCODE_F8 -> return 0xFFC5
        android.view.KeyEvent.KEYCODE_F9 -> return 0xFFC6
        android.view.KeyEvent.KEYCODE_F10 -> return 0xFFC7
        android.view.KeyEvent.KEYCODE_F11 -> return 0xFFC8
        android.view.KeyEvent.KEYCODE_F12 -> return 0xFFC9
    }

    val unicodeChar = event.unicodeChar
    // High bit set means this is a dead/accent key composing with the
    // next keystroke (e.g. a "^" key before typing "e" to get "ê") --
    // not supported yet, so skip it rather than send the wrong char.
    if (unicodeChar and KeyCharacterMap.COMBINING_ACCENT != 0) return null
    if (unicodeChar <= 0) return null
    return if (unicodeChar <= 0xFF) unicodeChar else 0x01000000 + unicodeChar
}
