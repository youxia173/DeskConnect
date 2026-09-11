package com.deskconnect.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.widget.RemoteViews
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.deskconnect.app.protocol.BarrierClient
import com.deskconnect.app.protocol.BarrierClientListener
import com.deskconnect.app.protocol.ReceivedFileStore
import com.deskconnect.app.ui.ApplyRemoteClipboardActivity
import com.deskconnect.app.ui.MainActivity
import com.deskconnect.app.ui.SyncClipboardActivity
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.min

/**
 * Keeps the Barrier TCP session alive while the app is in the background,
 * with automatic reconnect after unexpected disconnects.
 */
class ConnectionService : Service(), BarrierClientListener, ReceivedFileStore {
    private val connectExecutor = Executors.newSingleThreadExecutor()
    private val workerExecutor = Executors.newCachedThreadPool()
    private val clientRef = AtomicReference<BarrierClient?>(null)
    private val mainHandler = Handler(Looper.getMainLooper())
    private val applyingRemoteClipboard = AtomicBoolean(false)
    private val awaitingClipboardAck = AtomicBoolean(false)
    private val userStop = AtomicBoolean(false)
    private val loopRunning = AtomicBoolean(false)
    private val reconnectWait = Object()
    private var clipboard: ClipboardManager? = null
    private var session: SessionConfig? = null
    private val hostStore by lazy { SavedHostStore(this) }

    @Volatile
    private var lastRemoteClipboard: String? = null
    @Volatile
    private var lastSentClipboard: String? = null
    @Volatile
    private var notificationState: NotifState = NotifState.Connecting

    private val lastAutoSyncLaunchMs = AtomicLong(0L)

    private val clipboardListener = ClipboardManager.OnPrimaryClipChangedListener {
        if (applyingRemoteClipboard.get()) return@OnPrimaryClipChangedListener
        if (clientRef.get() == null) return@OnPrimaryClipChangedListener
        mainHandler.post { onLocalClipboardChanged() }
    }

    private fun onLocalClipboardChanged() {
        if (applyingRemoteClipboard.get() || clientRef.get() == null) return
        val text = readClipboardText()
        if (!text.isNullOrEmpty()) {
            pushClipboardText(text, "listener")
            return
        }
        // Android 10+: background apps usually cannot read clipboard. Briefly take
        // focus via a transparent activity (same approach as PC→phone apply).
        launchBackgroundClipboardSync()
    }

