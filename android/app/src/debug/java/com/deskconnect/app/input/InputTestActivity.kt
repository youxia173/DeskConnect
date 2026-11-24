package com.deskconnect.app.input

import android.app.Activity
import android.graphics.Color
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.deskconnect.app.ConnectionService
import com.deskconnect.app.protocol.BarrierClient
import com.deskconnect.app.protocol.BarrierClientListener
import com.deskconnect.app.protocol.ReceivedFileStore
import com.deskconnect.app.protocol.RemoteInputEvent
import com.deskconnect.app.protocol.RemoteScreenInfo
import org.json.JSONObject
import java.io.IOException
import java.io.OutputStream
import kotlin.math.roundToInt

/**
 * Debug-only end-to-end input target. Logs only this page's synthetic test data;
 * it does not read the clipboard, inspect other apps, or accept file transfers.
 */
class InputTestActivity : Activity() {
    private lateinit var controller: InputController
    private lateinit var statusText: TextView
    private lateinit var editor: EditText
    private lateinit var clickButton: Button
    private lateinit var scrollArea: ScrollView
    @Volatile private var client: BarrierClient? = null
    @Volatile private var disposed = false
    private var clicks = 0
    private var lastLayout = ""
    private val main = Handler(Looper.getMainLooper())

    private val inputFailure: (String) -> Unit = { reason ->
        Log.i(TAG, "input failure reason=${JSONObject.quote(reason)}")
        // A failure can originate while InputController holds its input lock.
        // Disconnect later to avoid reversing BarrierClient's lock order.
        main.post {
            client?.disconnect()
            showStatus("输入失败，测试连接已断开：$reason")
        }
    }

