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

## 目录说明

| 路径 | 说明 |
|------|------|
| `src/`、`cmake/`、`deploy/` 等 | 产品源码（品牌为 DeskConnect） |
| `kdeconnect-kde/` | KDE Connect 桌面端源码，供后续参考，未接入当前构建 |
| `docs/` | 上游开发文档 |

## 传文件怎么用

1. 两台电脑都安装并运行**本仓库编出的** DeskConnect（官方 Deskflow 无效）
2. 在 A 的资源管理器中复制文件
3. 把鼠标切到 B
4. 等待日志出现接收完成提示后，在 B 上粘贴

文件会先落到接收目录（默认 `下载/DeskConnect` 或 `Downloads/DeskConnect`），再写入本地剪切板。可在「偏好设置 → Clipboard file transfer」中开关、改目录与大小上限。

> 当前不传文件夹；不兼容手机 / 官方 KDE Connect 传文件协议。

## 编译

依赖概要：CMake 3.24+、Qt 6.7+、OpenSSL 3。Linux 还需 libei、libportal，以及可选的 X11 开发库。详见 [`docs/dev/build.md`](docs/dev/build.md)。

### Windows

用 Visual Studio 2022 + Qt 6（MSVC）配置并编译 Release，运行：

```text
build/bin/Release/DeskConnect.exe
```

### Linux

```bash
# Debian/Ubuntu 示例
sudo apt install build-essential cmake \
  qt6-base-dev libssl-dev \
  libx11-dev libxtst-dev \
  libei-dev libportal-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

产物一般在 `build/bin/`（如 `DeskConnect` / `deskflow` 与 `deskflow-core`）。

## 许可证

- Deskflow 衍生代码：GPL-2.0（见 `LICENSE`、`LICENSES/`）
- `kdeconnect-kde/`：保留其原有许可证
