package com.tigervnc.mobile.viewer

import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

/**
 * Foreground service held for as long as a VNC session is connected, so
 * the session survives the app going to the background.
 *
 * Without it, a few seconds after the user switches away Android moves
 * the process to the cached state, and Android 15+ cuts network access
 * for cached apps: tigervnc_core's socket read fails with ECONNABORTED
 * ("Software caused connection abort") and the session drops. A
 * foreground service keeps the process out of the cached state.
 *
 * It owns no session logic: [VncBridge] starts/stops it with the
 * connection and runs the background timeout itself; this only shows the
 * (required) notification, with the deadline while backgrounded and a
 * Disconnect action routed back through [onDisconnectRequested].
 */
class SessionKeepAliveService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_DISCONNECT) {
            onDisconnectRequested?.invoke()
            return START_NOT_STICKY
        }
        val label = intent?.getStringExtra(EXTRA_LABEL) ?: ""
        val deadline = intent?.getLongExtra(EXTRA_DEADLINE, 0L) ?: 0L
        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
        } else {
            0
        }
        ServiceCompat.startForeground(this, NOTIFICATION_ID, buildNotification(label, deadline), type)
        // The bridge restarts us explicitly on every connect; never
        // resurrect a notification for a session that no longer exists.
        return START_NOT_STICKY
    }

    private fun buildNotification(label: String, deadline: Long): android.app.Notification {
        ensureChannel(this)
        val openApp = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java)
                .setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val disconnect = PendingIntent.getService(
            this, 1,
            Intent(this, SessionKeepAliveService::class.java).setAction(ACTION_DISCONNECT),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(getString(R.string.session_notification_title, label))
            .setContentIntent(openApp)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
            .addAction(0, getString(R.string.session_notification_disconnect), disconnect)
        if (deadline > 0L) {
            // Live countdown ("Disconnects in 4:12") via the system's
            // chronometer, so nothing has to re-post the notification.
            builder.setContentText(getString(R.string.session_notification_backgrounded))
                .setWhen(deadline)
                .setShowWhen(true)
                .setUsesChronometer(true)
                .setChronometerCountDown(true)
        } else {
            builder.setContentText(getString(R.string.session_notification_active))
                .setShowWhen(false)
        }
        return builder.build()
    }

    companion object {
        private const val TAG = "SessionKeepAlive"
        private const val CHANNEL_ID = "vnc_session"
        private const val NOTIFICATION_ID = 1
        private const val EXTRA_LABEL = "label"
        private const val EXTRA_DEADLINE = "deadline"
        private const val ACTION_DISCONNECT = "com.tigervnc.mobile.viewer.DISCONNECT"

        /** Set by the [VncBridge] that owns the current session. */
        @Volatile
        var onDisconnectRequested: (() -> Unit)? = null

        private fun ensureChannel(context: Context) {
            NotificationManagerCompat.from(context).createNotificationChannel(
                NotificationChannelCompat.Builder(CHANNEL_ID, NotificationManagerCompat.IMPORTANCE_LOW)
                    .setName(context.getString(R.string.session_notification_channel))
                    .setShowBadge(false)
                    .build()
            )
        }

        /**
         * Starts the service or updates its notification. [deadline] is
         * the wall-clock time (ms) the background timeout fires, or 0
         * while the app is in the foreground. Returns false if Android
         * refused the start (e.g. connected while already backgrounded,
         * where foreground-service starts aren't allowed on Android 12+).
         */
        fun start(context: Context, label: String, deadline: Long = 0L): Boolean {
            val intent = Intent(context, SessionKeepAliveService::class.java)
                .putExtra(EXTRA_LABEL, label)
                .putExtra(EXTRA_DEADLINE, deadline)
            return try {
                ContextCompat.startForegroundService(context, intent)
                true
            } catch (e: IllegalStateException) {
                // ForegroundServiceStartNotAllowedException (API 31+) is
                // an IllegalStateException subclass.
                Log.w(TAG, "could not start keep-alive service", e)
                false
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, SessionKeepAliveService::class.java))
        }
    }
}
