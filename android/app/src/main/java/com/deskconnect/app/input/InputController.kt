package com.deskconnect.app.input

import android.content.Context
import android.content.Intent
import android.graphics.Point
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Display
import android.view.KeyEvent
import android.view.WindowManager
import com.deskconnect.app.SessionLog
import com.deskconnect.app.protocol.RemoteInputEvent
import com.deskconnect.app.protocol.RemoteScreenInfo
import org.json.JSONObject
import java.io.File
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.SecureRandom
import java.util.ArrayDeque
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicBoolean

/** Device-local authenticated bridge. Network callbacks never wait for injection. */
class InputController private constructor(private val context: Context) {
    private val prefs = context.getSharedPreferences("input-control", Context.MODE_PRIVATE)
    private val handler = Handler(Looper.getMainLooper())
    private val cursor = InputCursor(context) { reason -> failControl(reason) }
    private val queue = ArrayDeque<JSONObject>()
    private val queueLock = Object()
    private val started = AtomicBoolean(false)
    private val listeners = CopyOnWriteArrayList<(String) -> Unit>()
    private val geometryListeners = CopyOnWriteArrayList<() -> Unit>()
    @Volatile private var ready = false
    @Volatile private var active = false
    @Volatile private var helperSocket: Socket? = null
    @Volatile private var message = "未启用"
    @Volatile private var width = 1080
    @Volatile private var height = 1920
    private var x = 0
    private var y = 0
    private val heldKeys = mutableMapOf<Int, Int>()
    private val repeatCounts = mutableMapOf<Int, Int>()
    private val configFile = File(context.filesDir, "input-bridge.json")

    init {
        createConfig()
        updateGeometry()
        context.getSystemService(DisplayManager::class.java).registerDisplayListener(object : DisplayManager.DisplayListener {
            override fun onDisplayAdded(id: Int) = updateGeometry()
            override fun onDisplayRemoved(id: Int) = updateGeometry()
            override fun onDisplayChanged(id: Int) {
                if (id == Display.DEFAULT_DISPLAY) {
                    val previousWidth = width
                    val previousHeight = height
                    updateGeometry()
                    if (previousWidth != width || previousHeight != height) {
                        if (active) failControl("手机屏幕方向已改变，已退出控制") else release()
                        geometryListeners.forEach { it() }
                    }
                }
            }
        }, handler)
        start()
    }

    @Synchronized private fun createConfig() {
        if (configFile.exists()) {
            try {
                val existing = JSONObject(configFile.readText())
                if (existing.getString("token").length >= 64 && existing.getInt("port") in 1024..65535) return
            } catch (_: Exception) { }
        }
        val bytes = ByteArray(32).also { SecureRandom().nextBytes(it) }
        val token = bytes.joinToString("") { "%02x".format(it.toInt() and 255) }
        val port = ServerSocket(0, 1, java.net.InetAddress.getByName("127.0.0.1")).use { it.localPort }
        val config = JSONObject().put("port", port).put("token", token)
        configFile.writeText(config.toString())
    }

    @Suppress("DEPRECATION")
    private fun updateGeometry() {
        val point = Point()
        context.getSystemService(WindowManager::class.java).defaultDisplay.getRealSize(point)
        width = point.x.coerceIn(1, 32767)
        height = point.y.coerceIn(1, 32767)
    }

    @Synchronized fun screenInfo() = RemoteScreenInfo(width, height)
    fun addFailureListener(listener: (String) -> Unit) { listeners.add(listener) }
    fun removeFailureListener(listener: (String) -> Unit) { listeners.remove(listener) }
    fun addGeometryListener(listener: () -> Unit) { geometryListeners.add(listener) }
    fun removeGeometryListener(listener: () -> Unit) { geometryListeners.remove(listener) }

    @Synchronized fun release() {
        active = false
        heldKeys.clear()
        repeatCounts.clear()
        cursor.hide()
        synchronized(queueLock) {
            queue.clear()
            if (ready) queue.addLast(JSONObject().put("type", "release"))
            queueLock.notifyAll()
        }
    }

    private fun failControl(reason: String) {
        release()
        publishStatus(reason)
        listeners.forEach { it(reason) }
    }

