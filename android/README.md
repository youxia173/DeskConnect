# DeskConnect Android

LAN companion app for the DeskConnect PC server. Speaks the Barrier/Deskflow protocol over TCP `24800`, with optional **TLS** (default on).

## Features

- Connect by server IP + port (default `24800`)
- TLS 1.2+ with SHA-256 fingerprint trust (same model as DeskConnect PC)
- Self-signed client certificate for PC "require client certificate" / PeerAuth
- Screen name must already exist in the PC layout (default `Android`)
- Bidirectional **text** clipboard sync
- Send files to PC / receive files from PC
- Control the phone with the PC server's keyboard and mouse after USB ADB activation
- Phone-side floating pointer, touch dragging, scrolling and keyboard shortcuts
- Foreground service keeps the session alive

## Requirements

- Android Studio Ladybug+ (or SDK 35 + JDK 17)
- Same LAN; firewall allows TCP `24800`
- A screen named e.g. `Android` in the server layout
- For keyboard/mouse control: an unlocked phone, USB debugging authorized for the activating computer, overlay permission, and the debug APK

## Open & build

1. Open the `android/` folder in Android Studio (not the repo root).
2. Let Gradle sync; install any prompted SDK components.
3. Run on a device or emulator.

```bash
./gradlew :app:assembleDebug
```

APK: `app/build/outputs/apk/debug/app-debug.apk`

Windows:

```powershell
.\gradlew.bat :app:assembleDebug
```

## PC + phone pairing (TLS)

DeskConnect PC defaults to TLS **and** "require client certificate". Pairing is two-way:

1. Start DeskConnect in **server** mode with TLS enabled.
2. On the phone, leave **使用 TLS 加密** checked, enter the PC IP, tap Connect.
3. Phone shows the **server** SHA-256 fingerprint -> Trust (compare with PC fingerprint UI).
4. PC may prompt to trust the **phone** client fingerprint -> Accept.
5. Connect again if either side disconnected during the trust dialogs.

The phone shows its own fingerprint under the TLS switch (`本机指纹`) so you can match it on the PC.

To use plaintext instead, turn off TLS in the app **and** disable encryption on the PC.

## Keyboard and mouse control

The phone remains a client in the PC server's screen layout. Place its screen beside the PC, connect normally, then move the PC pointer across that edge to control the phone. The phone displays its own floating pointer; this feature does not mirror its screen onto the PC.

First install the debug APK above. Enable USB debugging on the phone, connect it by USB, unlock it, and accept the computer's ADB authorization prompt. The activation script enables DeskConnect's enhanced input switch and requests its overlay app-op through ADB. Confirm that the app shows `ADB 增强模式已就绪` before entering its screen.

From the repository root on Ubuntu/Linux:

```bash
bash android/tools/activate-input.sh
# If more than one device is connected:
bash android/tools/activate-input.sh DEVICE_SERIAL
# Optional executable and serial environment variables:
ADB=/path/to/platform-tools/adb SERIAL=DEVICE_SERIAL bash android/tools/activate-input.sh
```

The Linux script requires Bash, `adb`, and Python 3. On Windows:

```powershell
powershell -ExecutionPolicy Bypass -File android/tools/activate-input.ps1
# Optional arguments:
powershell -ExecutionPolicy Bypass -File android/tools/activate-input.ps1 -Serial DEVICE_SERIAL -Adb C:\Android\platform-tools\adb.exe
```

The script starts a device-local helper as the authorized ADB shell user. Root is not required. The helper accepts only authenticated loopback connections from the app; keyboard and mouse commands still arrive through the normal DeskConnect connection. USB is used for activation, and the PC and phone must have a working network connection for normal control. Run activation again after the phone restarts, the helper exits, or the APK is updated. The scripts use `run-as`, so a non-debug/release APK cannot be activated this way.

Controls:

- Move: position the phone's floating pointer.
- Left button: tap; hold and move to drag or swipe.
- Right button: Android Back.
- Middle button: Android Home.
- Wheel: scroll at the pointer position where the target app supports mouse scrolling.
- Keyboard: send Android key events, including modifiers and common shortcuts.
- `Ctrl+Alt+Esc`: stop phone control and return control to the PC. The PC server's emergency return shortcut remains available too.

Leaving the phone screen, disconnecting, or losing the input helper releases held input. A screen-size/rotation change ends the current control session so the PC can receive the updated dimensions. Screen-off and lock-screen states reject control; unlock the phone locally. Some manufacturers require an additional option such as **USB debugging (Security settings)** for injected input.

## Server-managed wireless recovery (Android 1.2.2 + updated desktop server)

Recovery belongs to the DeskConnect **server core**, on Windows or Ubuntu. No Windows scheduled task, systemd unit, login script or separately installed recovery daemon is required. Client mode never launches recovery. The Python worker is bundled in the server resource and extracted privately for its lifetime; no source checkout is needed on the user's machine.

