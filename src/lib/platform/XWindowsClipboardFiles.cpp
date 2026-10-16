/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/XWindowsClipboardFiles.h"
#include "base/IEventQueue.h"
#include "base/Event.h"
#include "base/Log.h"
#include "deskflow/ipc/CoreIpc.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <map>
#include <limits>
#include <algorithm>

#ifdef DESKCONNECT_X11_FILE_PASTE
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

struct XWindowsClipboardFiles::State
{
  Pull pull;
  List list;
  Publish publish;
  std::atomic_bool cancelled{false};
  std::atomic_bool ready{false};
  std::atomic_bool failed{false};
  std::atomic_bool readRequested{false};
  bool initialized = false;
  bool published = false;
  bool fetched = false;
  bool completed = false;
  std::mutex contentMutex;
  std::condition_variable contentReady;
  std::map<std::string, std::string> roots;
  std::vector<std::string> rootNames;
  std::map<std::string, deskflow::FileOffer> entries;
  std::map<std::string, std::vector<std::string>> children;
  std::string mountPath;
#ifdef DESKCONNECT_X11_FILE_PASTE
  std::mutex lifecycle;
  struct fuse *filesystem = nullptr;
  bool unmounted = false;
  static State &current() { return *static_cast<State *>(fuse_get_context()->private_data); }
  static int attributes(const char *path, struct stat *st, struct fuse_file_info *)
  {
    auto &s = current();
    if (s.cancelled) return -ECANCELED;
    auto entry = s.entries.find(path);
    if (entry == s.entries.end()) return -ENOENT;
    *st = {};
    const bool directory = entry->second.name.empty() || entry->second.name.back() == '/';
    st->st_mode = directory ? S_IFDIR | 0500 : S_IFREG | 0400;
    st->st_nlink = directory ? 2 : 1;
    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_size = static_cast<off_t>(entry->second.size);
    return 0;
  }
  static int directory(const char *path, void *buffer, fuse_fill_dir_t fill, off_t offset,
                       struct fuse_file_info *, enum fuse_readdir_flags)
  {
    auto &s = current();
    if (s.cancelled) return -ECANCELED;
    const auto found = s.children.find(path);
    if (found == s.children.end()) return -ENOTDIR;
    if (offset < 0) return -EINVAL;
    const auto &names = found->second;
    for (size_t i = static_cast<size_t>(offset); i < names.size() + 2; ++i) {
      const char *name = i == 0 ? "." : i == 1 ? ".." : names[i - 2].c_str();
      if (fill(buffer, name, nullptr, static_cast<off_t>(i + 1), static_cast<fuse_fill_dir_flags>(0))) break;
    }
    return 0;
  }
  static int openFile(const char *path, struct fuse_file_info *info)
  {
    auto &s = current();
    if (s.cancelled) return -ECANCELED;
    if ((info->flags & O_ACCMODE) != O_RDONLY) return -EROFS;
    const auto entry = s.entries.find(path);
    if (entry == s.entries.end()) return -ENOENT;
    if (entry->second.name.empty() || entry->second.name.back() == '/') return -EISDIR;
    // Opening for metadata or preview must not download anything.
    info->direct_io = 1;
    return 0;
  }
  static int readFile(const char *path, char *buffer, size_t count, off_t offset, struct fuse_file_info *)
  {
    auto &s = current();
    const auto entry = s.entries.find(path);
    if (entry == s.entries.end()) return -ENOENT;
    if (offset < 0) return -EINVAL;
    if (entry->second.name.empty() || entry->second.name.back() == '/') return -EISDIR;
    if (count == 0 || static_cast<uint64_t>(offset) >= entry->second.size) return 0;
    std::unique_lock lock(s.contentMutex);
    s.readRequested = true;
    // Only this filesystem worker waits. The main event thread continues
    // handling input, sockets and cancellation while its normal transfer runs.
    s.contentReady.wait(lock, [&s] { return s.cancelled || s.completed; });
    if (s.cancelled) return -ECANCELED;
    const auto &relative = entry->second.name;
    const auto slash = relative.find('/');
    const auto root = s.roots.find(relative.substr(0, slash));
    if (root == s.roots.end()) return -EIO;
    const std::string local = root->second + (slash == std::string::npos ? "" : relative.substr(slash));
    lock.unlock();
    const int fd = ::open(local.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -errno;
    count = std::min<uint64_t>(count, entry->second.size - static_cast<uint64_t>(offset));
    count = std::min<size_t>(count, std::numeric_limits<int>::max());
    const auto result = pread(fd, buffer, count, offset);
    const auto error = errno;
    ::close(fd);
    if (result < 0) return -error;
    return result == 0 ? -EIO : static_cast<int>(result);
  }
  void run()
  {
    struct fuse *handle = nullptr;
    {
      std::lock_guard lock(lifecycle);
      if (cancelled) return;
      fuse_operations ops{};
      ops.getattr = attributes;
      ops.readdir = directory;
      ops.open = openFile;
      ops.read = readFile;
      char name[] = "deskconnect-clipboard";
      char option[] = "-o";
      char options[] = "ro,nosuid,nodev,noexec,default_permissions,fsname=deskconnect-clipboard";
      char *argv[] = {name, option, options};
      fuse_args args = FUSE_ARGS_INIT(3, argv);
      filesystem = fuse_new(&args, &ops, sizeof(ops), this);
      fuse_opt_free_args(&args);
      if (!filesystem || fuse_mount(filesystem, mountPath.c_str()) != 0) {
        if (filesystem) fuse_destroy(filesystem);
        filesystem = nullptr;
        failed = true;
        return;
      }
      handle = filesystem;
      ready = true;
    }
    // Metadata must remain serviceable while a content reader waits for the
    // core thread; a single-thread loop would deadlock clipboard polling.
    fuse_loop_mt(handle, 0);
    {
      std::lock_guard lock(lifecycle);
      if (!unmounted) fuse_unmount(handle);
      fuse_destroy(handle);
      filesystem = nullptr;
    }
    failed = true;
    // Never recursively remove a mountpoint: remove only the empty directory
    // after unmount, leaving downloaded content in its separate private cache.
    QDir().rmdir(QString::fromStdString(mountPath));
  }
  void unmount()
  {
    std::lock_guard lock(lifecycle);
    if (filesystem && !unmounted) {
      fuse_exit(filesystem);
      fuse_unmount(filesystem);
      unmounted = true;
    }
    if (!filesystem) QDir().rmdir(QString::fromStdString(mountPath));
  }
#endif
};

bool XWindowsClipboardFiles::available()
{
#ifdef DESKCONNECT_X11_FILE_PASTE
  return true;
#else
  return false;
#endif
}

XWindowsClipboardFiles::XWindowsClipboardFiles(IEventQueue *events, Pull pull, List list, Publish publish)
    : m_events(events), m_state(std::make_shared<State>())
{
  m_state->pull = std::move(pull);
  m_state->list = std::move(list);
  m_state->publish = std::move(publish);
  schedule();
}

XWindowsClipboardFiles::~XWindowsClipboardFiles()
{
  clearTimer();
  const auto state = m_state;
  {
    std::lock_guard lock(state->contentMutex);
    state->cancelled = true;
  }
  state->contentReady.notify_all();
#ifdef DESKCONNECT_X11_FILE_PASTE
  // Unmount may wait for a FUSE request. Never hold Linux input capture here.
  try { std::thread([state] { state->unmount(); }).detach(); }
  catch (...) { LOG_ERR("could not schedule virtual clipboard unmount"); }
#endif
}

void XWindowsClipboardFiles::clearTimer()
{
  if (m_timer) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
    m_timer = nullptr;
  }
}
void XWindowsClipboardFiles::schedule()
{
  m_timer = m_events->newOneShotTimer(0.05, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { poll(); });
}
void XWindowsClipboardFiles::poll()
{
  clearTimer();
  const auto state = m_state;
  if (state->cancelled) return;
#ifdef DESKCONNECT_X11_FILE_PASTE
  if (!state->initialized) {
    state->initialized = true;
    const auto list = state->list;
    std::vector<deskflow::FileOffer> entries;
    try { if (list) entries = list(); } catch (...) { entries.clear(); }
    // list()/pull() pump the core queue; this object can be destroyed there.
    if (state->cancelled) return;
    state->entries.emplace("/", deskflow::FileOffer{});
    state->children["/"] = {};
    bool valid = !entries.empty();
    for (const auto &entry : entries) {
      if (!deskflow::isSafeTransferPath(entry.name) || entry.size > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
        valid = false; break;
      }
      const bool directory = entry.name.back() == '/';
      const auto relative = directory ? entry.name.substr(0, entry.name.size() - 1) : entry.name;
      const auto slash = relative.rfind('/');
      const auto parent = slash == std::string::npos ? "/" : "/" + relative.substr(0, slash);
      const auto name = slash == std::string::npos ? relative : relative.substr(slash + 1);
      if (!state->children.contains(parent) || !state->entries.emplace("/" + relative, entry).second) { valid = false; break; }
      state->children[parent].push_back(name);
      if (directory) state->children["/" + relative] = {};
      if (parent == "/") state->rootNames.push_back(name);
    }
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const auto base = QDir(runtime).filePath("deskconnect-clipboard");
    if (!valid || runtime.isEmpty() || !QDir().mkpath(base)) {
      state->failed = true;
    } else {
      QTemporaryDir mount(QDir(base).filePath("offer-XXXXXX"));
      if (!mount.isValid()) {
        state->failed = true;
      } else {
        mount.setAutoRemove(false);
        state->mountPath = mount.path().toStdString();
        try { std::thread([state] {
          try { state->run(); }
          catch (...) {
            state->failed = true;
            { std::lock_guard lock(state->contentMutex); state->cancelled = true; }
            state->contentReady.notify_all();
            state->unmount();
          }
        }).detach(); }
        catch (...) { state->failed = true; QDir().rmdir(mount.path()); }
      }
    }
  }
  if (state->failed) {
    LOG_ERR("X11 lazy file clipboard unavailable: check fuse3/fusermount3 and /dev/fuse; no automatic download");
    ipcSendToClient(QStringLiteral("fileTransfer"), QStringLiteral("error|Linux file paste requires working FUSE 3"));
    return;
  }
  if (state->ready && !state->published) {
    state->published = true;
    std::vector<std::string> roots;
    for (const auto &name : state->rootNames) roots.push_back(state->mountPath + "/" + name);
    const auto publish = state->publish;
    publish(roots);
    if (state->cancelled) return;
    LOG_INFO("X11 clipboard virtual files ready; URI/metadata requests do not transfer contents");
  }
  if (state->readRequested && !state->fetched) {
    state->fetched = true;
    LOG_INFO("X11 clipboard content read: starting normal file transfer");
    const auto pull = state->pull;
    std::vector<std::string> roots;
    try { if (pull) roots = pull(); } catch (...) { roots.clear(); }
    if (state->cancelled) return;
    {
      std::lock_guard lock(state->contentMutex);
      for (const auto &root : roots) {
        state->roots[QFileInfo(QString::fromStdString(root)).fileName().toStdString()] = root;
      }
      state->completed = true;
    }
    state->contentReady.notify_all();
  }
#endif
  if (!state->cancelled) schedule();
}
