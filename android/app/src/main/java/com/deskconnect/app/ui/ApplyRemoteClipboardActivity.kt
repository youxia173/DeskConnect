package com.deskconnect.app.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.deskconnect.app.R

/**
 * Brief focused activity so PC→phone clipboard writes succeed on Android 10+
 * (background [ClipboardManager.setPrimaryClip] is unreliable / blocked on many OEMs).
 */
class ApplyRemoteClipboardActivity : AppCompatActivity() {
    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val text = intent.getStringExtra(EXTRA_TEXT).orEmpty()
        if (text.isEmpty()) {
            finish()
            return
        }
        try {
            val clipboard = getSystemService(ClipboardManager::class.java)
            clipboard.setPrimaryClip(ClipData.newPlainText("DeskConnect", text))
            Toast.makeText(applicationContext, R.string.clipboard_received_from_pc, Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(
                applicationContext,
                getString(R.string.clipboard_apply_failed, e.message ?: ""),
                Toast.LENGTH_SHORT
            ).show()
        }
        handler.postDelayed({ finish() }, 250)
    }

    companion object {
        const val EXTRA_TEXT = "text"

        fun start(context: Context, text: String) {
            val intent = Intent(context, ApplyRemoteClipboardActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                putExtra(EXTRA_TEXT, text)
            }
            context.startActivity(intent)
        }
    }
}
