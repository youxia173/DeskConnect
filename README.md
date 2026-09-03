# DeskConnect

基于 [Deskflow](https://github.com/deskflow/deskflow) 的键鼠共享工具，在 DeskConnect 品牌下维护与扩展。

支持 Windows / Linux / macOS 跨设备共用一套键鼠，并在 DeskConnect ↔ DeskConnect 之间支持**剪切板式跨机传文件**。

仓库地址：<https://github.com/youxia173/DeskConnect>

## 主要功能

- 键鼠跨屏共享（Deskflow 协议，默认端口 `24800`）
- 文本 / HTML / 图片剪切板同步
- **复制文件 → 切到另一台 → 粘贴落地**（两侧均需本仓库构建的 DeskConnect）
  - Windows：`CF_HDROP`
  - Linux：`text/uri-list`（X11 / Portal）
- 开机启动、主机历史、按 WiFi 记忆 IP、中文界面等客户端便利功能

## Linux 安装包

构建后产物在仓库的 `dist/` 目录（该目录已 gitignore，需本地打包生成）：

| 文件 | 说明 |
|------|------|
| `dist/DeskConnect-v*-linux-x86_64.tar.gz` | 自带 Qt 的便携包（约 30MB） |
| `dist/deskconnect_*_amd64.deb` | **推荐**：自带 Qt 的 deb，安装到 `/opt/DeskConnect` |
| `dist/DeskConnect/` | 解压后的目录树（可直接运行或执行安装脚本） |
| `build/deskflow-*-ubuntu-*.deb` | CPack 瘦包（**不**捆绑 Qt，系统 Qt 过旧时可能无法启动） |

### deb 安装

```bash
# 生成（会先确保便携目录存在）
QTDIR=/path/to/Qt/6.8.x/gcc_64 ./deploy/linux/make-deb.sh

sudo apt install ./dist/deskconnect_*_amd64.deb
# 或
sudo dpkg -i ./dist/deskconnect_*_amd64.deb
```

### 便携包使用

```bash
# 生成便携包（需先完成 Release 编译，并设置 QTDIR）
QTDIR=/path/to/Qt/6.8.x/gcc_64 ./deploy/linux/make-portable.sh

# 解压后直接运行
tar -xzf dist/DeskConnect-v*-linux-x86_64.tar.gz
cd DeskConnect
./DeskConnect

# 或安装到 /opt/DeskConnect
sudo ./install.sh
```

安装后可从应用菜单打开 **DeskConnect**，或运行 `/opt/DeskConnect/DeskConnect`。

> 请勿与 Flatpak 版官方 Deskflow 混淆；菜单里应选择 **DeskConnect**。

### Wayland 说明（GNOME）

- 窗口标题栏依赖打包进便携包的 Qt Wayland 装饰插件；请使用上述便携包或 `/opt` 安装。
- Ubuntu 24.04 等发行版的 Input Capture portal 仍为 v1 时，**Wayland 下系统剪贴板可能无法经门户同步**；需要剪贴板时可改用登录选项 **Ubuntu on Xorg**，或升级到支持 Input Capture v2 的门户栈。

## 传文件怎么用

1. 两台电脑都安装并运行**本仓库编出的** DeskConnect（官方 Deskflow 无效）
2. 在 A 的资源管理器中复制文件
3. 把鼠标切到 B
4. 等待日志出现接收完成提示后，在 B 上粘贴

文件会先落到接收目录（默认 `下载/DeskConnect` 或 `Downloads/DeskConnect`），再写入本地剪切板。可在「偏好设置 → Clipboard file transfer」中开关、改目录与大小上限。

> 当前不传文件夹；不兼容手机 / 官方 KDE Connect 传文件协议。

## 目录说明

| 路径 | 说明 |
|------|------|
| `src/`、`cmake/`、`deploy/` 等 | 产品源码（品牌为 DeskConnect） |
| `deploy/linux/make-portable.sh` | Linux 便携包 / `/opt` 安装打包脚本 |
| `kdeconnect-kde/` | KDE Connect 桌面端源码，供后续参考，未接入当前构建 |
| `docs/` | 上游开发文档 |
| `dist/` | 本地打包输出（不入库） |
| `.githooks/` | Git 钩子（提交时自动去掉 Cursor co-author） |

配置与 TLS 证书默认在 `~/.config/DeskConnect/`（证书文件为 `tls/deskconnect.pem`）。

新克隆若要用仓库自带钩子，可执行：`git config core.hooksPath .githooks`（本机已安装到 `.git/hooks/`）。

## 编译

依赖概要：CMake 3.24+、**Qt 6.7+**（建议 6.8）、OpenSSL 3。Linux 还需 libei ≥ 1.3、libportal ≥ 0.10.0，以及可选的 X11 开发库。详见 [`docs/dev/build.md`](docs/dev/build.md)。

### Windows

用 Visual Studio 2022 + Qt 6（MSVC）配置并编译 Release，运行：

```text
build/bin/Release/DeskConnect.exe
```

### Linux

系统自带 Qt 若低于 6.7，请改用自行安装的 Qt（例如 `~/Qt/6.8.3/gcc_64`）：

```bash
# Debian/Ubuntu 基础依赖示例
sudo apt install build-essential cmake ninja-build \
  libssl-dev libx11-dev libxtst-dev \
  libei-dev libportal-dev pkg-config

export CMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j"$(nproc)"

# 打包便携安装包
QTDIR=/path/to/Qt/6.8.x/gcc_64 ./deploy/linux/make-portable.sh
```

开发用二进制一般在 `build/bin/`（`deskflow` GUI 与 `deskflow-core`）。对外品牌名为 DeskConnect，内部二进制名仍可能为 `deskflow*`。

## 许可证

- Deskflow 衍生代码：GPL-2.0（见 `LICENSE`、`LICENSES/`）
- `kdeconnect-kde/`：保留其原有许可证
