# Tag releases

Push `vMAJOR.MINOR.PATCH` (or `vMAJOR.MINOR.PATCH-beta.1`) to build Windows x64 MSI/ZIP/7Z, Ubuntu 26.04 amd64 DEB/AppImage and Android debug APK. All three must succeed before the draft release is published. SHA256SUMS.txt is included. Existing published releases are never overwritten.

Tags such as `v1.28.0-bluetooth.1` publish a GitHub prerelease with `latest=false`. Desktop display versions and package filenames preserve the suffix; Debian uses `~bluetooth.1` so it sorts before the stable version. MSI retains a numeric product version. Android keeps its own numeric version and adds the tag suffix to `versionName`. The release jobs run `BluetoothTests` and the CMake version parser checks before packaging.

Linux is built inside Docker; Windows uses a native Windows runner; Android uses JDK 17 and the Android SDK on Linux. The Linux packages target Ubuntu 26.04, not older Ubuntu distributions. Windows packages are not code-signed. Android is explicitly a debug build because wireless recovery currently needs run-as. Do not use this pipeline as a Play Store release process.

Repository secret ANDROID_DEBUG_KEYSTORE_BASE64 must hold base64 of the existing debug.keystore (standard Android debug alias/passwords). Keep the same key for updates. APK versionCode/versionName come from android/app/build.gradle.kts: increment versionCode before releasing an Android update. Tags select the exact committed source; uncommitted local changes are not included.

Actions → Release Windows Linux Android → Run workflow runs a build-only check on a branch. Tag pushes publish releases automatically. No tag is created by the workflow.

Local Linux build: `docker build -f deploy/docker/Dockerfile.linux -t deskconnect-builder .`, then `docker run --rm -v "$PWD:/source" deskconnect-builder`.

Windows ZIP/7Z archives include Qt and compiler runtime libraries plus `settings/DeskConnect.conf`. Extract the complete archive to a writable folder and run `DeskConnect.exe`; no MSI installation is required. Portable mode does not install the Windows service and cannot control UAC/login screens. The workflow checks required executable, Qt platform plugin and portable configuration entries in the ZIP before publishing. Existing tags keep their original workflow: push a new version tag to release this change.

Linux also publishes `DeskConnect-VERSION-linux-x86_64.AppImage`. Give it executable permission and run it without installing the DEB. It reuses the portable Qt/library payload, with appimagetool 1.9.1 and type2-runtime 20251108 pinned and SHA-256 verified. Docker builds and validates extraction without FUSE. The AppImage still requires a compatible host system (built on Ubuntu 26.04); compatibility with older distributions is not promised. User settings remain in the normal user configuration directory, outside the read-only image.
