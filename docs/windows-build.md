# Windows 编译与交付说明

## 编译前：更新版本号

每次修 bug / 发版前，先改 `CMakeLists.txt` 里的小版本（`PATCH`）：

```cmake
set(DESKFLOW_VERSION_MAJOR 1)
set(DESKFLOW_VERSION_MINOR 26)
set(DESKFLOW_VERSION_PATCH 18)   # ← 每次发版 +1
set(DESKFLOW_VERSION_TWEAK 0)
```

改完后重新配置并编译，否则 `DeskConnect.exe` 属性里的文件版本不会变。

```powershell
cmake -S . -B build
cmake --build build --config Release --target deskflow deskflow-core deskflow-daemon -j 8
```

确认本机 exe 版本：

```powershell
(Get-Item "D:\Other\CursorCode\Desk Connect\build\bin\Release\DeskConnect.exe").VersionInfo.FileVersion
```

## 需要的产物（只要这两样）

| 用途 | 路径 |
|------|------|
| 本机直接运行 | `D:\Other\CursorCode\Desk Connect\build\bin\Release\DeskConnect.exe` |
| 给别人安装 | `D:\Other\CursorCode\Desk Connect\build\deskflow-<版本>-win-x64.msi` |

打 MSI：

```powershell
cmake --build build --config Release --target package -j 8
```

生成后在 `build\` 下取 `deskflow-*-win-x64.msi` 即可。

## 不需要保留的文件

以下都不必当交付物，用完可删，避免和当前版本搞混：

- `*-portable.zip` / `*-portable.7z` 便携包
- `dist\` 里的旧 zip、旧 apk、零散 exe
- `build\bin-transfer-fix\` 等临时输出目录
- `build\_CPack_Packages\` 打包临时目录
- 历史版本的旧 MSI / 旧 zip

## 注意

- Release 输出目录必须是 `build\bin\Release\`。若 CMake 缓存被改成别的目录（例如 `bin-transfer-fix`），请改回后再编，否则你会以为版本没更新。
- 本机调试优先用 `build\bin\Release\DeskConnect.exe`；对外分发用 MSI。
