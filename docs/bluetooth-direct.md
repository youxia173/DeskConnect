# 蓝牙直连开发与合并说明

开发分支：`codex/bluetooth-direct`。这是实验功能，默认关闭。当前实现面向原生 Windows / Linux 桌面包，两台电脑均须运行包含此功能的 DeskConnect。没有把电脑模拟成通用蓝牙 HID 键盘，也没有修改 Android 控制链路。

## 使用方式

1. 在两台电脑的系统蓝牙设置中完成配对；Ubuntu 需要正常运行的 BlueZ 服务。
2. 两端 DeskConnect 选择桌面进程模式，启用 TLS 和证书指纹验证，并关闭网线直连。
3. 打开“编辑 → 蓝牙直连”，填写对方的蓝牙地址并启用。窗口显示本机地址，也可以扫描附近设备；扫描不替代系统配对。
4. 一端选服务端并排列另一台电脑的屏幕，另一端选客户端。服务端先启动，再启动客户端。客户端的 IP 地址框在此模式下不使用。
5. 首次连接核对双方显示的证书指纹。认证成功后，沿用原来的屏幕边缘切换、键盘和鼠标映射。
6. 恢复网络连接：停止 DeskConnect，在蓝牙窗口取消启用，再启动。原来的网络地址、端口、剪切板及文件传输设置保留。

当前蓝牙会话只传键鼠，不共享文本、图片或文件。仅支持一对电脑和默认蓝牙适配器；不支持 Windows 系统服务、登录界面或 UAC 安全桌面的控制。Flatpak、macOS 默认不编入蓝牙后端。

### Linux 与 Windows 同步

Linux 和 Windows 使用同一套蓝牙界面、协议及连接管理代码，两个平台均接入服务端和客户端角色。Ubuntu X11 → Windows、Windows → Ubuntu X11、Linux → Linux 都属于待实测范围，不需要另外维护一套 Linux 分支。Ubuntu 的输入权限仍取决于已有 X11/Wayland 输入后端，蓝牙只替换传输通道。

Linux 发布流程明确开启蓝牙构建，DEB、便携包和 AppImage 都从同一份带蓝牙的 GUI 构建产物打包。宿主系统须启用 BlueZ 蓝牙服务并完成系统配对；DEB 推荐依赖和 Arch 可选依赖也包含 BlueZ。没有把系统蓝牙服务装进 AppImage。

## 实现边界

- `src/lib/gui/bluetooth/` 包含设置窗口、RFCOMM 桥接、帧解析和临时会话配置。
- `CoreProcess` 管理桥接的启动和销毁；`MainWindow` 只增加入口、状态和模式互斥。核心键鼠协议、屏幕切换、Android 和正常 TCP 路径保持共用。
- 链路为“核心 TLS → 本机 127.0.0.1 → RFCOMM → 对端 127.0.0.1 → 核心 TLS”。桥接不解密 TLS，不跳过证书检查。蓝牙 MAC 白名单不能替代证书身份验证。
- 固定服务 UUID：`ef937c10-317f-4c9b-90a1-d7ec206ca001`。帧头为 `DCB`、版本 `1`、类型、两字节大端长度；Data 为 1–4096 字节，Ping/Pong 为四字节序号。未知版本、类型、长度会断开。
- 蓝牙连接建立限时 10 秒，已连接链路每 250 毫秒检查往返心跳；未响应或写队列停滞超过 3 秒时关闭本地 TCP，让原有断线逻辑释放控制。核心会话另启用 1 秒心跳，作为 GUI 阻塞时的独立保护。实际恢复时间还受系统调度及核心断线处理影响，需要双机验证。
- 桥接使用有界读写窗口，不缓存断线期间的输入；重连创建新连接。客户端依靠原有核心连接重试发起新的蓝牙连接，服务端重新接受指定设备。蓝牙适配器重新打开时会尝试恢复监听。
- 临时配置保存在原设置文件旁，证书路径与信任库位置沿用原配置，停止核心后删除。异常结束可能留下 `.bluetooth-*.ini/.conf` 临时文件，其中没有私钥内容。
- 核心服务端端口先用本地 TCP 预留后交给核心绑定；若被其他本机进程抢占，启动失败，不回退到局域网监听。TLS 认证仍是最终访问控制。
- 日志使用 `[Bluetooth]` 前缀记录连接阶段、设备地址、断开原因及超时；不记录键盘内容或 TLS 数据。

