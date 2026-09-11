package com.deskconnect.app.ui

import android.content.BroadcastReceiver
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.deskconnect.app.ConnectionService
import com.deskconnect.app.R

/**
 * Invisible activity used by the notification action so we can read the
 * clipboard (Android 10+ requires focus) without opening the main UI.
 */
class SyncClipboardActivity : AppCompatActivity() {
    private val handler = Handler(Looper.getMainLooper())
    private var finished = false

    private val events = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            when (intent?.action) {
                ConnectionService.ACTION_EVENT_CLIPBOARD_ACK -> {
                    finishWithToast(getString(R.string.clipboard_sync_success))
                }
                ConnectionService.ACTION_EVENT_CLIPBOARD_SYNC_RESULT -> {
                    val payload = intent.getStringExtra(ConnectionService.EXTRA_PAYLOAD).orEmpty()
                    val parts = payload.split('|', limit = 2)
                    val ok = parts.getOrNull(0) == "ok"
                    val msg = parts.getOrNull(1).orEmpty()
                        .ifBlank {
                            if (ok) getString(R.string.clipboard_sync_success)
                            else getString(R.string.clipboard_sync_failed)
                        }
                    finishWithToast(msg)
                }
            }
        }
    }

    private val timeout = Runnable {
        finishWithToast(getString(R.string.clipboard_sync_no_ack))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val filter = IntentFilter().apply {
            addAction(ConnectionService.ACTION_EVENT_CLIPBOARD_ACK)
            addAction(ConnectionService.ACTION_EVENT_CLIPBOARD_SYNC_RESULT)
        }
        ContextCompat.registerReceiver(this, events, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

        val text = readClipboardText()
        if (text.isNullOrEmpty()) {
            finishWithToast(getString(R.string.clipboard_sync_empty))
            return
        }
        ConnectionService.syncClipboardText(this, text)
        handler.postDelayed(timeout, ACK_TIMEOUT_MS)
    }

    override fun onDestroy() {
        handler.removeCallbacks(timeout)
        try {
            unregisterReceiver(events)
        } catch (_: Exception) {
        }
        super.onDestroy()
    }

    private fun finishWithToast(message: String) {
        if (finished) return
        finished = true
        handler.removeCallbacks(timeout)
        Toast.makeText(applicationContext, message, Toast.LENGTH_SHORT).show()
        finish()
    }

    private fun readClipboardText(): String? {
        return try {
            val clipboard = getSystemService(ClipboardManager::class.java)
            clipboard.primaryClip
                ?.takeIf { it.itemCount > 0 }
                ?.getItemAt(0)
                ?.coerceToText(this)
                ?.toString()
                ?.takeIf { it.isNotEmpty() }
        } catch (_: Exception) {
            null
        }
    }

    companion object {
        private const val ACK_TIMEOUT_MS = 8_000L
    }
}
