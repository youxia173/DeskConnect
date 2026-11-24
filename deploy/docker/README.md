# Tag releases

Push `vMAJOR.MINOR.PATCH` (or `vMAJOR.MINOR.PATCH-beta.1`) to build Windows x64 MSI/7Z, Ubuntu 26.04 amd64 DEB and Android debug APK. All three must succeed before the draft release is published. SHA256SUMS.txt is included. Existing published releases are never overwritten.

Linux is built inside Docker; Windows uses a native Windows runner; Android uses JDK 17 and the Android SDK on Linux. The DEB targets Ubuntu 26.04, not older Ubuntu distributions. Windows packages are not code-signed. Android is explicitly a debug build because wireless recovery currently needs run-as. Do not use this pipeline as a Play Store release process.

Repository secret ANDROID_DEBUG_KEYSTORE_BASE64 must hold base64 of the existing debug.keystore (standard Android debug alias/passwords). Keep the same key for updates. APK versionCode/versionName come from android/app/build.gradle.kts: increment versionCode before releasing an Android update. Tags select the exact committed source; uncommitted local changes are not included.

Actions → Release Windows Linux Android → Run workflow runs a build-only check on a branch. Tag pushes publish releases automatically. No tag is created by the workflow.

Local Linux build: `docker build -f deploy/docker/Dockerfile.linux -t deskconnect-builder .`, then `docker run --rm -v "$PWD:/source" deskconnect-builder`.