1. On the server computer, install Python 3.10+ and current Android platform-tools. Put `python3`/`python` and `adb` on PATH, or set `DESKCONNECT_PYTHON` and `DESKCONNECT_ADB` to their executable paths before starting the server. ADB is also found in `platform-tools` beside the server executable, or under `ANDROID_HOME` / `ANDROID_SDK_ROOT`. Service mode needs these tools and ADB authorization available to the account running the core. Missing dependencies are reported in the core log; restart the server after installing them.
2. Pair each phone's Wireless debugging with **that server computer** once. Use the pairing dialog's IP/port and code with `adb pair IP:PAIR_PORT` (not its separate connection port). USB authorization can bootstrap a connected phone, but does not guarantee wireless pairing on all devices. No pairing code is saved by DeskConnect.
3. Install Android 1.2.2, enable **键鼠控制** and **无线自动恢复控制**, and connect the phone to the updated DeskConnect server. Registration happens over the existing connection after capability negotiation. No model, serial number or IP is hardcoded or entered into a recovery task. Ordinary desktop clients and phones without opt-in never start a worker.
4. The server matches each registration against the app's private ADB-readable identity, then verifies the physical serial on every subsequent ADB connection. Up to 32 registered phones are tracked separately, with at most four concurrent ADB checks. mDNS discovers changed connection ports; a phone-provided WLAN address can assist discovery. Pairing authorization and identity checks are still required before activation.

A registered phone may recover after an unexpected disconnect/reboot while the server stays running. Stopping the server, switching it to client mode, removing the phone from its layout, closing the recovery pipe or exiting the core ends recovery. Turning off either phone switch or explicitly disconnecting pauses activation; changing to another server invalidates the previous session's recovery identity. Already running input helpers are left alone. A phone must connect once again after the server itself restarts: registrations deliberately last only for that server run.

The phone must be unlocked after reboot, on the same LAN, with Wireless debugging enabled. If Android disables Wireless debugging, turn it back on locally. This currently requires a debug APK because private configuration is read with `run-as`; release APKs need a different provisioning mechanism. Recovery is not screen mirroring and does not bypass Android input/overlay permissions.

Status and dependency errors appear in the **DeskConnect server core log**, prefixed `android recovery`. No pairing code or input bridge token is logged. `wireless-input.py watch` remains a manual development diagnostic; do not deploy it as a login task. Run `python android/tools/test_wireless_input.py` for host regression tests. The desktop core must be rebuilt to include the server integration; updating only the phone APK is insufficient.

## Device verification

The activation scripts check that the helper reports startup readiness. That check does not verify actual taps, keyboard input, or a complete DeskConnect connection. After activation, use a harmless text field or settings screen to check movement, tap/drag, scrolling, Back/Home, and typing; then check leaving the phone and disconnecting with a key held. Confirm `Ctrl+Alt+Esc` returns control to the PC and that reconnecting works. Avoid testing with an unsaved document or a destructive action selected.

For a repeatable device check, disconnect the app's normal DeskConnect session, leave the phone unlocked, and activate the helper first. Then run:

```bash
python3 android/tools/test-input-device.py --serial DEVICE_SERIAL
# Optional ADB executable path (use python on Windows if appropriate):
python3 android/tools/test-input-device.py --adb /path/to/adb --serial DEVICE_SERIAL
```

The debug-only test activity and script exercise click, ASCII text entry, Shift/Backspace, wheel scrolling, left-button dragging, six seconds of sustained motion, leaving/re-entering with held-key release, and Ctrl+Alt+Esc emergency disconnect. The test field uses visible-password input mode to avoid Chinese IMEs holding the ASCII test string as an uncommitted pinyin candidate; the user's default IME is unchanged. Only ADB/system callers with DUMP permission can launch this diagnostic activity. The script creates a local test server and an ADB reverse mapping on TCP `24999`, removing its mapping on exit; that port must be free. It edits only the dedicated test page. This checks the Android input path and protocol handling, while a real PC-server session is still needed to check screen-edge transitions and the complete deployment. Treat the script's actual PASS/FAIL output as the result.

When reporting a device-specific failure, include the phone model/Android version, whether the app displayed enhanced mode as ready, the DeskConnect app log, and the helper log:

```bash
adb shell cat /data/local/tmp/deskconnect-input/helper.log
```

## Limits

- Text clipboard only on Android (no images/HTML yet)
- No screen mirroring; view the phone's physical display while controlling it
- Chinese and other complex text input depend on the phone's active IME and its hardware-keyboard support; arbitrary Unicode text injection is not guaranteed
- This is ordinary touch/keyboard control, not a multi-touch or game-controller emulator
- ADB injection permissions and overlay behavior vary by Android version and manufacturer
- Screen must be configured on the server (`EUNK` if the name is unknown)

Received files:

`Android/data/com.deskconnect.app/files/Download/DeskConnect/`

## Layout in this repo

```
android/
  app/src/main/java/com/deskconnect/app/
    ConnectionService.kt
    protocol/          # Barrier framing, TLS, client
    input/             # Input bridge, ADB shell helper, pointer and key mapping
    ui/MainActivity.kt
  tools/
    activate-input.sh  # Ubuntu/Linux USB activation
    activate-input.ps1 # Windows USB activation
    test-input-device.py # Debug-only on-device input checks
```