Qt 接口依据：[RFCOMM 服务端](https://doc.qt.io/qt-6.8/qbluetoothserver.html)、[异步蓝牙套接字](https://doc.qt.io/qt-6.8/qbluetoothsocket.html)。Windows 不支持 Qt 的蓝牙安全标志，因此必须保留现有 TLS 双向指纹检查。

## 构建及验证

Windows / Linux 默认 `ENABLE_BLUETOOTH=ON`，需要 Qt 的 **Qt Connectivity / Qt6Bluetooth** 开发模块。已有 Qt 安装可通过维护工具添加该模块。Ubuntu 构建依赖是 `qt6-connectivity-dev`；Windows CI 的 Qt 安装增加 `qtconnectivity`。禁用后端可传 `-DENABLE_BLUETOOTH=OFF`，保留原网络功能和关闭已保存蓝牙配置的入口。

Windows 使用 windeployqt 带入 Qt6Bluetooth；Linux portable / DEB / AppImage 路径增加 Qt6Bluetooth 和 libbluetooth，BlueZ 服务由宿主系统提供。Android 构建不受影响。

`BluetoothTests` 覆盖拆包/粘包、任意二进制载荷、边界长度、错误帧、配置隔离、证书路径保留、临时文件清理及失败回滚。测试使用临时目录和配置快照，不访问用户的真实设置。运行方式：

```sh
ctest --test-dir build/src/unittests --output-on-failure -R '^BluetoothTests$'
```

新增 `Bluetooth branch checks` 工作流，在推送本分支、相关 PR 或手动触发时分别验证 Windows/Linux 的 ON/OFF 构建和上述测试。Linux 还会生成便携包，检查启用构建实际链接并打包了 Qt6Bluetooth。便携包与 AppImage 的启动检查同时运行核心和 GUI 的 `--version`，以覆盖仅 GUI 依赖蓝牙库的情况；这类检查不启动键鼠控制，也不能替代双机实测。工作流不发布 Release，也不创建 tag。

本次完成了新增 C++ 文件的 clang-format 20.1.0 检查、修改文件的 `git diff --check`、YAML 的解析检查、Linux 构建和打包脚本的 Bash 语法检查。按要求未在开发电脑编译，也未进行双机蓝牙实测；新增的 C++ 单元测试和 CI 尚未执行，不能视为已经通过。合并前必须补齐：

| 场景 | 验收结果 |
| --- | --- |
| Windows ↔ Windows、Ubuntu X11 ↔ Windows、Linux ↔ Linux，双方轮流做服务端 | 可认证、按屏幕布局切换，键鼠正常 |
| 不连接 Wi-Fi / 网线，仅开启蓝牙 | 正常连接与切换，证明未走局域网 |
| 拒绝证书、未知设备、错误名称 | 不获得键鼠控制；接受正确证书后可恢复 |
| 按住按键或鼠标键时关闭对端蓝牙、退出对端、拔适配器 | 控制返回本机，无残留按键；记录恢复耗时 |
| 恢复蓝牙、休眠唤醒、停止后重启、客户端先启动 | 重连或明确错误，不重放陈旧输入 |
| 高频鼠标移动、连续滚轮、长时间运行 | 无持续积压、失控或内存增长 |
| 停止、取消蓝牙，再连接原局域网/网线直连 | 原地址、证书信任、剪切板、文件和按钮状态正常 |
| 安装包和免安装包，在未装 Qt 的电脑运行 | 蓝牙 DLL/so 齐全；Linux 缺 BlueZ 有明确日志 |

## 合并约定

- 只在功能分支开发，不在验证期间打正式 release tag。
- 提交按“传输模块和接入”“构建、测试及说明”分开，便于审查与回退。
- 合并前更新目标分支并解决冲突，重点审查 `CoreProcess` 生命周期、`MainWindow` 启停状态、`Settings` 键和 CI/打包依赖；避免为解决冲突覆盖主线后续修复。
- 更新分支后重新跑 ON/OFF 四组验证和双机矩阵，再通过 PR 合入；可 squash 成一个功能提交以便整体回退。不要依赖 cherry-pick 单独搬运 GUI 入口而漏掉模块、依赖或测试。
- 如果暂不合入蓝牙，可保持此分支；如果合入后需关闭构建，使用 CMake 开关。运行时默认关闭，老用户升级不会自动切换连接方式。
