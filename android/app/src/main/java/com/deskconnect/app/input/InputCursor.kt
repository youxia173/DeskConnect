package com.deskconnect.app.input

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import com.deskconnect.app.SessionLog
import kotlin.math.roundToInt

/** A passive pointer: it must never take focus or intercept local or injected touches. */
class InputCursor(context: Context, private val onFailure: (String) -> Unit = {}) {
    private val app = context.applicationContext
    private val windowManager = app.getSystemService(WindowManager::class.java)
    private val main = Handler(Looper.getMainLooper())
    private val lock = Any()
    private var state = State()
    private var renderPending = false
    private var view: CursorView? = null
    private var lastFailure: String? = null
    private val density = app.resources.displayMetrics.density
    private val inset = (2f * density).roundToInt()
    private val params = WindowManager.LayoutParams(
        (28f * density).roundToInt(),
        (34f * density).roundToInt(),
        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
        PixelFormat.TRANSLUCENT
    ).apply {
        gravity = Gravity.TOP or Gravity.LEFT
        // Android 12+ rejects touches under an opaque, untrusted overlay. Keep the
        // entire window below its obscuring-opacity limit, even over the cursor.
        alpha = 0.7f
        title = "DeskConnect pointer"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) setFitInsetsTypes(0)
    }

    private val render = Runnable {
        val snapshot = synchronized(lock) {
            renderPending = false
            state
        }
        renderOnMain(snapshot)
    }

    fun show(x: Int, y: Int) = update { copy(visible = true, x = x, y = y) }

    fun move(x: Int, y: Int) = update { copy(x = x, y = y) }

    fun hide() = update { copy(visible = false) }

    fun close() = update { copy(visible = false, closed = true) }

    private fun update(transform: State.() -> State) {
        synchronized(lock) {
            if (state.closed) return
            state = state.transform()
            if (!renderPending) {
                renderPending = true
                // Coalesce high-rate motion into one render per frame. Input
                // injection is independent and never waits for the UI thread.
                main.postDelayed(render, 16)
            }
        }
    }

    private fun renderOnMain(snapshot: State) {
        if (snapshot.closed || !snapshot.visible) {
            removeView()
            return
        }
        // WindowManager is the authority for overlay permission. Some OEMs
        // return false from canDrawOverlays after a previous pointer is removed
        // despite an allowed AppOp. A rejected add/update still exits control.
        params.x = snapshot.x - inset
        params.y = snapshot.y - inset
        try {
            val current = view
            if (current == null) {
                val created = CursorView(app)
                windowManager.addView(created, params)
                view = created
            } else {
                windowManager.updateViewLayout(current, params)
            }
            lastFailure = null
        } catch (error: RuntimeException) {
            // Permission may be revoked or the display removed between the
            // permission check and add/update. Return control instead of leaving
            // the user controlling a device without a visible pointer.
            removeView()
            logFailure("鼠标指针显示失败：${error.javaClass.simpleName}")
        }
    }

    private fun removeView() {
        val current = view ?: return
        view = null
        try {
            windowManager.removeViewImmediate(current)
        } catch (_: RuntimeException) {
            // WindowManager may already have removed a revoked overlay.
        }
    }

    private fun logFailure(message: String) {
        if (lastFailure != message) {
            lastFailure = message
            SessionLog.append("input cursor: $message")
        }
        onFailure(message)
    }

    private data class State(
        val visible: Boolean = false,
        val x: Int = 0,
        val y: Int = 0,
        val closed: Boolean = false
    )

    private class CursorView(context: Context) : View(context) {
        private val scale = resources.displayMetrics.density
        private val arrow = Path().apply {
            moveTo(2f, 2f)
            lineTo(2f, 26f)
            lineTo(8f, 20f)
            lineTo(13f, 31f)
            lineTo(18f, 28.5f)
            lineTo(13f, 18f)
            lineTo(22f, 18f)
            close()
        }
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        init {
            importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        }

        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            canvas.save()
            canvas.scale(scale, scale)
            paint.style = Paint.Style.FILL
            paint.color = Color.WHITE
            canvas.drawPath(arrow, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 1.5f
            paint.strokeJoin = Paint.Join.ROUND
            paint.color = Color.BLACK
            canvas.drawPath(arrow, paint)
            canvas.restore()
        }
    }
}