    private val geometryChanged: () -> Unit = {
        client?.refreshScreenInfo()
        editor.post { logLayout() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildTestPage()
        Log.i(TAG, "created scope=this_test_page_only")

        // The input bridge is shared with the real connection service. Refuse
        // concurrent sessions instead of releasing another session's keys.
        if (ConnectionService.isSessionActive()) {
            showStatus("请先断开主界面的电脑连接，再重新打开输入测试。")
            Log.i(TAG, "connection refused reason=normal_session_active")
            return
        }

        controller = InputController.get(this)
        controller.addFailureListener(inputFailure)
        controller.addGeometryListener(geometryChanged)

        val host = intent.getStringExtra("test_host")?.trim().orEmpty().ifBlank { "127.0.0.1" }
        val port = intent.getIntExtra("test_port", 24999)
        if (port !in 1..65535) {
            showStatus("测试端口无效")
            Log.i(TAG, "connection refused reason=invalid_port")
            return
        }
        val testClient = BarrierClient(object : BarrierClientListener {
            override fun onConnected(protocol: String, major: Int, minor: Int) {
                if (disposed) {
                    client?.disconnect()
                    return
                }
                Log.i(TAG, "connected protocol=$protocol version=$major.$minor")
                showStatus("已连接测试服务器；${InputController.status(this@InputTestActivity)}")
            }

            override fun onDisconnected(reason: String?) {
                controller.release()
                Log.i(TAG, "disconnected reason=${JSONObject.quote(reason.orEmpty())}")
                showStatus("测试连接已断开：${reason.orEmpty()}")
            }

            override fun onLog(message: String) {
                Log.i(TAG, "protocol ${JSONObject.quote(message)}")
            }

            override fun getScreenInfo(): RemoteScreenInfo = controller.screenInfo()

            override fun onInputEvent(event: RemoteInputEvent) {
                if (!disposed) controller.accept(event)
            }

            override fun onClipboardText(text: String) {
                Log.i(TAG, "clipboard ignored")
            }

            override fun onFileReceived(fileName: String, absolutePath: String) = Unit
            override fun onFileProgress(message: String) = Unit
        }, object : ReceivedFileStore {
            override fun open(fileName: String): Pair<String, OutputStream> {
                throw IOException("Input test does not accept files")
            }
        })
        client = testClient
        showStatus("正在连接输入测试服务器 $host:$port…")
        Log.i(TAG, "connecting host=${JSONObject.quote(host)} port=$port tls=false")
        Thread({
            if (!disposed) {
                testClient.connect(host, port, SCREEN_NAME, useTls = false)
            }
        }, "deskconnect-input-test").apply { isDaemon = true; start() }
    }

    @Suppress("DEPRECATION")
    private fun buildTestPage() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.WHITE)
            isFocusableInTouchMode = true
            setPadding(dp(16), dp(16), dp(16), dp(16))
        }
        root.setOnApplyWindowInsetsListener { view, insets ->
            val bars = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                insets.getInsets(WindowInsets.Type.systemBars()).let { Rect(it.left, it.top, it.right, it.bottom) }
            } else {
                Rect(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            view.setPadding(dp(16) + bars.left, dp(16) + bars.top,
                dp(16) + bars.right, dp(16) + bars.bottom)
            insets
        }
        root.addView(label("DeskConnect 键鼠输入测试", 22f), row())
        root.addView(label("仅记录本测试页的文字、点击和滚动；不读取系统剪贴板或其他应用。", 14f), row())
        statusText = label("准备连接…", 14f)
        root.addView(statusText, row())

        editor = EditText(this).apply {
            hint = "点击这里，测试电脑键盘输入"
            setTextColor(Color.BLACK)
            setHintTextColor(Color.DKGRAY)
            setBackgroundColor(0xffeeeeee.toInt())
            textSize = 20f
            gravity = Gravity.TOP or Gravity.START
            // Keep this diagnostic field in ASCII mode: a Chinese IME may hold
            // ordinary letters as an uncommitted pinyin candidate indefinitely.
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
                InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            // Keep coordinates stable while injecting hardware key events.
            showSoftInputOnFocus = false
            setPadding(dp(10), dp(10), dp(10), dp(10))
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
                override fun afterTextChanged(value: Editable?) {
                    Log.i(TAG, "text value=${JSONObject.quote(value?.toString().orEmpty())}")
                }
            })
            setOnFocusChangeListener { _, focused -> Log.i(TAG, "editor focus=$focused") }
        }
        root.addView(editor, row(dp(150)))

        clickButton = Button(this).apply {
            text = "点击计数：0"
            isAllCaps = false
            setOnClickListener {
                clicks++
                text = "点击计数：$clicks"
                Log.i(TAG, "click count=$clicks")
            }
        }
        root.addView(clickButton, row(dp(58)))

        scrollArea = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(0xffe5efff.toInt())
            setOnScrollChangeListener { _, x, y, oldX, oldY ->
                Log.i(TAG, "scroll x=$x y=$y previousX=$oldX previousY=$oldY")
            }
            addView(LinearLayout(this@InputTestActivity).apply {
                orientation = LinearLayout.VERTICAL
                (1..60).forEach { index ->
                    addView(label("滚动测试第 $index 行", 18f).apply {
                        gravity = Gravity.CENTER_VERTICAL
                        setPadding(dp(12), 0, dp(12), 0)
                    }, row(dp(46)))
                }
            })
        }
        root.addView(scrollArea, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f
        ))
        setContentView(root)
        root.requestFocus()
        root.viewTreeObserver.addOnGlobalLayoutListener { logLayout() }
    }

    private fun label(value: String, size: Float) = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(Color.BLACK)
        setPadding(0, dp(4), 0, dp(4))
    }

    private fun row(height: Int = LinearLayout.LayoutParams.WRAP_CONTENT) =
        LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, height)

    private fun dp(value: Int) = (value * resources.displayMetrics.density).roundToInt()

    private fun logLayout() {
        if (disposed || !this::editor.isInitialized) return
        val entries = listOf("editor" to editor, "button" to clickButton, "scroll" to scrollArea).map { (name, view) ->
            val location = IntArray(2)
            view.getLocationOnScreen(location)
            val left = location[0]
            val top = location[1]
            "layout name=$name left=$left top=$top right=${left + view.width} " +
                "bottom=${top + view.height} centerX=${left + view.width / 2} centerY=${top + view.height / 2}"
        }
        val combined = entries.joinToString("\n")
        if (combined != lastLayout) {
            lastLayout = combined
            entries.forEach { Log.i(TAG, it) }
            if (this::controller.isInitialized) {
                val info = controller.screenInfo()
                Log.i(TAG, "layout screen width=${info.width} height=${info.height}")
            }
        }
    }

    private fun showStatus(value: String) {
        runOnUiThread {
            if (!disposed && !isFinishing) statusText.text = value
        }
    }

    override fun onDestroy() {
        disposed = true
        client?.disconnect()
        if (this::controller.isInitialized) {
            controller.removeFailureListener(inputFailure)
            controller.removeGeometryListener(geometryChanged)
            controller.release()
        }
        Log.i(TAG, "destroyed input_released=true")
        super.onDestroy()
    }

    companion object {
        private const val TAG = "DeskConnectInputTest"
        private const val SCREEN_NAME = "DeskConnectInputTest"
    }
}
