# Ubuntu 与 Windows 剪贴板同步排查

2026-10-11 检查的版本为 1.26.28。本机 Windows 的当天日志中，22:07:25 至 22:07:26 等时段出现一秒数百次 `clipboard was updated`。这证明客户端仍在接收剪贴板消息，但仅凭客户端日志无法确认 Ubuntu 当前是 X11 还是 Wayland，也不能证明所有消息的内容相同。

代码中发现并修正了以下问题：

- X11 的 XFixes 与 SelectionClear 通知直接上报所有权变化，绕过内容去重。剪贴板管理器接管收到的数据时，也会被当成本地新复制。现在 CLIPBOARD 的两种通知都通过内容检查；离屏时仍延后读取，避免持有键鼠捕获时进行额外的同步读取。
- X11 的兜底内容检测只认可非空文字，遗漏仅含图片的截图。现在同时认可图片。
- 旧 SelectionClear 可能在程序重新获得所有权后才处理，误清除新收到的数据缓存。现在先确认当前所有者。
- 服务端在读取主机新内容前，就让客户端接管并清空剪贴板。读取失败或只是剪贴板管理器回声时，客户端原有内容也会丢失。现在主机内容读取成功后直接推送数据，重复重试不再推送相同内容；远端主动重新同步仍会应用数据。

新增普通级别日志记录共享开关、服务端下发的大小限制、发送和接收的剪贴板编号及字节数，以及服务端读到文字还是图片。日志不记录剪贴板正文。

## 编译后双机验证

1. 使用 Ubuntu 服务端与 Windows 客户端，确认日志中的共享开关启用且大小限制大于零。分别复制不同的文字，跨屏粘贴，验证两个方向。
2. 分别在两端截图，仅复制图片而不附带文字，跨屏后粘贴到支持图片的应用中。
3. 启用 Ubuntu 剪贴板管理器并重复上述操作。停止复制后，不应持续产生密集的收发日志；主机读取失败时不应清空 Windows 当前剪贴板。
4. Windows 复制后返回 Ubuntu，再复制新的 Ubuntu 内容返回 Windows，确认后复制的内容能够覆盖旧缓存。断开重连后再验证。
5. Ubuntu 若使用 Wayland，检查服务端启动日志里的 portal clipboard 状态。以上 X11 修改不替代 Wayland 会话权限与系统 portal 支持。

本次按要求没有编译或运行双机测试。若仍失败，保留 Ubuntu 从启动到双向复制失败后的完整日志，同时保留 Windows 相同时段的日志；单独的客户端“已更新”记录不能说明系统剪贴板最终是否成功应用。

## 2026-10-12 服务端日志结论

收到的 `deskconnect-core2.log` 确认 Ubuntu 正在运行 1.26.29，使用 Wayland/EI 后端，InputCapture portal 版本为 1。13:22:11.605 明确记录 `portal clipboard unavailable: input capture version 1 cannot enable clipboard`。因此当前系统会话不能通过本程序使用的 InputCapture 剪贴板接口共享文字或图片；前面的 X11 修复不覆盖此会话。键鼠捕获可独立工作，不能用键鼠正常判断剪贴板权限已经授予。

当前可行的排查方式是注销 Ubuntu，在登录界面选择 Xorg 会话，登录后用 `echo $XDG_SESSION_TYPE` 确认输出 `x11`，再启动软件验证双向复制。若继续使用 Wayland，需要系统 portal 及桌面后端支持对应接口；仅更新或重新编译 DeskConnect 不会补齐系统接口。接口约束参考 https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Clipboard.html 。

同一日志的 13:21:53.107、13:22:24.930、13:22:51.540 都记录发送 `/tmp/deskconnect-core2.log` 前文件大小改变并中止。把日志复制到其它目录后成功传来，与持续追加写入触发大小校验的原因吻合；FileSend.cpp 已改为允许追加增长，仅发送原先声明的长度。该改动仍未编译或双机验证。

## 一键设置默认 Xorg 登录

Linux 图形界面的“设置 → 常规”新增“默认使用 Xorg 登录”，服务端和客户端均可使用，Windows/macOS 隐藏此入口。点击后先检查 GDM 登录管理器、Xorg 会话文件和 Ubuntu GDM 配置是否存在，通过 `/usr/bin/pkexec` 请求管理员授权。授权进程只接受固定的 `--deskconnect-default-xorg` 参数，在创建图形界面和检查单实例之前处理。

配置程序把 `/etc/gdm3/custom.conf` 的 `[daemon]` 下 `WaylandEnable` 设为 `false`，保留其他登录配置、注释与换行格式；首次改动前保存 `/etc/gdm3/custom.conf.deskconnect.bak`，已有备份不覆盖，配置使用原权限原子写入。缺少 Xorg、非 GDM、授权失败或保存失败均报告原因。操作直接应用到系统，不受设置窗口“取消”影响；界面已说明是系统配置操作。

成功后由用户保存工作并重启电脑。程序不重启 GDM，也不自动注销或重启电脑。若需要恢复 Wayland，可在 GDM 配置中重新注释 `WaylandEnable=false`，或根据备份恢复对应设置。单元测试覆盖其他键不变、缺少键/章节、冲突键、CRLF 和重复执行；按用户要求尚未编译或运行测试，也未进行 Linux 授权实测。
