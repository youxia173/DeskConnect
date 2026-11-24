这是蓝牙功能的实验预发布版，正式最新版仍为 v1.27.5。

- Windows、Linux 桌面端加入“编辑 → 蓝牙直连”入口，默认关闭。两台电脑都需安装此预览版，并先在系统蓝牙设置中完成配对。
- 支持一端作为服务端、另一端作为客户端，沿用原有屏幕布局和边缘切换，保留 TLS 和证书指纹验证。
- 当前蓝牙模式仅传键鼠，不共享剪切板或文件；需要桌面进程模式。关闭蓝牙模式后可继续使用原网络连接。
- 蓝牙硬件互通、休眠恢复和断线返回本机仍需双机实测，不代表已经完成硬件验收。
- 修复 Windows 蓝牙临时配置文件被占用、无法保存而导致会话启动失败的问题。

安装包：Windows MSI / ZIP / 7Z，Linux DEB / AppImage，Android debug APK。Android 仍使用现有局域网控制方式，此次没有增加 Android 蓝牙控制。

桌面显示版本和下载文件名包含 `bluetooth` 标识；Android 显示 `1.2.3-bluetooth.N`。Linux DEB 使用 `~bluetooth.N` 表示预发布。Windows MSI 使用数字产品版本，文件名及程序界面保留预发布标识。

Linux 包在 Ubuntu 26.04 构建，不能保证在更旧发行版运行。需宿主系统的 BlueZ 蓝牙服务。Windows ZIP/7Z 解压完整目录后运行 `DeskConnect.exe`。校验值见 `SHA256SUMS.txt`。
