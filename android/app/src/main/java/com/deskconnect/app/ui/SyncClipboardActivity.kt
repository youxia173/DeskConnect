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
 * Invisible activity used so we can read the clipboard (Android 10+ requires
 * input focus) without opening the main UI — from notification or background
 * clipboard-change auto launch.
 */
class SyncClipboardActivity : AppCompatActivity() {
    private val handler = Handler(Looper.getMainLooper())
    private var finished = false
    private var syncStarted = false

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

    private val focusFallback = Runnable {
        if (!syncStarted) startSync()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val filter = IntentFilter().apply {
            addAction(ConnectionService.ACTION_EVENT_CLIPBOARD_ACK)
            addAction(ConnectionService.ACTION_EVENT_CLIPBOARD_SYNC_RESULT)
        }
        ContextCompat.registerReceiver(this, events, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        // Wait briefly for window focus so clipboard read is allowed.
        handler.postDelayed(focusFallback, 400)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            handler.removeCallbacks(focusFallback)
            startSync()
        }
    }

    private fun startSync() {
        if (syncStarted || finished) return
        syncStarted = true
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
        handler.removeCallbacks(focusFallback)
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
        handler.removeCallbacks(focusFallback)
        val quiet = intent?.getBooleanExtra(EXTRA_QUIET, false) == true
        if (!quiet) {
            Toast.makeText(applicationContext, message, Toast.LENGTH_SHORT).show()
        }
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
        const val EXTRA_QUIET = "quiet"
        private const val ACK_TIMEOUT_MS = 8_000L

        fun start(context: Context, quiet: Boolean = false) {
            val intent = Intent(context, SyncClipboardActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                putExtra(EXTRA_QUIET, quiet)
            }
            context.startActivity(intent)
        }
    }
}
