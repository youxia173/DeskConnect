# 文件复制和按需粘贴

两台电脑都需要使用包含此修改的版本，并启用剪贴板共享和文件传输。
Ubuntu 使用 Xorg 会话。当前 Ei/Wayland 后端不能提供此功能，会提示使用 Xorg，
不会为了兼容而在复制时提前下载文件。

在源电脑的文件管理器中复制一个或多个文件、文件夹，另一台电脑在桌面或文件夹中粘贴。
复制只保存源路径、发送根名称及随机编号，不读取文件内容。
接收端准备或查询文件清单时，在独立后台任务枚举名称、目录和大小；此步骤不读取文件内容，
不启动文件传输。取消或断开连接不会等待目录扫描完成。
Windows 内容流或 Linux 虚拟文件首次被读取时发出带编号的下载请求。源端使用原有
FileSendSession / FileReceiveSession 传输，沿用进度、限速和取消流程。
完整接收之后，Windows 内容流或 Linux 虚拟文件读取才返回缓存中的字节；文件管理器负责复制到用户选中的位置，
也负责最终目标的同名覆盖提示。成功的接收副本保留，可再次粘贴而不重复传输。

## 协议和生命周期

- 服务端通过选项 `CLFL=1` 广告扩展，客户端回复 `DFOF` JSON hello 后启用。
  不向未协商的旧服务端发送未知控制消息；旧客户端不会收到扩展控制或目录清单。
- `DFOF` 使用 `{v:1,op:...,id:...}`，操作为 offer / request / begin / error / cancel。
  offer 附带 names（只有根名称，没有源电脑的绝对路径）。请求必须匹配当前 offer 编号。
  list / listing 单独交换相对路径及大小清单；不调用文件发送流程，不发送绝对路径。
- begin 后使用现有 DDRG 和 DFTR。DDRG 名称为安全相对路径，目录以 `/` 结尾，
  目录对应大小为 0 的 Start/End，不创建伪文件。父目录必须出现在子项之前。
- 每次接收创建独立的 `DeskConnect-XXXXXX` 目录。拒绝目录穿越、设备名、大小写冲突、
  文件作为父目录等清单；失败在后台删除该次不完整目录。成功只向剪贴板交付根路径。
- 重新复制、断线及退出使旧回调失效。回调保留独立状态，避免等待期间连接对象销毁后访问它。
  等待继续分发键鼠、网络、心跳和取消事件；连续 60 秒无传输进展会失败并取消远端发送。
- Windows 使用 `IDataObject` + `IDataObjectAsyncCapability`，FILEGROUPDESCRIPTORW、
  FILECONTENTS/IStream 和 COPY drop effect，不再发布会被预读的 CF_HDROP。
  枚举格式、读取描述符、取得流、Stat 和 Seek 都不会下载；首次 Read/CopyTo 才下载。
  描述符只包含原相对路径，不包含内部缓存父目录。
- X11 提供 text/uri-list 和 x-special/gnome-copied-files，后者明确使用 copy 操作。
  路径指向 FUSE 3 只读虚拟文件，SelectionRequest 只返回路径，绝不拉取文件内容。
  getattr / readdir / open 只返回名称、目录和大小，首次非空 read 才通知核心线程走原传输流程。
  FUSE 多线程处理元数据和读取，网络回调留在核心事件线程，防止文件读取与剪贴板查询互相等待。
  剪贴板管理器接管相同 URI 时保留挂载；新复制、断线和退出时唤醒读取者并在后台卸载。
  PRIMARY 更新和切屏缓存不覆盖待粘贴的文件 offer。

## 边界和验证

目前支持普通文件、普通目录（包含空目录、隐藏项、中文名称）。符号链接、Windows
reparse point、不兼容的文件名以及超出配置大小/65535 项上限的树会失败；不静默跳过。
Windows 描述符的相对路径必须短于 260 个 UTF-16 字符，序列化清单须不超过 1 MiB。
源文件在粘贴前必须仍存在且可读。目录树在请求时生成，随后改变或删除文件可能导致传输失败。
剪贴板粘贴缓存位于应用 CacheLocation/clipboard-files 下，占用本机磁盘，
可在不再需要重复粘贴时自行清理。内部随机目录用于隔离传输，不作为粘贴项，
也不弹出普通“收到文件”的接收目录通知；普通菜单传输仍使用配置的接收目录。

Windows 触发依据是读取文件内容流，并非拦截 Ctrl+V，因此兼容右键粘贴。
只探测文件清单的剪贴板工具不会下载；主动读取内容流的第三方程序仍会触发下载。
X11 的 URI、stat、目录枚举和打开文件均不下载，真正读取虚拟文件内容时才下载。
主动读取文件内容的缩略图/预览程序仍会触发下载；这里使用文件内容读取作为触发依据，
不是拦截键盘或菜单。普通剪贴板历史工具只保存 URI 时不会下载。
支持异步提取的 Windows Shell 可在后台等待；其他文件管理器自身可能有剪贴板请求超时。

