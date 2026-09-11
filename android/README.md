# DeskConnect Android

LAN companion app for the DeskConnect PC server. Speaks the Barrier/Deskflow protocol over TCP `24800`, with optional **TLS** (default on).

## Features

- Connect by server IP + port (default `24800`)
- TLS 1.2+ with SHA-256 fingerprint trust (same model as DeskConnect PC)
- Self-signed client certificate for PC "require client certificate" / PeerAuth
- Screen name must already exist in the PC layout (default `Android`)
- Bidirectional **text** clipboard sync
- Send files to PC / receive files from PC
- Foreground service keeps the session alive

## Requirements

- Android Studio Ladybug+ (or SDK 35 + JDK 17)
- Same LAN; firewall allows TCP `24800`
- A screen named e.g. `Android` in the server layout

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

## Limits

- Text clipboard only on Android (no images/HTML yet)
- Not a full KVM client (mouse/keyboard events are ignored)
- Screen must be configured on the server (`EUNK` if the name is unknown)

Received files:

`Android/data/com.deskconnect.app/files/Download/DeskConnect/`

## Layout in this repo

```
android/
  app/src/main/java/com/deskconnect/app/
    ConnectionService.kt
    protocol/          # Barrier framing, TLS, client
    ui/MainActivity.kt
```
