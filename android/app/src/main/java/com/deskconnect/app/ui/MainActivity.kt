package com.deskconnect.app.ui

import android.Manifest
import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
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
import com.deskconnect.app.PermissionGuide
import com.deskconnect.app.PermissionKind
import com.deskconnect.app.R
import com.deskconnect.app.SavedHost
import com.deskconnect.app.SavedHostStore
import com.deskconnect.app.WifiNetworkHelper
import com.deskconnect.app.databinding.ActivityMainBinding
import com.deskconnect.app.databinding.DialogPermissionGuideBinding
import com.deskconnect.app.databinding.ItemPermissionBinding
import com.deskconnect.app.databinding.ItemSavedHostBinding
import com.deskconnect.app.protocol.FingerprintUtil
import com.deskconnect.app.protocol.TlsSupport
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var clipboard: ClipboardManager
    private lateinit var hostStore: SavedHostStore
    private var connected = false
    private var sessionActive = false
    private var applyingRemoteClipboard = false
    private var currentWifiKey = SavedHostStore.WIFI_UNKNOWN

    private val prefs by lazy { getSharedPreferences(PREFS, MODE_PRIVATE) }

    private val openDocuments = registerForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris ->
        if (uris.isNullOrEmpty()) return@registerForActivityResult
        ConnectionService.sendFiles(this, ArrayList(uris.map { it.toString() }))
    }

    private val requestNotificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) {
        refreshPermissionUi()
    }

    private val requestWifiPermission = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        refreshWifiAndHosts(selectLatest = true)
        refreshPermissionUi()
    }

    private val requestGuidePermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        refreshWifiAndHosts(selectLatest = false)
        refreshPermissionUi()
        // If still missing after dialog, reopen guide so user can jump to Settings.
        if (PermissionGuide.missing(this).isNotEmpty()) {
            binding.root.post { showPermissionGuide(force = true) }
        }
    }

    private val events = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val payload = intent?.getStringExtra(ConnectionService.EXTRA_PAYLOAD).orEmpty()
            when (intent?.action) {
                ConnectionService.ACTION_EVENT_STATUS -> onStatus(payload)
                ConnectionService.ACTION_EVENT_LOG -> appendLog(payload)
                ConnectionService.ACTION_EVENT_CLIPBOARD -> applyRemoteClipboard(payload)
                ConnectionService.ACTION_EVENT_FILE -> {
                    appendLog("file received: $payload")
                    Toast.makeText(this@MainActivity, "已收到文件", Toast.LENGTH_SHORT).show()
                }
                ConnectionService.ACTION_EVENT_FINGERPRINT -> showTrustDialog(payload)
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

        binding.hostInput.setText(prefs.getString(KEY_HOST, ""))
        binding.portInput.setText(prefs.getString(KEY_PORT, getString(R.string.default_port)))
        binding.screenInput.setText(prefs.getString(KEY_SCREEN, getString(R.string.default_screen)))
        binding.tlsSwitch.isChecked = prefs.getBoolean(KEY_TLS, true)

        refreshClientFingerprintLabel()
        refreshWifiAndHosts(selectLatest = binding.hostInput.text.isNullOrBlank())

        binding.connectButton.setOnClickListener { connect() }
        binding.disconnectButton.setOnClickListener {
            ConnectionService.disconnect(this)
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
        binding.permissionsButton.setOnClickListener { showPermissionGuide(force = true) }
        binding.wifiLabel.setOnClickListener { showPermissionGuide(force = true) }

        maybeRequestNotificationPermission()
        maybeRequestWifiPermission()
        if (PermissionGuide.missing(this).isNotEmpty() &&
            !prefs.getBoolean(KEY_PERM_GUIDE_SHOWN, false)
        ) {
            binding.root.post {
                showPermissionGuide(force = true)
                prefs.edit().putBoolean(KEY_PERM_GUIDE_SHOWN, true).apply()
            }
        }
        refreshPermissionUi()
    }

    override fun onResume() {
        super.onResume()
        refreshWifiAndHosts(selectLatest = false)
        refreshPermissionUi()
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
        }
        ContextCompat.registerReceiver(this, events, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        refreshWifiAndHosts(selectLatest = false)
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

    private fun refreshPermissionUi() {
        val missing = PermissionGuide.missing(this)
        if (missing.isEmpty()) {
            binding.permissionsButton.text = getString(R.string.perm_guide_button)
        } else {
            binding.permissionsButton.text =
                getString(R.string.perm_missing_banner) + "（${missing.size}）"
        }
    }

    private fun showPermissionGuide(force: Boolean) {
        if (!force && PermissionGuide.missing(this).isEmpty()) return
        val dialogBinding = DialogPermissionGuideBinding.inflate(layoutInflater)
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.perm_guide_title)
            .setView(dialogBinding.root)
            .setPositiveButton(R.string.perm_open_app_settings) { _, _ ->
                PermissionGuide.openAppDetails(this)
            }
            .setNegativeButton(R.string.cancel, null)
            .create()

        fun bindList() {
            dialogBinding.permissionList.removeAllViews()
            val inflater = LayoutInflater.from(this)
            PermissionGuide.items(this).forEach { item ->
                val row = ItemPermissionBinding.inflate(inflater, dialogBinding.permissionList, false)
                row.permTitle.setText(item.titleRes)
                row.permReason.setText(item.reasonRes)
                if (item.granted) {
                    row.permStatus.setText(R.string.perm_status_granted)
                    row.permAction.setText(R.string.perm_action_done)
                    row.permAction.isEnabled = false
                } else {
                    row.permStatus.setText(R.string.perm_status_missing)
                    row.permAction.setText(R.string.perm_action_grant)
                    row.permAction.isEnabled = true
                    row.permAction.setOnClickListener {
                        dialog.dismiss()
                        requestPermissionItem(item.kind)
                    }
                }
                dialogBinding.permissionList.addView(row.root)
            }
        }
        bindList()
        dialog.setOnDismissListener { refreshPermissionUi() }
        dialog.show()
    }

    private fun requestPermissionItem(kind: PermissionKind) {
        when (kind) {
            PermissionKind.Notifications -> {
                val runtime = PermissionGuide.notificationRuntimePermissions()
                if (runtime.isNotEmpty() &&
                    ContextCompat.checkSelfPermission(this, runtime[0]) != PackageManager.PERMISSION_GRANTED
                ) {
                    if (PermissionGuide.shouldOpenSettingsForRuntime(this, runtime[0])) {
                        PermissionGuide.openNotificationSettings(this)
                    } else {
                        PermissionGuide.markRuntimeAsked(this, runtime)
                        requestGuidePermissions.launch(runtime)
                    }
                } else if (!PermissionGuide.hasNotifications(this)) {
                    PermissionGuide.openNotificationSettings(this)
                }
            }
            PermissionKind.WifiSsid -> {
                val runtime = PermissionGuide.wifiRuntimePermissions()
                val first = runtime.firstOrNull() ?: return
                if (ContextCompat.checkSelfPermission(this, first) == PackageManager.PERMISSION_GRANTED) {
                    return
                }
                if (PermissionGuide.shouldOpenSettingsForRuntime(this, first)) {
                    PermissionGuide.openAppDetails(this)
                } else {
                    PermissionGuide.markRuntimeAsked(this, runtime)
                    requestGuidePermissions.launch(runtime)
                }
            }
            PermissionKind.LocationServices -> PermissionGuide.openLocationServicesSettings(this)
            PermissionKind.Overlay -> PermissionGuide.openOverlaySettings(this)
        }
    }

    private fun refreshWifiAndHosts(selectLatest: Boolean) {
        currentWifiKey = WifiNetworkHelper.currentWifiKey(this)
        val wifiName = WifiNetworkHelper.displayName(this)
        binding.wifiLabel.text = getString(R.string.wifi_label, wifiName)
        if (!PermissionGuide.hasWifiSsidPermission(this) ||
            (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU && !PermissionGuide.isLocationEnabled(this))
        ) {
            binding.wifiLabel.append("\n")
            binding.wifiLabel.append(getString(R.string.perm_missing_banner))
        }
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
            if (selectLatest) {
                applySavedHost(hosts.first())
            }
        }
    }

    private fun applySavedHost(host: SavedHost) {
        binding.hostInput.setText(host.host)
        binding.portInput.setText(host.port.toString())
        binding.screenInput.setText(host.screen)
        binding.tlsSwitch.isChecked = host.useTls
    }

    private fun confirmDeleteHost(host: SavedHost) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_saved_host)
            .setMessage(getString(R.string.delete_saved_host_message, host.label(), WifiNetworkHelper.displayName(this)))
            .setPositiveButton(R.string.delete) { _, _ ->
                hostStore.remove(currentWifiKey, host)
                refreshWifiAndHosts(selectLatest = false)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun connect() {
        val host = binding.hostInput.text?.toString()?.trim().orEmpty()
        val port = binding.portInput.text?.toString()?.toIntOrNull() ?: 24800
        val screen = binding.screenInput.text?.toString()?.trim().orEmpty().ifBlank { "Android" }
        val useTls = binding.tlsSwitch.isChecked
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
        // Optimistic remember; service also remembers on successful handshake.
        hostStore.remember(currentWifiKey, SavedHost(host, port, screen, useTls))
        refreshWifiAndHosts(selectLatest = false)

        setUiConnecting()
        appendLog(if (useTls) "connecting with TLS…" else "connecting plaintext…")
        if (useTls) {
            refreshClientFingerprintLabel()
        }
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
                binding.statusText.text = getString(R.string.status_connected)
                binding.connectButton.isEnabled = false
                binding.disconnectButton.isEnabled = true
                binding.sendFilesButton.isEnabled = true
                binding.syncClipboardButton.isEnabled = true
                setInputsEnabled(false)
                refreshWifiAndHosts(selectLatest = false)
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
                    binding.statusText.text = getString(R.string.status_disconnected)
                    binding.connectButton.isEnabled = true
                    binding.disconnectButton.isEnabled = false
                    binding.sendFilesButton.isEnabled = false
                    binding.syncClipboardButton.isEnabled = false
                    setInputsEnabled(true)
                } else {
                    // Transient drop; auto-reconnect will continue.
                    binding.statusText.text = getString(R.string.status_reconnecting, 0)
                    binding.connectButton.isEnabled = false
                    binding.disconnectButton.isEnabled = true
                    binding.sendFilesButton.isEnabled = false
                    binding.syncClipboardButton.isEnabled = false
                    setInputsEnabled(false)
                }
                if (reason.isNotBlank() && reason != "untrusted fingerprint" && reason != "stopped") {
                    appendLog("disconnected: $reason")
                }
            }
        }
    }

    private fun setInputsEnabled(enabled: Boolean) {
        binding.hostInput.isEnabled = enabled
        binding.portInput.isEnabled = enabled
        binding.screenInput.isEnabled = enabled
        binding.tlsSwitch.isEnabled = enabled
    }

    private fun setUiConnecting() {
        sessionActive = true
        binding.statusText.text = getString(R.string.status_connecting)
        binding.connectButton.isEnabled = false
        binding.disconnectButton.isEnabled = true
        setInputsEnabled(false)
    }

    private fun showTrustDialog(fingerprintHex: String) {
        sessionActive = false
        val pretty = FingerprintUtil.formatColon(fingerprintHex)
        AlertDialog.Builder(this)
            .setTitle(R.string.trust_fingerprint_title)
            .setMessage(getString(R.string.trust_fingerprint_message, pretty))
            .setPositiveButton(R.string.trust) { _, _ ->
                trustFingerprint(fingerprintHex)
                appendLog("trusted server fingerprint, reconnecting…")
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

    private fun refreshClientFingerprintLabel() {
        try {
            val identity = TlsSupport.ensureClientIdentity(File(filesDir, "tls"))
            binding.clientFingerprintText.text =
                getString(R.string.client_fingerprint, FingerprintUtil.formatColon(identity.fingerprintHex))
        } catch (e: Exception) {
            binding.clientFingerprintText.text = getString(R.string.client_fingerprint, e.message ?: "?")
        }
    }

    private fun applyRemoteClipboard(text: String) {
        applyingRemoteClipboard = true
        try {
            clipboard.setPrimaryClip(ClipData.newPlainText("DeskConnect", text))
        } finally {
            binding.root.postDelayed({ applyingRemoteClipboard = false }, 500)
        }
    }

    private fun appendLog(line: String) {
        val existing = binding.logText.text?.toString().orEmpty()
        val next = if (existing.isEmpty()) line else "$existing\n$line"
        binding.logText.text = next.lines().takeLast(200).joinToString("\n")
        binding.logScroll.post {
            binding.logScroll.fullScroll(android.view.View.FOCUS_DOWN)
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
