# 文件复制和按需粘贴

两台电脑都需要使用包含此修改的版本，并启用剪贴板共享和文件传输。
Ubuntu 使用 Xorg 会话。当前 Ei/Wayland 后端不能提供此功能，会提示使用 Xorg，
不会为了兼容而在复制时提前下载文件。

在源电脑的文件管理器中复制一个或多个文件、文件夹，另一台电脑在桌面或文件夹中粘贴。
复制只保存源路径、发送根名称及随机编号，不读取文件内容、不递归扫描文件夹。
粘贴时的目录清单在独立后台任务生成，取消或断开连接不会等待目录扫描完成。
目标系统请求文件剪贴板数据时才发出带编号的请求。源端在此时枚举目录，使用原有
FileSendSession / FileReceiveSession 传输，沿用进度、限速和取消流程。
完整接收之后，才把本机实际路径交给目标文件管理器；文件管理器负责复制到用户选中的位置，
也负责最终目标的同名覆盖提示。成功的接收副本保留，可再次粘贴而不重复传输。

## 协议和生命周期

- 服务端通过选项 `CLFL=1` 广告扩展，客户端回复 `DFOF` JSON hello 后启用。
  不向未协商的旧服务端发送未知控制消息；旧客户端不会收到扩展控制或目录清单。
- `DFOF` 使用 `{v:1,op:...,id:...}`，操作为 offer / request / begin / error / cancel。
  offer 附带 names（只有根名称，没有源电脑的绝对路径）。请求必须匹配当前 offer 编号。
- begin 后使用现有 DDRG 和 DFTR。DDRG 名称为安全相对路径，目录以 `/` 结尾，
  目录对应大小为 0 的 Start/End，不创建伪文件。父目录必须出现在子项之前。
- 每次接收创建独立的 `DeskConnect-XXXXXX` 目录。拒绝目录穿越、设备名、大小写冲突、
  文件作为父目录等清单；失败在后台删除该次不完整目录。成功只向剪贴板交付根路径。
- 重新复制、断线及退出使旧回调失效。回调保留独立状态，避免等待期间连接对象销毁后访问它。
  等待继续分发键鼠、网络、心跳和取消事件；连续 60 秒无传输进展会失败并取消远端发送。
- Windows 使用 `IDataObject` + `IDataObjectAsyncCapability`，CF_HDROP 和 COPY drop effect。
  不再在 WM_RENDERALLFORMATS 中主动下载远端文件。
- X11 提供 text/uri-list 和 x-special/gnome-copied-files，后者明确使用 copy 操作。
  PRIMARY 更新和切屏缓存不覆盖待粘贴的文件 offer。

## 边界和验证

目前支持普通文件、普通目录（包含空目录、隐藏项、中文名称）。符号链接、Windows
reparse point、不兼容的文件名以及超出配置大小/65535 项上限的树会失败；不静默跳过。
源文件在粘贴前必须仍存在且可读。目录树在请求时生成，随后改变或删除文件可能导致传输失败。
成功缓存位于配置的接收目录下，占用本机磁盘，可在不再需要重复粘贴时自行清理。

触发依据是系统读取文件格式，并非拦截 Ctrl+V，因此兼容右键粘贴。主动读取文件格式的
第三方剪贴板历史/预览工具也可能触发请求；普通 TARGETS/格式枚举不会读取文件内容。
支持异步提取的 Windows Shell 可在后台等待；其他文件管理器自身可能有剪贴板请求超时。

已补充 TransferTests 的源代码：复制不传输、旧编号拒绝、文件夹树/空目录/多根项、
URI 路径往返、安全清单和失败清理。按照用户要求，本次没有编译，也没有运行这些测试。
后续编译和双机验收重点：

1. Ubuntu Xorg → Windows、Windows → Ubuntu Xorg，各测 Ctrl+C/V 和右键复制/粘贴。
2. 复制后观察日志只有 advertised，没有 sending；粘贴后出现 paste requested 和正常传输进度。
3. 单文件、多文件、中文目录、多层空目录、零字节文件及长于 30 秒的大文件传输。
4. 粘贴到桌面、不同文件夹，重复粘贴应复用已接收副本，并由文件管理器处理同名文件。
5. 传输中取消、断线、重新复制文字/图片/另一批文件，确认键鼠继续工作，旧回调不覆盖新剪贴板。
6. 新旧版本混用：文字图片和键鼠继续工作；目录发送明确要求升级两端。

Windows 异步提取参考：[Microsoft IDataObjectAsyncCapability](https://learn.microsoft.com/en-us/windows/win32/api/shldisp/nn-shldisp-idataobjectasynccapability)。
GNOME 文件剪贴板格式参考：[Qt QClipboard](https://doc.qt.io/qt-6/qclipboard.html)。
