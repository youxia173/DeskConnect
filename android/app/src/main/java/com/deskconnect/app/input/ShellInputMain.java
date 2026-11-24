package com.deskconnect.app.input;

import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * ADB-authorized, shell-UID input bridge. Run with app_process and this APK on
 * CLASSPATH. It is never registered as an exported Android component.
 *
 * The bootstrap file contains {port,token}; it is deleted before listening.
 * Only loopback clients with the app's random token can inject input. No input
 * is accepted before enter. Disconnect, leave, release and timeout release all
 * pressed keys and cancel any touch gesture. No commands run a shell.
 */
public final class ShellInputMain {
    private static final int MAX_LINE_BYTES = 16384;
    private static final int IDLE_TIMEOUT_MS = 3000;
    private static final int MAX_TEXT_LENGTH = 1024;

    private ShellInputMain() {}

    public static void main(String[] args) {
        try {
            int uid = android.os.Process.myUid();
            if (uid != 2000 && uid != 0) throw new SecurityException("ADB shell authorization is required");
            if (args.length != 1) throw new IllegalArgumentException("Expected private bootstrap configuration path");
            File configuration = new File(args[0]);
            JSONObject config;
            try (InputStream input = new FileInputStream(configuration)) {
                byte[] bytes = readBoundedFile(input, 4096);
                config = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
            } finally {
                if (!configuration.delete() && configuration.exists()) {
                    throw new IOException("Could not remove bootstrap configuration");
                }
            }
            int port = requiredInt(config, "port");
            String token = requiredString(config, "token");
            if (port < 1024 || port > 65535 || token.length() < 32 || token.length() > 512) {
                throw new IllegalArgumentException("Invalid bootstrap configuration");
            }
            if (Looper.getMainLooper() == null) Looper.prepareMainLooper();
            Controller controller = new Controller();
            try (ServerSocket server = new ServerSocket(port, 2, InetAddress.getByName("127.0.0.1"))) {
                System.out.println("DeskConnect input helper ready on loopback (uid=" + uid + ")");
                for (;;) {
                    try (Socket socket = server.accept()) {
                        socket.setTcpNoDelay(true);
                        socket.setSoTimeout(IDLE_TIMEOUT_MS);
                        serve(socket, token, controller);
                    } catch (SocketTimeoutException ignored) {
                        System.out.println("Input control heartbeat expired; released input");
                    } catch (Exception failure) {
                        // Deliberately omit command bodies, tokens and typed text.
                        System.err.println("Input connection ended: " + failure.getClass().getSimpleName());
                    } finally {
                        controller.releaseAll();
                    }
                }
            }
        } catch (Throwable failure) {
            // JSON parsing errors can contain the bootstrap token. Keep even
            // startup diagnostics free of configuration/input contents.
            System.err.println("DeskConnect input helper failed: " + failure.getClass().getSimpleName());
            System.exit(1);
        }
    }

    private static void serve(Socket socket, String token, Controller controller) throws Exception {
        LineReader input = new LineReader(socket);
        OutputStream output = new BufferedOutputStream(socket.getOutputStream());
        String first = input.readLine();
        if (first == null) return;
        JSONObject auth = new JSONObject(first);
        if (!"auth".equals(auth.opt("type")) || !(auth.opt("token") instanceof String) || !MessageDigest.isEqual(
                token.getBytes(StandardCharsets.UTF_8), ((String) auth.get("token")).getBytes(StandardCharsets.UTF_8))) {
            write(output, new JSONObject().put("type", "error").put("code", "unauthorized"));
            return;
        }
        controller.releaseAll();
        write(output, new JSONObject().put("type", "ready").put("version", 1)
                .put("screenUnlocked", controller.canControl(true)));
        for (;;) {
            String line = input.readLine();
            if (line == null) return;
            try {
                JSONObject command = new JSONObject(line);
                String type = requiredString(command, "type");
                if ("ping".equals(type)) {
                    if (controller.active && !controller.canControl(true)) {
                        controller.releaseAll();
                        write(output, new JSONObject().put("type", "error").put("code", "screen_locked"));
                    }
                    write(output, new JSONObject().put("type", "pong"));
                } else if ("leave".equals(type) || "release".equals(type)) {
                    controller.releaseAll();
                } else if ("enter".equals(type)) {
                    controller.releaseAll();
                    if (!controller.canControl(true)) throw new InputFailure("screen_locked");
                    controller.position(command);
                    controller.active = true;
                } else {
                    if (!controller.active) throw new InputFailure("not_entered");
                    if (!controller.canControl(false)) throw new InputFailure("screen_locked");
                    switch (type) {
                        case "move": controller.move(command); break;
                        case "button": controller.button(command); break;
                        case "wheel": controller.wheel(command); break;
                        case "key": controller.key(command); break;
                        case "text": controller.text(requiredString(command, "text")); break;
                        default: throw new InputFailure("unsupported_command");
                    }
                }
            } catch (InputFailure failure) {
                if ("unsupported_text".equals(failure.code)) {
                    // The character map rejects this text before injecting any
                    // event. Keep the active session and held inputs intact.
                    write(output, new JSONObject().put("type", "warning").put("code", failure.code));
                } else {
                    controller.releaseAll();
                    write(output, new JSONObject().put("type", "error").put("code", failure.code));
                }
            } catch (JSONException | IllegalArgumentException failure) {
                controller.releaseAll();
                write(output, new JSONObject().put("type", "error").put("code", "invalid_command"));
            }
        }
    }

