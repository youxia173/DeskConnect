# DeskConnect

基于 [Deskflow](https://github.com/deskflow/deskflow) 的键鼠共享工具，在 DeskConnect 品牌下维护与扩展。

支持 Windows / Linux / macOS 跨设备共用一套键鼠，并在 DeskConnect ↔ DeskConnect 之间支持**剪切板式跨机传文件**。

仓库地址：<https://github.com/youxia173/DeskConnect>

## 主要功能

- 键鼠跨屏共享（Deskflow 协议，默认端口 `24800`）
- 文本 / HTML / 图片剪切板同步
- **复制文件 → 切到另一台 → 粘贴落地**（两侧均需本仓库构建的 DeskConnect）
- 开机启动、主机历史、按 WiFi 记忆 IP、中文界面等客户端便利功能

## Linux 安装包

本地打包后：

- **推荐** `dist/DeskConnect-v*-linux-x86_64.tar.gz`（自带 Qt 的便携包）
- 解压后执行 `./DeskConnect`，或 `sudo ./install.sh` 安装到 `/opt/DeskConnect`

生成方式：`QTDIR=/path/to/Qt/6.8.x/gcc_64 ./deploy/linux/make-portable.sh`

完整说明见仓库根目录 [README.md](../README.md)。

## 许可证

- Deskflow 衍生代码：GPL-2.0
- `kdeconnect-kde/`：保留其原有许可证