已补充 TransferTests 的源代码：复制不传输、旧编号拒绝、文件夹树/空目录/多根项、
URI 路径往返、安全清单和失败清理。新增 Windows 回归用例覆盖预读描述符/流的
Stat/Seek 不下载、首次 Read 下载、再次粘贴复用缓存，以及原目录名不附带缓存目录。按照用户要求，本次没有编译，也没有运行这些测试。
后续编译和双机验收重点：

1. Windows → Windows 优先验收，两个方向各测 Ctrl+C/V 和右键复制/粘贴；再测跨平台。
2. 复制后可出现 advertised / metadata listed，不能出现 sending 或内容下载；
   在另一台电脑桌面/文件夹粘贴后出现 Windows consumer started reading 和正常传输进度。
3. 单文件、多文件、中文目录、多层空目录、零字节文件及长于 30 秒的大文件传输。
4. 粘贴到桌面、不同文件夹，重复粘贴应复用已接收副本，并由文件管理器处理同名文件。
5. 传输中取消、断线、重新复制文字/图片/另一批文件，确认键鼠继续工作，旧回调不覆盖新剪贴板。
6. 新旧版本混用：文字图片和键鼠继续工作；目录发送明确要求升级两端。

Windows 异步提取参考：[Microsoft IDataObjectAsyncCapability](https://learn.microsoft.com/en-us/windows/win32/api/shldisp/nn-shldisp-idataobjectasynccapability)。
GNOME 文件剪贴板格式参考：[Qt QClipboard](https://doc.qt.io/qt-6/qclipboard.html)。


## X11 文字/图片不同步的回归检查

切屏前读取到的新内容现在通过正常发布流程发送，不能只更新服务端缓存。
X11 首次读取失败/空结果只在当次 open 中缓存，下次 open 重新读取；不同 owner
即使返回相同时间戳，也不能沿用旧数据。SelectionNotify 必须匹配请求窗口、
selection、target 和时间，迟到的旧回复不能结束新的读取。
已有 TARGETS 清单时仅请求所列格式，避免对普通文字逐个探测图片/文件格式。

日志级别 INFO 下确认：
- 启动：`X11 clipboard backend` 中的 display/session/role，以及共享开关和大小限制。
- Ubuntu 复制：`detected external CLIPBOARD change` → `pushing clipboard 0`。
- Windows 复制：`sending local clipboard 0` → 服务端 `updated clipboard 0`。
- 对端接收写入：`applied clipboard 0 to the local system clipboard`；失败则显示 warning。
- X11 读取失败显示 selection/target/字节数/耗时；监视状态每 30 秒记录一次，无正文。

双机各复制一条新的文字并立即切屏粘贴，再测图片；若仍失败，保留两端从启动到
复制失败后的日志。已添加迟到 SelectionNotify 的回归测试源码；本次未编译、未运行测试。


## Ubuntu 按需文件粘贴的构建与验收

Linux X11 构建默认启用 `ENABLE_X11_FILE_PASTE=ON`，新增 `fuse3 >= 3.10`
开发依赖。Ubuntu 构建环境安装 `libfuse3-dev`，运行环境需有 `fuse3`（含 fusermount3）
及可用的 `/dev/fuse`。依赖已加入 CI 安装清单。缺少开发依赖时 CMake 会明确失败；
显式关闭此构建选项后，文字/图片仍可同步，Linux 文件粘贴会提示不支持，绝不改为提前下载。
本次未安装依赖、未编译，也未运行测试。

挂载点在用户 RuntimeLocation/deskconnect-clipboard 下，按 offer 隔离；剪贴板中只提供
原文件或原目录路径，不把 offer 目录作为复制项。缓存仍在独立 CacheLocation 下。
无法挂载时日志提示检查 FUSE 3，文件管理器不会拿到伪造的已下载路径。

优先验收 Windows 客户端 → Ubuntu X11 服务端：
1. Windows Ctrl+C 多个文件/目录，Ubuntu 剪贴板管理器读取 URI、查询大小，不能出现发送进度。
2. Ubuntu 文件夹或桌面 Ctrl+V / 右键粘贴后才出现 `X11 clipboard content read` 和正常文件传输。
3. 检查中文、多层目录、空目录、原文件名，没有额外目录外壳；重复粘贴复用已下载内容。
4. 传输期间移动键鼠，重新复制或断线，不能卡住主事件线程；退出后挂载点应卸载。

新增 `XWindowsClipboardFilesTests` 集成测试源码，验证 URI/元数据查询不下载、首次读取触发、
重复读取复用缓存、卸载清理。测试需要允许用户 FUSE 挂载的 Linux 环境；没有设备或
fusermount3 时明确跳过，设备存在但挂载失败时测试失败。
参考：[libfuse 文件系统操作](https://libfuse.github.io/doxygen/structfuse__operations.html)。
