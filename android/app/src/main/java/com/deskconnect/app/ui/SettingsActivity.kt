package com.deskconnect.app.ui

import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.LayoutInflater
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.documentfile.provider.DocumentFile
import com.deskconnect.app.PermissionGuide
import com.deskconnect.app.PermissionKind
import com.deskconnect.app.R
import com.deskconnect.app.ReceiveFolderStore
import com.deskconnect.app.databinding.ActivitySettingsBinding
import com.deskconnect.app.databinding.DialogPermissionGuideBinding
import com.deskconnect.app.databinding.ItemPermissionBinding
import com.deskconnect.app.protocol.FingerprintUtil
import com.deskconnect.app.protocol.TlsSupport
import java.io.File

class SettingsActivity : AppCompatActivity() {
    private lateinit var binding: ActivitySettingsBinding
    private lateinit var receiveFolderStore: ReceiveFolderStore
    private val prefs by lazy { getSharedPreferences(PREFS, MODE_PRIVATE) }

    private val pickReceiveFolder = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        if (uri == null) return@registerForActivityResult
        val label = DocumentFile.fromTreeUri(this, uri)?.name
            ?: getString(R.string.receive_folder_change)
        receiveFolderStore.setTreeUri(uri, label)
        refreshReceiveFolderUi()
        Toast.makeText(
            this,
            getString(R.string.receive_folder_changed, receiveFolderStore.displayPath()),
            Toast.LENGTH_SHORT,
        ).show()
    }

    private val requestGuidePermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        refreshPermissionButton()
        if (PermissionGuide.missing(this).isNotEmpty()) {
            binding.root.post { showPermissionGuide() }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)
        receiveFolderStore = ReceiveFolderStore(this)

        binding.toolbar.setNavigationOnClickListener { finish() }
        binding.tlsSwitch.isChecked = prefs.getBoolean(KEY_TLS, true)
        binding.tlsSwitch.setOnCheckedChangeListener { _, checked ->
            prefs.edit().putBoolean(KEY_TLS, checked).apply()
            refreshClientFingerprintLabel()
        }
        binding.changeReceiveFolderButton.setOnClickListener { pickReceiveFolder.launch(null) }
        binding.resetReceiveFolderButton.setOnClickListener {
            receiveFolderStore.clearCustom()
            refreshReceiveFolderUi()
            Toast.makeText(this, R.string.receive_folder_reset_done, Toast.LENGTH_SHORT).show()
        }
        binding.permissionsButton.setOnClickListener { showPermissionGuide() }

        refreshClientFingerprintLabel()
        refreshReceiveFolderUi()
        refreshPermissionButton()
    }

    override fun onResume() {
        super.onResume()
        refreshReceiveFolderUi()
        refreshPermissionButton()
        refreshClientFingerprintLabel()
    }

    private fun refreshClientFingerprintLabel() {
        if (!binding.tlsSwitch.isChecked) {
            binding.clientFingerprintText.text = getString(R.string.tls_disabled_hint)
            return
        }
        try {
            val identity = TlsSupport.ensureClientIdentity(File(filesDir, "tls"))
            binding.clientFingerprintText.text =
                getString(R.string.client_fingerprint, FingerprintUtil.formatColon(identity.fingerprintHex))
        } catch (e: Exception) {
            binding.clientFingerprintText.text =
                getString(R.string.client_fingerprint, e.message ?: "?")
        }
    }

    private fun refreshReceiveFolderUi() {
        val path = receiveFolderStore.displayPath()
        binding.receiveFolderText.text = if (receiveFolderStore.isCustom()) {
            path
        } else {
            getString(R.string.receive_folder_default_hint) + "\n" + path
        }
        binding.resetReceiveFolderButton.isEnabled = receiveFolderStore.isCustom()
    }

    private fun refreshPermissionButton() {
        val missing = PermissionGuide.missing(this)
        binding.permissionsButton.text = if (missing.isEmpty()) {
            getString(R.string.perm_guide_button)
        } else {
            getString(R.string.perm_missing_banner) + "（${missing.size}）"
        }
    }

    private fun showPermissionGuide() {
        val dialogBinding = DialogPermissionGuideBinding.inflate(layoutInflater)
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.perm_guide_title)
            .setView(dialogBinding.root)
            .setPositiveButton(R.string.perm_open_app_settings) { _, _ ->
                PermissionGuide.openAppDetails(this)
            }
            .setNegativeButton(R.string.cancel, null)
            .create()

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
        dialog.setOnDismissListener { refreshPermissionButton() }
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

    companion object {
        private const val PREFS = "deskconnect"
        private const val KEY_TLS = "tls"

        fun open(from: AppCompatActivity) {
            from.startActivity(Intent(from, SettingsActivity::class.java))
        }
    }
}