    private static final class LineReader {
        final Socket socket;
        final InputStream input;
        final byte[] buffer = new byte[4096];
        int offset;
        int count;

        LineReader(Socket socket) throws IOException {
            this.socket = socket;
            input = socket.getInputStream();
        }

        String readLine() throws IOException {
            ByteArrayOutputStream line = new ByteArrayOutputStream(256);
            long deadline = SystemClock.elapsedRealtime() + IDLE_TIMEOUT_MS;
            for (;;) {
                if (offset == count) {
                    long remaining = deadline - SystemClock.elapsedRealtime();
                    if (remaining <= 0) throw new SocketTimeoutException("Command heartbeat deadline exceeded");
                    // Change the socket timeout once per read, rather than once
                    // per byte during high-frequency mouse movement.
                    socket.setSoTimeout((int) remaining);
                    count = input.read(buffer);
                    offset = 0;
                    if (count == -1) {
                        if (line.size() != 0) throw new IOException("Incomplete command");
                        return null;
                    }
                }
                int next = buffer[offset++] & 0xff;
                if (next == '\n') return line.toString(StandardCharsets.UTF_8.name());
                if (next == '\r') continue;
                if (line.size() >= MAX_LINE_BYTES) throw new IOException("Command exceeds size limit");
                line.write(next);
            }
        }
    }

