package com.deskconnect.app

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArrayList

/** In-memory ring buffer for connection logs (main UI no longer embeds the log panel). */
object SessionLog {
    private const val MAX_LINES = 400
    private val lines = ArrayDeque<String>()
    private val listeners = CopyOnWriteArrayList<(String) -> Unit>()
    private val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    @Synchronized
    fun append(message: String) {
        val stamped = "${timeFormat.format(Date())}  $message"
        while (lines.size >= MAX_LINES) {
            lines.removeFirst()
        }
        lines.addLast(stamped)
        listeners.forEach { it(stamped) }
    }

    @Synchronized
    fun snapshot(): String = lines.joinToString("\n")

    @Synchronized
    fun clear() {
        lines.clear()
        listeners.forEach { it("") }
    }

    fun addListener(listener: (String) -> Unit) {
        listeners.add(listener)
    }

    fun removeListener(listener: (String) -> Unit) {
        listeners.remove(listener)
    }
}
