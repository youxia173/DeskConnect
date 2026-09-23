package com.deskconnect.app.ui

import android.Manifest
import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.deskconnect.app.ConnectionService
import com.deskconnect.app.DeviceNameHelper
import com.deskconnect.app.PermissionGuide
import com.deskconnect.app.R
import com.deskconnect.app.SavedHost
import com.deskconnect.app.SavedHostStore
import com.deskconnect.app.SessionLog
import com.deskconnect.app.WifiNetworkHelper
import com.deskconnect.app.databinding.ActivityMainBinding
import com.deskconnect.app.databinding.ItemSavedHostBinding
import com.deskconnect.app.protocol.FingerprintUtil

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var clipboard: ClipboardManager
    private lateinit var hostStore: SavedHostStore
    private var connected = false
    private var sessionActive = false
    private var applyingRemoteClipboard = false
    private var currentWifiKey = SavedHostStore.WIFI_UNKNOWN
    private var lastWifiKeyForHosts: String? = null
    private var connectedPeerName: String = ""

    private val prefs by lazy { getSharedPreferences(PREFS, MODE_PRIVATE) }

    private val openDocuments = registerForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris ->
        if (uris.isNullOrEmpty()) return@registerForActivityResult
        ConnectionService.sendFiles(this, ArrayList(uris.map { it.toString() }))
    }

    private val requestLegacyStorage = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (!granted && Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            Toast.makeText(this, R.string.receive_folder_need_storage, Toast.LENGTH_LONG).show()
        }
    }

    private val requestNotificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) {
        refreshPermissionHint()
    }

    private val requestWifiPermission = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        refreshWifiAndHosts(applyForCurrentWifi = true)
        refreshPermissionHint()
    }

    private val events = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val payload = intent?.getStringExtra(ConnectionService.EXTRA_PAYLOAD).orEmpty()
            when (intent?.action) {
                ConnectionService.ACTION_EVENT_STATUS -> onStatus(payload)
                ConnectionService.ACTION_EVENT_LOG -> {
                    // Persisted via SessionLog in ConnectionService; no main-page panel.
                }
                ConnectionService.ACTION_EVENT_CLIPBOARD -> applyRemoteClipboard(payload)
                ConnectionService.ACTION_EVENT_FILE -> {
                    SessionLog.append("file received: $payload")
                    Toast.makeText(this@MainActivity, "已收到文件", Toast.LENGTH_SHORT).show()
                }
                ConnectionService.ACTION_EVENT_FINGERPRINT -> showTrustDialog(payload)
                ConnectionService.ACTION_EVENT_PEER_NAME -> {
                    connectedPeerName = payload
                    if (connected) {
                        binding.statusText.text = getString(R.string.status_connected_peer, payload)
                        setStatusDot(StatusTone.On)
                    }
                    refreshWifiAndHosts(applyForCurrentWifi = false)
                }
                ConnectionService.ACTION_EVENT_CLIPBOARD_SYNC_RESULT -> {
                    val parts = payload.split('|', limit = 2)
                    val msg = parts.getOrNull(1).orEmpty().ifBlank {
                        if (parts.getOrNull(0) == "ok") getString(R.string.clipboard_sync_success)
                        else getString(R.string.clipboard_sync_failed)
                    }
                    Toast.makeText(this@MainActivity, msg, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        clipboard = getSystemService(ClipboardManager::class.java)
        hostStore = SavedHostStore(this)

        binding.toolbar.inflateMenu(R.menu.menu_main)
        binding.toolbar.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.action_settings -> {
                    SettingsActivity.open(this)
                    true
                }
                R.id.action_logs -> {
                    LogActivity.open(this)
                    true
                }
                else -> false
            }
        }

        binding.hostInput.setText(prefs.getString(KEY_HOST, ""))
        binding.portInput.setText(prefs.getString(KEY_PORT, getString(R.string.default_port)))
        val defaultScreen = DeviceNameHelper.screenName(this)
        binding.screenInput.setText(
            prefs.getString(KEY_SCREEN, null)?.takeIf { it.isNotBlank() } ?: defaultScreen
        )

        if (ConnectionService.isSessionActive()) {
            sessionActive = true
        }
        lastWifiKeyForHosts = null
        refreshWifiAndHosts(applyForCurrentWifi = !sessionActive)
        setStatusDot(StatusTone.Off)

        binding.connectButton.setOnClickListener { connect() }
        binding.disconnectButton.setOnClickListener {
            val running = sessionActive || ConnectionService.isSessionActive()
            ConnectionService.disconnect(this)
            if (!running) {
                sessionActive = false
                refreshWifiAndHosts(applyForCurrentWifi = true)
            }
        }
        binding.sendFilesButton.setOnClickListener {
            openDocuments.launch(arrayOf("*/*"))
        }
        binding.syncClipboardButton.setOnClickListener {
            if (!connected) {
                Toast.makeText(this, R.string.status_disconnected, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            ConnectionService.syncClipboard(this)
            Toast.makeText(this, R.string.clipboard_syncing, Toast.LENGTH_SHORT).show()
        }
        binding.permissionsHint.setOnClickListener { SettingsActivity.open(this) }
        binding.wifiLabel.setOnClickListener { SettingsActivity.open(this) }

        maybeRequestNotificationPermission()
        maybeRequestWifiPermission()
        maybeRequestLegacyStoragePermission()
        if (PermissionGuide.missing(this).isNotEmpty() &&
            !prefs.getBoolean(KEY_PERM_GUIDE_SHOWN, false)
        ) {
            binding.root.post {
                SettingsActivity.open(this)
                prefs.edit().putBoolean(KEY_PERM_GUIDE_SHOWN, true).apply()
            }
        }
        refreshPermissionHint()
    }

    override fun onResume() {
        super.onResume()
        if (ConnectionService.isSessionActive()) {
            sessionActive = true
        }
        // Re-open (including returning from background) should follow the Wi-Fi
        // that is connected right now, unless a session is still up.
        refreshWifiAndHosts(applyForCurrentWifi = !sessionActive)
        refreshPermissionHint()
    }

    override fun onStart() {
        super.onStart()
        val filter = IntentFilter().apply {
            addAction(ConnectionService.ACTION_EVENT_STATUS)
            addAction(ConnectionService.ACTION_EVENT_LOG)
            addAction(ConnectionService.ACTION_EVENT_CLIPBOARD)
            addAction(ConnectionService.ACTION_EVENT_CLIPBOARD_SYNC_RESULT)
            addAction(ConnectionService.ACTION_EVENT_FILE)
            addAction(ConnectionService.ACTION_EVENT_FINGERPRINT)
            addAction(ConnectionService.ACTION_EVENT_PEER_NAME)
        }
        ContextCompat.registerReceiver(this, events, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        if (ConnectionService.isSessionActive()) {
            sessionActive = true
        }
        refreshWifiAndHosts(applyForCurrentWifi = !sessionActive)
    }

    override fun onStop() {
        unregisterReceiver(events)
        super.onStop()
    }

    private fun maybeRequestWifiPermission() {
        val needed = PermissionGuide.wifiRuntimePermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isNotEmpty()) {
            PermissionGuide.markRuntimeAsked(this, needed.toTypedArray())
            requestWifiPermission.launch(needed.toTypedArray())
        }
    }

    private fun refreshPermissionHint() {
        val missing = PermissionGuide.missing(this)
        if (missing.isEmpty()) {
            binding.permissionsHint.visibility = View.GONE
        } else {
            binding.permissionsHint.visibility = View.VISIBLE
            binding.permissionsHint.text =
                getString(R.string.perm_missing_banner) + "（${missing.size}）→"
        }
    }

    private fun refreshWifiAndHosts(applyForCurrentWifi: Boolean) {
        val previousWifi = lastWifiKeyForHosts
        currentWifiKey = WifiNetworkHelper.currentWifiKey(this)
        val wifiChanged = previousWifi != null && previousWifi != currentWifiKey
        lastWifiKeyForHosts = currentWifiKey
        val wifiName = WifiNetworkHelper.displayName(this)
        binding.wifiLabel.text = getString(R.string.wifi_label, wifiName)
        val hosts = hostStore.listForWifi(currentWifiKey)
        binding.savedHostsContainer.removeAllViews()
        if (hosts.isEmpty()) {
            binding.savedHostsEmpty.visibility = View.VISIBLE
        } else {
            binding.savedHostsEmpty.visibility = View.GONE
            val inflater = LayoutInflater.from(this)
            hosts.forEach { host ->
                val item = ItemSavedHostBinding.inflate(inflater, binding.savedHostsContainer, false)
                item.hostSelectButton.text = host.label()
                item.hostSelectButton.setOnClickListener {
                    applySavedHost(host)
                    Toast.makeText(this, getString(R.string.saved_host_applied, host.label()), Toast.LENGTH_SHORT).show()
                }
                item.hostDeleteButton.setOnClickListener {
                    confirmDeleteHost(host)
                }
                binding.savedHostsContainer.addView(item.root)
            }
            val identified = currentWifiKey != SavedHostStore.WIFI_UNKNOWN
            val shouldApply = identified && !sessionActive && (applyForCurrentWifi || wifiChanged)
            val latest = hosts.first()
            if (shouldApply && !formMatches(latest)) {
                applySavedHost(latest)
            }
        }
    }

    private fun formMatches(host: SavedHost): Boolean {
        val currentHost = binding.hostInput.text?.toString()?.trim().orEmpty()
        val currentPort = binding.portInput.text?.toString()?.toIntOrNull() ?: 24800
        return currentHost == host.host && currentPort == host.port
    }

    private fun applySavedHost(host: SavedHost) {
        binding.hostInput.setText(host.host)
        binding.portInput.setText(host.port.toString())
        binding.screenInput.setText(host.screen)
        prefs.edit().putBoolean(KEY_TLS, host.useTls).apply()
    }

    private fun confirmDeleteHost(host: SavedHost) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_saved_host)
            .setMessage(getString(R.string.delete_saved_host_message, host.label(), WifiNetworkHelper.displayName(this)))
            .setPositiveButton(R.string.delete) { _, _ ->
                hostStore.remove(currentWifiKey, host)
                refreshWifiAndHosts(applyForCurrentWifi = false)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun connect() {
        val host = binding.hostInput.text?.toString()?.trim().orEmpty()
        val port = binding.portInput.text?.toString()?.toIntOrNull() ?: 24800
        val screen = binding.screenInput.text?.toString()?.trim().orEmpty()
            .ifBlank { DeviceNameHelper.screenName(this) }
        val useTls = prefs.getBoolean(KEY_TLS, true)
        if (host.isEmpty()) {
            Toast.makeText(this, "请填写服务器 IP", Toast.LENGTH_SHORT).show()
            return
        }
        currentWifiKey = WifiNetworkHelper.currentWifiKey(this)
        prefs.edit()
            .putString(KEY_HOST, host)
            .putString(KEY_PORT, port.toString())
            .putString(KEY_SCREEN, screen)
            .putBoolean(KEY_TLS, useTls)
            .apply()
        binding.screenInput.setText(screen)
        val existingPeer = hostStore.listForWifi(currentWifiKey)
            .firstOrNull { it.host == host && it.port == port }
            ?.peerName
            .orEmpty()
        hostStore.remember(currentWifiKey, SavedHost(host, port, screen, useTls, peerName = existingPeer))
        refreshWifiAndHosts(applyForCurrentWifi = false)

        setUiConnecting()
        SessionLog.append(if (useTls) "connecting with TLS…" else "connecting plaintext…")
        ConnectionService.connect(
            this, host, port, screen, useTls, trustedFingerprints(), currentWifiKey
        )
    }

    private fun onStatus(payload: String) {
        val parts = payload.split('|')
        when (parts.firstOrNull()) {
            "connected" -> {
                connected = true
                sessionActive = true
                val peer = parts.getOrNull(4).orEmpty().ifBlank { connectedPeerName }
                if (peer.isNotBlank()) {
                    connectedPeerName = peer
                    binding.statusText.text = getString(R.string.status_connected_peer, peer)
                } else {
                    binding.statusText.text = getString(R.string.status_connected)
                }
                setStatusDot(StatusTone.On)
                binding.connectButton.isEnabled = false
                binding.disconnectButton.isEnabled = true
                binding.sendFilesButton.isEnabled = true
                binding.syncClipboardButton.isEnabled = true
                setInputsEnabled(false)
                refreshWifiAndHosts(applyForCurrentWifi = false)
                maybeAskOverlayPermission()
            }
            "connecting" -> {
                connected = false
                sessionActive = true
                val attempt = parts.getOrNull(2)?.toIntOrNull() ?: 0
                binding.statusText.text = if (attempt > 0) {
                    getString(R.string.status_reconnecting, attempt)
                } else {
                    getString(R.string.status_connecting)
                }
                setStatusDot(StatusTone.Connecting)
                binding.connectButton.isEnabled = false
                binding.disconnectButton.isEnabled = true
                binding.sendFilesButton.isEnabled = false
                binding.syncClipboardButton.isEnabled = false
                setInputsEnabled(false)
            }
            "reconnecting" -> {
                connected = false
                sessionActive = true
                val attempt = parts.getOrNull(2)?.toIntOrNull() ?: 0
                val delayMs = parts.getOrNull(3)?.toLongOrNull() ?: 0L
                binding.statusText.text = getString(
                    R.string.status_reconnecting_wait,
                    attempt,
                    (delayMs / 1000).coerceAtLeast(1)
                )
                setStatusDot(StatusTone.Connecting)
                binding.connectButton.isEnabled = false
                binding.disconnectButton.isEnabled = true
                binding.sendFilesButton.isEnabled = false
                binding.syncClipboardButton.isEnabled = false
                setInputsEnabled(false)
            }
            "disconnected" -> {
                connected = false
                val reason = parts.getOrNull(1).orEmpty()
                if (reason == "stopped" || !sessionActive) {
                    sessionActive = false
                    connectedPeerName = ""
                    binding.statusText.text = getString(R.string.status_disconnected)
                    setStatusDot(StatusTone.Off)
                    binding.connectButton.isEnabled = true
                    binding.disconnectButton.isEnabled = false
                    binding.sendFilesButton.isEnabled = false
                    binding.syncClipboardButton.isEnabled = false
                    setInputsEnabled(true)
                    refreshWifiAndHosts(applyForCurrentWifi = true)
                } else {
                    binding.statusText.text = getString(R.string.status_reconnecting, 0)
                    setStatusDot(StatusTone.Connecting)
                    binding.connectButton.isEnabled = false
                    binding.disconnectButton.isEnabled = true
                    binding.sendFilesButton.isEnabled = false
                    binding.syncClipboardButton.isEnabled = false
                    setInputsEnabled(false)
                }
                if (reason.isNotBlank() && reason != "untrusted fingerprint" && reason != "stopped") {
                    SessionLog.append("disconnected: $reason")
                }
            }
        }
    }

    private fun setInputsEnabled(enabled: Boolean) {
        binding.hostInput.isEnabled = enabled
        binding.portInput.isEnabled = enabled
        binding.screenInput.isEnabled = enabled
    }

    private fun setUiConnecting() {
        sessionActive = true
        binding.statusText.text = getString(R.string.status_connecting)
        setStatusDot(StatusTone.Connecting)
        binding.connectButton.isEnabled = false
        binding.disconnectButton.isEnabled = true
        setInputsEnabled(false)
    }

    private fun setStatusDot(tone: StatusTone) {
        val color = ContextCompat.getColor(
            this,
            when (tone) {
                StatusTone.Off -> R.color.dc_status_off
                StatusTone.Connecting -> R.color.dc_status_connecting
                StatusTone.On -> R.color.dc_status_on
            }
        )
        val bg = binding.statusDot.background
        if (bg is GradientDrawable) {
            bg.setColor(color)
        } else {
            binding.statusDot.backgroundTintList = ColorStateList.valueOf(color)
        }
    }

    private fun showTrustDialog(fingerprintHex: String) {
        sessionActive = false
        val pretty = FingerprintUtil.formatColon(fingerprintHex)
        AlertDialog.Builder(this)
            .setTitle(R.string.trust_fingerprint_title)
            .setMessage(getString(R.string.trust_fingerprint_message, pretty))
            .setPositiveButton(R.string.trust) { _, _ ->
                trustFingerprint(fingerprintHex)
                SessionLog.append("trusted server fingerprint, reconnecting…")
                connect()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun trustFingerprint(fingerprintHex: String) {
        val set = trustedFingerprints().toMutableSet()
        set.add(FingerprintUtil.normalize(fingerprintHex))
        prefs.edit().putStringSet(KEY_TRUSTED_FPS, set).apply()
    }

    private fun trustedFingerprints(): Set<String> =
        prefs.getStringSet(KEY_TRUSTED_FPS, emptySet())?.map { FingerprintUtil.normalize(it) }?.toSet()
            ?: emptySet()

    private fun applyRemoteClipboard(text: String) {
        applyingRemoteClipboard = true
        try {
            clipboard.setPrimaryClip(ClipData.newPlainText("DeskConnect", text))
        } finally {
            binding.root.postDelayed({ applyingRemoteClipboard = false }, 500)
        }
    }

    private fun maybeAskOverlayPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return
        if (ConnectionService.canDrawOverlays(this)) return
        if (prefs.getBoolean(KEY_OVERLAY_PROMPT_DONE, false)) return
        AlertDialog.Builder(this)
            .setTitle(R.string.overlay_permission_title)
            .setMessage(R.string.overlay_permission_message)
            .setPositiveButton(R.string.overlay_permission_open) { _, _ ->
                prefs.edit().putBoolean(KEY_OVERLAY_PROMPT_DONE, true).apply()
                try {
                    startActivity(
                        Intent(
                            Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                            Uri.parse("package:$packageName")
                        )
                    )
                } catch (_: Exception) {
                    Toast.makeText(this, R.string.overlay_permission_later, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.overlay_permission_later) { _, _ ->
                prefs.edit().putBoolean(KEY_OVERLAY_PROMPT_DONE, true).apply()
            }
            .show()
    }

    private fun maybeRequestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED
            ) {
                requestNotificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    private fun maybeRequestLegacyStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) return
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestLegacyStorage.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        }
    }

    private enum class StatusTone { Off, Connecting, On }

    companion object {
        private const val PREFS = "deskconnect"
        private const val KEY_HOST = "host"
        private const val KEY_PORT = "port"
        private const val KEY_SCREEN = "screen"
        private const val KEY_TLS = "tls"
        private const val KEY_TRUSTED_FPS = "trusted_server_fps"
        private const val KEY_OVERLAY_PROMPT_DONE = "overlay_prompt_done"
        private const val KEY_PERM_GUIDE_SHOWN = "perm_guide_shown"
    }
}