    private static byte[] readBoundedFile(InputStream input, int limit) throws IOException {
        ByteArrayOutputStream result = new ByteArrayOutputStream();
        byte[] buffer = new byte[1024];
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (result.size() + count > limit) throw new IOException("Bootstrap file too large");
            result.write(buffer, 0, count);
        }
        return result.toByteArray();
    }

    private static void write(OutputStream output, JSONObject response) throws IOException {
        output.write(response.toString().getBytes(StandardCharsets.UTF_8));
        output.write('\n');
        output.flush();
    }

    private static String requiredString(JSONObject object, String name) throws JSONException {
        Object value = object.get(name);
        if (!(value instanceof String)) throw new JSONException("Expected string field");
        return (String) value;
    }

    private static double requiredNumber(JSONObject object, String name) throws JSONException {
        Object value = object.get(name);
        if (!(value instanceof Number)) throw new JSONException("Expected number field");
        return ((Number) value).doubleValue();
    }

    private static int requiredInt(JSONObject object, String name) throws JSONException {
        double value = requiredNumber(object, name);
        if (!Double.isFinite(value) || value != Math.rint(value)
                || value < Integer.MIN_VALUE || value > Integer.MAX_VALUE) {
            throw new JSONException("Expected integer field");
        }
        return (int) value;
    }

    private static int optionalInt(JSONObject object, String name, int fallback) throws JSONException {
        return object.has(name) ? requiredInt(object, name) : fallback;
    }

    private static boolean requiredBoolean(JSONObject object, String name) throws JSONException {
        Object value = object.get(name);
        if (!(value instanceof Boolean)) throw new JSONException("Expected boolean field");
        return (Boolean) value;
    }

    private static final class InputFailure extends Exception {
        final String code;
        InputFailure(String code) { super(code); this.code = code; }
    }

    private static final class PressedKey {
        final long downTime;
        final int meta;
        PressedKey(long time, int state) { downTime = time; meta = state; }
    }

    private static final class Controller {
        final Object manager;
        final Method inject;
        final Object power;
        final Method isInteractive;
        final Object window;
        final Method isKeyguardLocked;
        final Method setDisplayId;
        final Map<Integer, PressedKey> keys = new LinkedHashMap<>();
        final Set<Integer> buttons = new HashSet<>();
        boolean active;
        boolean touching;
        float x;
        float y;
        long touchDownTime;
        long screenStateExpiry;
        boolean screenAllowsControl;

        Controller() throws Exception {
            Class<?> managerClass;
            try { managerClass = Class.forName("android.hardware.input.InputManagerGlobal"); }
            catch (ClassNotFoundException ignored) { managerClass = Class.forName("android.hardware.input.InputManager"); }
            Method get = managerClass.getDeclaredMethod("getInstance");
            get.setAccessible(true);
            manager = get.invoke(null);
            inject = managerClass.getMethod("injectInputEvent", InputEvent.class, int.class);
            Method display = null;
            try { display = InputEvent.class.getMethod("setDisplayId", int.class); }
            catch (NoSuchMethodException ignored) { /* Android 8 uses the default display. */ }
            setDisplayId = display;
            power = service("power", "android.os.IPowerManager");
            isInteractive = Class.forName("android.os.IPowerManager").getMethod("isInteractive");
            window = service("window", "android.view.IWindowManager");
            isKeyguardLocked = Class.forName("android.view.IWindowManager").getMethod("isKeyguardLocked");
        }

        static Object service(String name, String interfaceName) throws Exception {
            Method getService = Class.forName("android.os.ServiceManager").getMethod("getService", String.class);
            IBinder binder = (IBinder) getService.invoke(null, name);
            if (binder == null) throw new IllegalStateException("Missing Android service: " + name);
            return Class.forName(interfaceName + "$Stub").getMethod("asInterface", IBinder.class).invoke(null, binder);
        }

        boolean canControl(boolean refresh) throws InputFailure {
            long now = SystemClock.elapsedRealtime();
            // High-rate mouse packets share this short-lived check. Enter and
            // heartbeat always query Android so idle/returning sessions cannot
            // keep a stale unlocked state.
            if (!refresh && now < screenStateExpiry) return screenAllowsControl;
            try {
                screenAllowsControl = Boolean.TRUE.equals(isInteractive.invoke(power))
                        && !Boolean.TRUE.equals(isKeyguardLocked.invoke(window));
                screenStateExpiry = now + 100;
                return screenAllowsControl;
            } catch (Exception ignored) {
                screenStateExpiry = 0;
                screenAllowsControl = false;
                throw new InputFailure("device_state_unavailable");
            }
        }

        void position(JSONObject command) throws JSONException, InputFailure {
            double nextX = requiredNumber(command, "x");
            double nextY = requiredNumber(command, "y");
            if (!Double.isFinite(nextX) || !Double.isFinite(nextY) || nextX < 0 || nextY < 0 || nextX > 65535 || nextY > 65535) {
                throw new InputFailure("invalid_coordinates");
            }
            x = (float) nextX;
            y = (float) nextY;
        }

        void move(JSONObject command) throws Exception {
            position(command);
            if (touching) touch(MotionEvent.ACTION_MOVE);
            // The app paints the pointer. Hover-only movement must not click or
            // synthesize a swipe; it only updates the next touch/scroll position.
        }

        void button(JSONObject command) throws Exception {
            position(command);
            int button = requiredInt(command, "button");
            boolean down = requiredBoolean(command, "down");
            if (button < 1 || button > 3) throw new InputFailure("unsupported_button");
            if (down && !buttons.add(button)) return;
            if (!down && !buttons.remove(button)) return;
            if (button == 1) {
                if (down) {
                    touchDownTime = SystemClock.uptimeMillis();
                    // Mark before injection so any partial failure still cancels.
                    touching = true;
                    touch(MotionEvent.ACTION_DOWN);
                } else if (touching) {
                    touch(MotionEvent.ACTION_UP);
                    touching = false;
                }
            } else if (down) {
                if (touching) { touch(MotionEvent.ACTION_CANCEL); touching = false; buttons.remove(1); }
                tapKey(button == 3 ? KeyEvent.KEYCODE_BACK : KeyEvent.KEYCODE_HOME);
            }
        }

        void touch(int action) throws InputFailure {
            long now = SystemClock.uptimeMillis();
            MotionEvent event = MotionEvent.obtain(touchDownTime, now, action, x, y, 0);
            event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
            try { send(event); } finally { event.recycle(); }
        }

        void wheel(JSONObject command) throws Exception {
            position(command);
            double dx = requiredNumber(command, "dx");
            double dy = requiredNumber(command, "dy");
            if (!Double.isFinite(dx) || !Double.isFinite(dy) || Math.abs(dx) > 100 || Math.abs(dy) > 100) {
                throw new InputFailure("invalid_scroll");
            }
            MotionEvent.PointerProperties properties = new MotionEvent.PointerProperties();
            properties.id = 0;
            properties.toolType = MotionEvent.TOOL_TYPE_MOUSE;
            MotionEvent.PointerCoords coordinates = new MotionEvent.PointerCoords();
            coordinates.x = x;
            coordinates.y = y;
            coordinates.setAxisValue(MotionEvent.AXIS_HSCROLL, (float) dx);
            coordinates.setAxisValue(MotionEvent.AXIS_VSCROLL, (float) dy);
            long now = SystemClock.uptimeMillis();
            MotionEvent event = MotionEvent.obtain(now, now, MotionEvent.ACTION_SCROLL, 1,
                    new MotionEvent.PointerProperties[] {properties}, new MotionEvent.PointerCoords[] {coordinates},
                    0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_MOUSE, 0);
            try { send(event); } finally { event.recycle(); }
        }

        void key(JSONObject command) throws Exception {
            int keyCode = requiredInt(command, "keyCode");
            if (keyCode <= KeyEvent.KEYCODE_UNKNOWN || keyCode > KeyEvent.getMaxKeyCode()) throw new InputFailure("invalid_key");
            boolean down = requiredBoolean(command, "down");
            int allowedMeta = KeyEvent.META_SHIFT_MASK | KeyEvent.META_ALT_MASK | KeyEvent.META_CTRL_MASK
                    | KeyEvent.META_META_MASK | KeyEvent.META_SYM_ON | KeyEvent.META_FUNCTION_ON
                    | KeyEvent.META_CAPS_LOCK_ON | KeyEvent.META_NUM_LOCK_ON | KeyEvent.META_SCROLL_LOCK_ON;
            int meta = KeyEvent.normalizeMetaState(optionalInt(command, "metaState", 0) & allowedMeta);
            int repeat = optionalInt(command, "repeat", 0);
            if (repeat < 0 || repeat > 1000) throw new InputFailure("invalid_repeat");
            long now = SystemClock.uptimeMillis();
            PressedKey pressed = keys.get(keyCode);
            if (down) {
                if (pressed == null) {
                    pressed = new PressedKey(now, meta);
                    keys.put(keyCode, pressed);
                } else if (repeat == 0) {
                    return;
                }
                send(keyEvent(pressed.downTime, now, KeyEvent.ACTION_DOWN, keyCode, repeat, meta));
            } else if (pressed != null) {
                send(keyEvent(pressed.downTime, now, KeyEvent.ACTION_UP, keyCode, 0, meta));
                keys.remove(keyCode);
            }
        }

        void text(String text) throws InputFailure {
            if (text.length() > MAX_TEXT_LENGTH) throw new InputFailure("text_too_long");
            KeyEvent[] events = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD).getEvents(text.toCharArray());
            if (events == null) throw new InputFailure("unsupported_text");
            // Modifier/key events from the character map are balanced before any
            // next wire command; errors release tracked downs as with normal keys.
            for (KeyEvent event : events) {
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    keys.put(event.getKeyCode(), new PressedKey(event.getDownTime(), event.getMetaState()));
                }
                send(event);
                if (event.getAction() == KeyEvent.ACTION_UP) keys.remove(event.getKeyCode());
            }
        }

        static KeyEvent keyEvent(long down, long now, int action, int code, int repeat, int meta) {
            return new KeyEvent(down, now, action, code, repeat, meta, KeyCharacterMap.VIRTUAL_KEYBOARD,
                    0, 0, InputDevice.SOURCE_KEYBOARD);
        }

        void tapKey(int code) throws InputFailure {
            long now = SystemClock.uptimeMillis();
            keys.put(code, new PressedKey(now, 0));
            send(keyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0, 0));
            send(keyEvent(now, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP, code, 0, 0));
            keys.remove(code);
        }

        void send(InputEvent event) throws InputFailure {
            try {
                if (setDisplayId != null) setDisplayId.invoke(event, 0);
                // ASYNC avoids waiting for focus or a stalled target application.
                // Socket heartbeat/release processing must remain responsive.
                if (!Boolean.TRUE.equals(inject.invoke(manager, event, 0))) throw new InputFailure("input_rejected");
            } catch (InputFailure failure) { throw failure; }
            catch (Exception failure) {
                Throwable cause = failure;
                for (int depth = 0; depth < 5 && cause.getCause() != null && cause.getCause() != cause; depth++) {
                    cause = cause.getCause();
                }
                throw new InputFailure(cause instanceof SecurityException
                        ? "injection_permission_denied" : "injection_api_error");
            }
        }

        void releaseAll() {
            active = false;
            if (touching) {
                try { touch(MotionEvent.ACTION_CANCEL); } catch (Exception ignored) {}
                touching = false;
            }
            for (Map.Entry<Integer, PressedKey> entry : keys.entrySet()) {
                try {
                    send(keyEvent(entry.getValue().downTime, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP,
                            entry.getKey(), 0, 0));
                } catch (Exception ignored) {}
            }
            keys.clear();
            buttons.clear();
        }
    }
}