    @Synchronized fun accept(event: RemoteInputEvent) {
        if (event is RemoteInputEvent.Leave) { release(); return }
        if (event is RemoteInputEvent.Enter) {
            if (!prefs.getBoolean("enabled", false) || !ready) {
                failControl("键鼠未就绪，请开启增强模式并从电脑激活")
                return
            }
            release()
            active = true
            x = event.x.coerceIn(0, width - 1)
            y = event.y.coerceIn(0, height - 1)
            cursor.show(x, y)
            enqueue(JSONObject().put("type", "enter").put("x", x).put("y", y))
            return
        }
        if (!active || !ready) return
        when (event) {
            is RemoteInputEvent.Move -> {
                x = (if (event.relative) x + event.x else event.x).coerceIn(0, width - 1)
                y = (if (event.relative) y + event.y else event.y).coerceIn(0, height - 1)
                cursor.move(x, y)
                enqueue(JSONObject().put("type", "move").put("x", x).put("y", y))
            }
            is RemoteInputEvent.Button -> if (event.button in 1..3) {
                enqueue(JSONObject().put("type", "button")
                    .put("button", event.button).put("down", event.down).put("x", x).put("y", y))
            }
            is RemoteInputEvent.Wheel -> enqueue(JSONObject().put("type", "wheel")
                .put("dx", (event.dx / 120.0).coerceIn(-100.0, 100.0))
                .put("dy", (event.dy / 120.0).coerceIn(-100.0, 100.0)).put("x", x).put("y", y))
            is RemoteInputEvent.Key -> {
                // Emergency disconnect is handled locally even if an injected key fails.
                if (event.down && event.keyId == 0xef1b && event.modifiers and 0x6 == 0x6) {
                    failControl("已通过 Ctrl+Alt+Esc 退出控制")
                    return
                }
                val code = if (event.down) DeskflowKeyMapper.keyCode(event.keyId)
                    else heldKeys.remove(event.button) ?: DeskflowKeyMapper.keyCode(event.keyId)
                if (code == null || code == KeyEvent.KEYCODE_UNKNOWN) {
                    if (event.down && event.modifiers and 0x1e == 0) {
                        DeskflowKeyMapper.unicodeText(event.keyId)?.let { text ->
                            repeat(event.repeat.coerceIn(1, 64)) {
                                enqueue(JSONObject().put("type", "text").put("text", text))
                            }
                        }
                    }
                    return
                }
                if (event.down) heldKeys[event.button] = code
                if (event.repeat == 0) repeatCounts.remove(event.button)
                repeat(event.repeat.coerceIn(1, 64)) {
                    val count = if (event.repeat > 0) ((repeatCounts[event.button] ?: 0) + 1).coerceAtMost(1000) else 0
                    if (event.repeat > 0) repeatCounts[event.button] = count
                    enqueue(JSONObject().put("type", "key").put("keyCode", code).put("down", event.down)
                        .put("metaState", DeskflowKeyMapper.metaState(event.modifiers)).put("repeat", count))
                }
            }
            else -> Unit
        }
    }

    private fun enqueue(command: JSONObject) {
        var overflow = false
        synchronized(queueLock) {
            // Only replace consecutive moves: never cross a button/key boundary.
            if (command.optString("type") == "move" && queue.peekLast()?.optString("type") == "move") queue.removeLast()
            if (queue.size >= 256) {
                overflow = true
                queue.clear()
                queue.addLast(JSONObject().put("type", "release"))
            } else queue.addLast(command)
            queueLock.notifyAll()
        }
        if (overflow) failControl("输入队列拥塞，已退出控制")
    }

    private fun publishStatus(value: String) {
        if (message == value) return
        message = value
        SessionLog.append("键鼠控制: $value")
        Log.i("DeskConnectInput", value)
        context.sendBroadcast(Intent(ACTION_STATUS).setPackage(context.packageName))
    }

