package com.deskconnect.app.ui

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.deskconnect.app.R
import com.deskconnect.app.SessionLog
import com.deskconnect.app.databinding.ActivityLogBinding

class LogActivity : AppCompatActivity() {
    private lateinit var binding: ActivityLogBinding

    private val logListener: (String) -> Unit = {
        runOnUiThread { renderLog() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityLogBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.toolbar.setNavigationOnClickListener { finish() }
        binding.clearLogButton.setOnClickListener {
            SessionLog.clear()
            renderLog()
        }
        renderLog()
    }

    override fun onStart() {
        super.onStart()
        SessionLog.addListener(logListener)
        renderLog()
    }

    override fun onStop() {
        SessionLog.removeListener(logListener)
        super.onStop()
    }

    private fun renderLog() {
        val text = SessionLog.snapshot().ifBlank { getString(R.string.log_empty) }
        binding.logText.text = text
        binding.logScroll.post {
            binding.logScroll.fullScroll(android.view.View.FOCUS_DOWN)
        }
    }

    companion object {
        fun open(from: AppCompatActivity) {
            from.startActivity(Intent(from, LogActivity::class.java))
        }
    }
}