    private fun launchBackgroundClipboardSync() {
        val now = SystemClock.elapsedRealtime()
        val prev = lastAutoSyncLaunchMs.get()
        if (now - prev < AUTO_SYNC_DEBOUNCE_MS) return
        if (!lastAutoSyncLaunchMs.compareAndSet(prev, now)) return
        try {
            SyncClipboardActivity.start(this, quiet = false)
            broadcast(ACTION_EVENT_LOG, "clipboard changed: auto sync via focus activity")
        } catch (e: Exception) {
            broadcast(ACTION_EVENT_LOG, "auto clipboard sync blocked: ${e.message}")
            showClipboardNudgeNotification()
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        clipboard = getSystemService(ClipboardManager::class.java)
        clipboard?.addPrimaryClipChangedListener(clipboardListener)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CONNECT -> {
                val host = intent.getStringExtra(EXTRA_HOST).orEmpty()
                val port = intent.getIntExtra(EXTRA_PORT, 24800)
                val screen = intent.getStringExtra(EXTRA_SCREEN).orEmpty().ifBlank { "Android" }
                val useTls = intent.getBooleanExtra(EXTRA_USE_TLS, true)
                val trusted = intent.getStringArrayListExtra(EXTRA_TRUSTED_FPS).orEmpty().toSet()
                val wifiKey = intent.getStringExtra(EXTRA_WIFI_KEY)
                    ?.ifBlank { null }
                    ?: WifiNetworkHelper.currentWifiKey(this)
                userStop.set(false)
                session = SessionConfig(host, port, screen, useTls, trusted, wifiKey)
                notificationState = NotifState.Connecting
                startForegroundCompat()
                startConnectLoop()
            }
            ACTION_DISCONNECT -> {
                userStop.set(true)
                wakeReconnectWait()
                clientRef.get()?.disconnect()
                // Loop will exit and stopSelf.
            }
            ACTION_SEND_CLIPBOARD -> {
                val text = intent.getStringExtra(EXTRA_TEXT).orEmpty()
                if (text.isNotEmpty()) {
                    lastSentClipboard = text
                    clientRef.get()?.sendClipboardText(text)
                } else {
                    mainHandler.post { pushLocalClipboardIfPossible("manual") }
                }
            }
            ACTION_SYNC_CLIPBOARD -> {
                mainHandler.post { pushLocalClipboardIfPossible("sync") }
            }
            ACTION_SYNC_CLIPBOARD_TEXT -> {
                val text = intent.getStringExtra(EXTRA_TEXT).orEmpty()
                mainHandler.post { pushClipboardText(text, "sync") }
            }
            ACTION_SEND_FILES -> {
                val uris = intent.getStringArrayListExtra(EXTRA_URIS).orEmpty()
                workerExecutor.execute { sendFilesFromUris(uris) }
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        userStop.set(true)
        wakeReconnectWait()
        clipboard?.removePrimaryClipChangedListener(clipboardListener)
        clientRef.getAndSet(null)?.disconnect()
        connectExecutor.shutdownNow()
        workerExecutor.shutdownNow()
        super.onDestroy()
    }

    private fun startConnectLoop() {
        if (!loopRunning.compareAndSet(false, true)) {
            // Already looping — bounce current client so the loop picks up new session.
            clientRef.get()?.disconnect()
            wakeReconnectWait()
            return
        }
        connectExecutor.execute {
            try {
                var attempt = 0
                while (!userStop.get()) {
                    val cfg = session ?: break
                    notificationState = if (attempt == 0) NotifState.Connecting else NotifState.Reconnecting(attempt)
                    updateNotification()
                    broadcast(ACTION_EVENT_STATUS, "connecting|${cfg.host}|$attempt")

                    clientRef.get()?.disconnect()
                    val client = BarrierClient(this, this)
                    clientRef.set(client)
                    client.connect(
                        host = cfg.host,
                        port = cfg.port,
                        screenName = cfg.screen,
                        useTls = cfg.useTls,
                        trustedServerFingerprints = cfg.trusted,
                        tlsIdentityDir = File(filesDir, "tls"),
                    )
                    clientRef.compareAndSet(client, null)

                    if (userStop.get()) break

                    attempt++
                    val delayMs = reconnectDelayMs(attempt)
                    notificationState = NotifState.Reconnecting(attempt)
                    updateNotification()
                    broadcast(
                        ACTION_EVENT_STATUS,
                        "reconnecting|${cfg.host}|$attempt|$delayMs"
                    )
                    broadcast(ACTION_EVENT_LOG, "auto-reconnect #$attempt in ${delayMs / 1000}s…")
                    waitForReconnect(delayMs)
                }
            } finally {
                loopRunning.set(false)
                notificationState = NotifState.Idle
                broadcast(ACTION_EVENT_STATUS, "disconnected|stopped")
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }
    }

    private fun reconnectDelayMs(attempt: Int): Long {
        // 2s, 4s, 8s, 16s, 32s, then cap at 60s
        val exp = min(attempt - 1, 5)
        return min(MAX_RECONNECT_DELAY_MS, BASE_RECONNECT_DELAY_MS shl exp)
    }

    private fun waitForReconnect(delayMs: Long) {
        val deadline = System.currentTimeMillis() + delayMs
        synchronized(reconnectWait) {
            while (!userStop.get()) {
                val left = deadline - System.currentTimeMillis()
                if (left <= 0) break
                try {
                    reconnectWait.wait(left)
                } catch (_: InterruptedException) {
                    break
                }
            }
        }
    }

    private fun wakeReconnectWait() {
        synchronized(reconnectWait) {
            reconnectWait.notifyAll()
        }
    }

    private fun sendFilesFromUris(uriStrings: List<String>) {
        val client = clientRef.get() ?: return
        val paths = ArrayList<Pair<String, java.io.InputStream>>()
        val sizes = ArrayList<Long>()
        try {
            for (uriString in uriStrings) {
                val uri = android.net.Uri.parse(uriString)
                val name = queryDisplayName(uri) ?: "file"
                val input = contentResolver.openInputStream(uri) ?: continue
                val size = querySize(uri)
                paths += name to input
                sizes += size
            }
            if (paths.isNotEmpty()) {
                client.sendFiles(paths, sizes)
            }
        } catch (e: Exception) {
            broadcast(ACTION_EVENT_LOG, "file send error: ${e.message}")
        }
    }

    private fun queryDisplayName(uri: android.net.Uri): String? {
        contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    return cursor.getString(0)
                }
            }
        return uri.lastPathSegment
    }

    private fun querySize(uri: android.net.Uri): Long {
        contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.SIZE), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst() && !cursor.isNull(0)) {
                    return cursor.getLong(0)
                }
            }
        return -1L
    }

    override fun open(fileName: String): Pair<String, OutputStream> {
        val safe = fileName.replace(Regex("[\\\\/:*?\"<>|]"), "_")
        val dir = File(getExternalFilesDir(android.os.Environment.DIRECTORY_DOWNLOADS), "DeskConnect")
        if (!dir.exists()) dir.mkdirs()
        var target = File(dir, safe)
        var i = 1
        while (target.exists()) {
            val dot = safe.lastIndexOf('.')
            target = if (dot > 0) {
                File(dir, "${safe.substring(0, dot)} ($i)${safe.substring(dot)}")
            } else {
                File(dir, "$safe ($i)")
            }
            i++
        }
        return target.absolutePath to FileOutputStream(target)
    }

    override fun onConnected(protocol: String, major: Int, minor: Int) {
        notificationState = NotifState.Connected
        updateNotification()
        session?.let { cfg ->
            hostStore.remember(
                cfg.wifiKey,
                SavedHost(cfg.host, cfg.port, cfg.screen, cfg.useTls)
            )
        }
        broadcast(ACTION_EVENT_STATUS, "connected|$protocol|$major|$minor")
    }

    override fun onDisconnected(reason: String?) {
        if (!userStop.get()) {
            notificationState = NotifState.Reconnecting(0)
            updateNotification()
        }
        broadcast(ACTION_EVENT_STATUS, "disconnected|${reason.orEmpty()}")
    }

    override fun onLog(message: String) {
        broadcast(ACTION_EVENT_LOG, message)
    }

    override fun onClipboardText(text: String) {
        lastRemoteClipboard = text
        broadcast(ACTION_EVENT_CLIPBOARD, text)
        applyingRemoteClipboard.set(true)
        mainHandler.post {
            // Writing the clipboard is allowed without window focus (only reading
            // is restricted on Android 10+), and starting an activity from a
            // background service is silently dropped — so write directly and keep
            // the focused activity only as a fallback for OEMs that refuse it.
            var applied = false
            try {
                clipboard?.setPrimaryClip(ClipData.newPlainText("DeskConnect", text))
                applied = true
            } catch (e: Exception) {
                broadcast(ACTION_EVENT_LOG, "apply remote clipboard failed: ${e.message}")
            }
            if (!applied) {
                try {
                    ApplyRemoteClipboardActivity.start(this, text)
                } catch (e: Exception) {
                    broadcast(ACTION_EVENT_LOG, "apply via focus activity failed: ${e.message}")
                }
            }
            mainHandler.postDelayed({ applyingRemoteClipboard.set(false) }, 1200)
        }
    }

    private fun pushLocalClipboardIfPossible(reason: String) {
        if (applyingRemoteClipboard.get()) return
        val text = readClipboardText()
        if (text.isNullOrEmpty()) {
            if (reason == "manual" || reason == "sync") {
                broadcast(ACTION_EVENT_LOG, "clipboard empty or not readable")
                broadcast(ACTION_EVENT_CLIPBOARD_SYNC_RESULT, "fail|${getString(R.string.clipboard_sync_empty)}")
            }
            return
        }
        pushClipboardText(text, reason)
    }

    private fun pushClipboardText(text: String, reason: String) {
        if (applyingRemoteClipboard.get()) return
        val client = clientRef.get()
        if (client == null) {
            if (reason == "manual" || reason == "sync") {
                broadcast(ACTION_EVENT_CLIPBOARD_SYNC_RESULT, "fail|${getString(R.string.status_disconnected)}")
            }
            return
        }
        if (text.isEmpty()) {
            if (reason == "manual" || reason == "sync") {
                broadcast(ACTION_EVENT_CLIPBOARD_SYNC_RESULT, "fail|${getString(R.string.clipboard_sync_empty)}")
            }
            return
        }
        // Manual/notification sync always sends so PC can ACK even if content matches.
        if (reason != "manual" && reason != "sync") {
            if (text == lastRemoteClipboard || text == lastSentClipboard) {
                return
            }
        }
        lastSentClipboard = text
        val ok = client.sendClipboardText(text)
        if (!ok) {
            lastSentClipboard = null
            if (reason == "manual" || reason == "sync") {
                broadcast(ACTION_EVENT_LOG, "clipboard sync failed")
                broadcast(ACTION_EVENT_CLIPBOARD_SYNC_RESULT, "fail|${getString(R.string.clipboard_sync_failed)}")
            }
            return
        }
        if (reason == "manual" || reason == "sync") {
            broadcast(ACTION_EVENT_LOG, "clipboard sent (${text.length} chars), waiting PC ack…")
            awaitingClipboardAck.set(true)
            mainHandler.removeCallbacks(ackTimeoutRunnable)
            mainHandler.postDelayed(ackTimeoutRunnable, ACK_TIMEOUT_MS)
        }
    }

    private val ackTimeoutRunnable = Runnable {
        if (awaitingClipboardAck.compareAndSet(true, false)) {
            broadcast(ACTION_EVENT_CLIPBOARD_SYNC_RESULT, "fail|${getString(R.string.clipboard_sync_no_ack)}")
            broadcast(ACTION_EVENT_LOG, "clipboard ack timeout (upgrade PC DeskConnect?)")
        }
    }

    override fun onClipboardAck(clipboardId: Int) {
        if (awaitingClipboardAck.compareAndSet(true, false)) {
            mainHandler.removeCallbacks(ackTimeoutRunnable)
            broadcast(ACTION_EVENT_CLIPBOARD_ACK, clipboardId.toString())
            broadcast(ACTION_EVENT_CLIPBOARD_SYNC_RESULT, "ok|${getString(R.string.clipboard_sync_success)}")
            broadcast(ACTION_EVENT_LOG, "clipboard sync success (PC ack)")
            showSyncSuccessNotification()
        }
    }

    private fun showSyncSuccessNotification() {
        ensureChannel()
        val manager = getSystemService(NotificationManager::class.java) ?: return
        val n = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.clipboard_sync_success))
            .setContentText(getString(R.string.clipboard_sync_success_detail))
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setTimeoutAfter(3_000)
            .setAutoCancel(true)
            .setOnlyAlertOnce(true)
            .build()
        manager.notify(SYNC_RESULT_NOTIFICATION_ID, n)
        // Keep the ongoing connection notification accurate.
        updateNotification()
    }

    private fun readClipboardText(): String? {
        return try {
            clipboard?.primaryClip
                ?.takeIf { it.itemCount > 0 }
                ?.getItemAt(0)
                ?.coerceToText(this)
                ?.toString()
        } catch (e: SecurityException) {
            broadcast(ACTION_EVENT_LOG, "clipboard access denied: ${e.message}")
            null
        } catch (e: Exception) {
            broadcast(ACTION_EVENT_LOG, "clipboard read failed: ${e.message}")
            null
        }
    }

    override fun onFileReceived(fileName: String, absolutePath: String) {
        broadcast(ACTION_EVENT_FILE, "$fileName|$absolutePath")
    }

    override fun onFileProgress(message: String) {
        broadcast(ACTION_EVENT_LOG, message)
    }

    override fun onUntrustedServerFingerprint(fingerprintHex: String) {
        // Stop auto-reconnect until user trusts; keep service so UI can reconnect.
        userStop.set(true)
        wakeReconnectWait()
        broadcast(ACTION_EVENT_FINGERPRINT, fingerprintHex)
    }

    private fun broadcast(action: String, payload: String) {
        sendBroadcast(
            Intent(action).setPackage(packageName).putExtra(EXTRA_PAYLOAD, payload)
        )
    }

    private fun startForegroundCompat() {
        ensureChannel()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, buildNotification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        } else {
            startForeground(NOTIFICATION_ID, buildNotification())
        }
    }

    private fun updateNotification() {
        val manager = getSystemService(NotificationManager::class.java) ?: return
        manager.notify(NOTIFICATION_ID, buildNotification())
    }

    private fun buildNotification(): Notification {
        val openApp = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val syncPending = PendingIntent.getActivity(
            this,
            1,
            Intent(this, SyncClipboardActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                .addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val host = session?.host.orEmpty()
        val (title, text) = when (val state = notificationState) {
            NotifState.Idle -> getString(R.string.notification_title) to getString(R.string.notification_text)
            NotifState.Connecting ->
                getString(R.string.notification_connecting_title) to
                    getString(R.string.notification_connecting_text, host)
            is NotifState.Reconnecting ->
                getString(R.string.notification_reconnecting_title) to
                    getString(R.string.notification_reconnecting_text, host, state.attempt)
            NotifState.Connected ->
                getString(R.string.notification_title) to
                    getString(R.string.notification_connected_text, host)
        }

        // Compact custom layout keeps the sync control on the far right so it is
        // not collapsed into the overflow action list under long status text.
        val content = RemoteViews(packageName, R.layout.notification_ongoing).apply {
            setTextViewText(R.id.notifTitle, title)
            setTextViewText(R.id.notifText, text)
            setOnClickPendingIntent(R.id.notifSyncButton, syncPending)
        }

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_sync_clipboard)
            .setContentIntent(openApp)
            .setCustomContentView(content)
            .setCustomBigContentView(content)
            .setStyle(NotificationCompat.DecoratedCustomViewStyle())
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    private fun showClipboardNudgeNotification() {
        ensureNudgeChannel()
        val syncPending = PendingIntent.getActivity(
            this,
            2,
            Intent(this, SyncClipboardActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val n = NotificationCompat.Builder(this, CHANNEL_NUDGE_ID)
            .setContentTitle(getString(R.string.clipboard_nudge_title))
            .setContentText(getString(R.string.clipboard_nudge_text))
            .setSmallIcon(R.drawable.ic_sync_clipboard)
            .setContentIntent(syncPending)
            .setAutoCancel(true)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setTimeoutAfter(8_000)
            .build()
        getSystemService(NotificationManager::class.java)?.notify(CLIPBOARD_NUDGE_NOTIFICATION_ID, n)
    }

    private fun ensureChannel() {
        val manager = getSystemService(NotificationManager::class.java) ?: return
        val channel = NotificationChannel(
            CHANNEL_ID,
            getString(R.string.notification_channel),
            NotificationManager.IMPORTANCE_LOW
        )
        manager.createNotificationChannel(channel)
    }

    private fun ensureNudgeChannel() {
        val manager = getSystemService(NotificationManager::class.java) ?: return
        val channel = NotificationChannel(
            CHANNEL_NUDGE_ID,
            getString(R.string.notification_nudge_channel),
            NotificationManager.IMPORTANCE_HIGH
        )
        manager.createNotificationChannel(channel)
    }

    private data class SessionConfig(
        val host: String,
        val port: Int,
        val screen: String,
        val useTls: Boolean,
        val trusted: Set<String>,
        val wifiKey: String,
    )

    private sealed class NotifState {
        data object Idle : NotifState()
        data object Connecting : NotifState()
        data object Connected : NotifState()
        data class Reconnecting(val attempt: Int) : NotifState()
    }

    companion object {
        const val ACTION_CONNECT = "com.deskconnect.app.CONNECT"
        const val ACTION_DISCONNECT = "com.deskconnect.app.DISCONNECT"
        const val ACTION_SEND_CLIPBOARD = "com.deskconnect.app.SEND_CLIPBOARD"
        const val ACTION_SYNC_CLIPBOARD = "com.deskconnect.app.SYNC_CLIPBOARD"
        const val ACTION_SYNC_CLIPBOARD_TEXT = "com.deskconnect.app.SYNC_CLIPBOARD_TEXT"
        const val ACTION_SEND_FILES = "com.deskconnect.app.SEND_FILES"

        const val ACTION_EVENT_STATUS = "com.deskconnect.app.EVENT_STATUS"
        const val ACTION_EVENT_LOG = "com.deskconnect.app.EVENT_LOG"
        const val ACTION_EVENT_CLIPBOARD = "com.deskconnect.app.EVENT_CLIPBOARD"
        const val ACTION_EVENT_CLIPBOARD_ACK = "com.deskconnect.app.EVENT_CLIPBOARD_ACK"
        const val ACTION_EVENT_CLIPBOARD_SYNC_RESULT = "com.deskconnect.app.EVENT_CLIPBOARD_SYNC_RESULT"
        const val ACTION_EVENT_FILE = "com.deskconnect.app.EVENT_FILE"
        const val ACTION_EVENT_FINGERPRINT = "com.deskconnect.app.EVENT_FINGERPRINT"

        const val EXTRA_HOST = "host"
        const val EXTRA_PORT = "port"
        const val EXTRA_SCREEN = "screen"
        const val EXTRA_USE_TLS = "useTls"
        const val EXTRA_TRUSTED_FPS = "trustedFps"
        const val EXTRA_WIFI_KEY = "wifiKey"
        const val EXTRA_TEXT = "text"
        const val EXTRA_URIS = "uris"
        const val EXTRA_PAYLOAD = "payload"

        private const val CHANNEL_ID = "deskconnect_connection"
        private const val CHANNEL_NUDGE_ID = "deskconnect_clipboard_nudge"
        private const val NOTIFICATION_ID = 42
        private const val SYNC_RESULT_NOTIFICATION_ID = 43
        private const val CLIPBOARD_NUDGE_NOTIFICATION_ID = 44
        private const val ACK_TIMEOUT_MS = 8_000L
        private const val AUTO_SYNC_DEBOUNCE_MS = 1_500L
        private const val BASE_RECONNECT_DELAY_MS = 2_000L
        private const val MAX_RECONNECT_DELAY_MS = 60_000L

        fun canDrawOverlays(context: Context): Boolean {
            return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                Settings.canDrawOverlays(context)
            } else {
                true
            }
        }

        fun connect(
            context: Context,
            host: String,
            port: Int,
            screen: String,
            useTls: Boolean,
            trustedFingerprints: Collection<String>,
            wifiKey: String = WifiNetworkHelper.currentWifiKey(context),
        ) {
            val intent = Intent(context, ConnectionService::class.java)
                .setAction(ACTION_CONNECT)
                .putExtra(EXTRA_HOST, host)
                .putExtra(EXTRA_PORT, port)
                .putExtra(EXTRA_SCREEN, screen)
                .putExtra(EXTRA_USE_TLS, useTls)
                .putExtra(EXTRA_WIFI_KEY, wifiKey)
                .putStringArrayListExtra(EXTRA_TRUSTED_FPS, ArrayList(trustedFingerprints))
            ContextCompat.startForegroundService(context, intent)
        }

        fun disconnect(context: Context) {
            context.startService(
                Intent(context, ConnectionService::class.java).setAction(ACTION_DISCONNECT)
            )
        }

        fun sendClipboard(context: Context, text: String) {
            context.startService(
                Intent(context, ConnectionService::class.java)
                    .setAction(ACTION_SEND_CLIPBOARD)
                    .putExtra(EXTRA_TEXT, text)
            )
        }

        fun syncClipboard(context: Context) {
            context.startService(
                Intent(context, ConnectionService::class.java).setAction(ACTION_SYNC_CLIPBOARD)
            )
        }

        fun syncClipboardText(context: Context, text: String) {
            context.startService(
                Intent(context, ConnectionService::class.java)
                    .setAction(ACTION_SYNC_CLIPBOARD_TEXT)
                    .putExtra(EXTRA_TEXT, text)
            )
        }

        fun sendFiles(context: Context, uris: ArrayList<String>) {
            context.startService(
                Intent(context, ConnectionService::class.java)
                    .setAction(ACTION_SEND_FILES)
                    .putStringArrayListExtra(EXTRA_URIS, uris)
            )
        }
    }
}