    private fun start() {
        if (!started.compareAndSet(false, true)) return
        Thread({
            while (true) {
                if (!prefs.getBoolean("enabled", false)) {
                    publishStatus("未启用")
                    Thread.sleep(500)
                    continue
                }
                try {
                    val config = JSONObject(configFile.readText())
                    Socket().use { socket ->
                        helperSocket = socket
                        socket.tcpNoDelay = true
                        socket.connect(InetSocketAddress("127.0.0.1", config.getInt("port")), 1000)
                        socket.soTimeout = 4000
                        val output = socket.getOutputStream().bufferedWriter(Charsets.UTF_8)
                        val input = socket.getInputStream().bufferedReader(Charsets.UTF_8)
                        output.write(JSONObject().put("type", "auth").put("token", config.getString("token")).toString())
                        output.newLine(); output.flush()
                        val greeting = JSONObject(input.readLine() ?: error("helper disconnected"))
                        check(greeting.optString("type") == "ready") { greeting.optString("message", "authentication failed") }
                        ready = true
                        publishStatus("ADB 增强模式已就绪")
                        val reader = Thread({
                            try {
                                while (!socket.isClosed) {
                                    val line = input.readLine() ?: break
                                    val response = JSONObject(line)
                                    if (response.optString("type") == "warning" && response.optString("code") == "unsupported_text") {
                                        publishStatus("当前字符无法直接输入，请使用手机输入法")
                                        continue
                                    }
                                    if (response.optString("type") == "error") {
                                        val detail = response.optString("code", response.optString("message", "输入失败"))
                                        publishStatus("输入被系统拒绝: $detail")
                                        failControl(when (detail) {
                                            "injection_permission_denied" -> "系统未授予输入权限，请在开发者选项开启 USB 调试（安全设置）"
                                            "screen_locked" -> "手机已锁屏，请解锁后重新控制"
                                            else -> "输入失败（$detail），已退出控制"
                                        })
                                        break
                                    }
                                }
                            } catch (_: Exception) { }
                            finally { try { socket.close() } catch (_: Exception) { } }
                        }, "deskconnect-input-replies")
                        reader.isDaemon = true
                        reader.start()
                        try {
                            var lastPing = android.os.SystemClock.elapsedRealtime()
                            while (prefs.getBoolean("enabled", false) && !socket.isClosed) {
                                val now = android.os.SystemClock.elapsedRealtime()
                                if (now - lastPing >= 1000) {
                                    output.write("{\"type\":\"ping\"}"); output.newLine(); output.flush()
                                    lastPing = now
                                }
                                val command = synchronized(queueLock) {
                                    if (queue.isEmpty()) queueLock.wait(400)
                                    if (queue.isEmpty()) JSONObject().put("type", "ping") else queue.removeFirst()
                                }
                                output.write(command.toString()); output.newLine(); output.flush()
                            }
                        } finally {
                            ready = false
                            if (active) failControl("输入辅助进程断开，已退出控制") else release()
                            socket.close()
                        }
                    }
                } catch (_: Exception) {
                    ready = false
                    if (active) failControl("输入辅助进程断开，已退出控制")
                    publishStatus(if (prefs.getBoolean("enabled", false)) "等待电脑激活 ADB 增强模式" else "未启用")
                } finally { helperSocket = null }
                Thread.sleep(1000)
            }
        }, "deskconnect-input-bridge").apply { isDaemon = true; start() }
    }

    companion object {
        const val ACTION_STATUS = "com.deskconnect.app.INPUT_STATUS"
        @Volatile private var instance: InputController? = null
        @Synchronized fun get(context: Context): InputController = instance ?: InputController(context.applicationContext).also { instance = it }
        fun ensureConfig(context: Context) { get(context) }
        fun setEnabled(context: Context, enabled: Boolean) {
            val controller = get(context)
            controller.prefs.edit().putBoolean("enabled", enabled).apply()
            if (!enabled) {
                controller.failControl("已关闭键鼠控制")
                try { controller.helperSocket?.close() } catch (_: Exception) { }
            }
        }
        fun isEnabled(context: Context): Boolean = get(context).prefs.getBoolean("enabled", false)
        fun restartBridge(context: Context) {
            val controller = get(context)
            controller.failControl("正在无线恢复键鼠控制")
            try { controller.helperSocket?.close() } catch (_: Exception) { }
        }
        fun isWirelessRecoveryEnabled(context: Context): Boolean = get(context).prefs.getBoolean("wirelessRecovery", false)
        @Synchronized fun beginRecoveryRegistration(context: Context, serverKey: String): String {
            // Keep the lease during reconnect/activation. Rotating on every TCP
            // reconnect would cancel the very worker restoring the input helper.
            // Choosing a different server invalidates the old server's lease.
            val prefs = get(context).prefs
            val previous = prefs.getString("recoveryId", null)
            if (prefs.getString("recoveryServer", null) == serverKey &&
                previous != null && previous.matches(Regex("[0-9a-f]{32}"))) return previous
            val id = java.util.UUID.randomUUID().toString().replace("-", "")
            prefs.edit().putString("recoveryId", id).putString("recoveryServer", serverKey).apply()
            return id
        }
        fun setWirelessRecoveryEnabled(context: Context, enabled: Boolean) {
            get(context).prefs.edit().putBoolean("wirelessRecovery", enabled).apply()
        }
        fun setResumeConnection(context: Context, enabled: Boolean) {
            get(context).prefs.edit().putBoolean("resumeConnection", enabled).apply()
        }
        fun shouldResumeConnection(context: Context): Boolean = get(context).prefs.getBoolean("resumeConnection", true)
        fun status(context: Context): String = get(context).message
    }
}
