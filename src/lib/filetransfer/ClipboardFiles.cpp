/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "filetransfer/ClipboardFiles.h"
#include "filetransfer/FileTransfer.h"
#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/IPlatformScreen.h"
#include "deskflow/ipc/CoreIpc.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <chrono>

namespace deskflow {
struct ClipboardFiles::Pull
{
  std::string id;
  bool valid = true;
  bool waiting = false;
  bool started = false;
  bool done = false;
  bool success = false;
  std::vector<std::string> paths;
  std::chrono::steady_clock::time_point activity = std::chrono::steady_clock::now();
};

ClipboardFiles::ClipboardFiles(IEventQueue *events, IPlatformScreen *screen, SendControl control, SendFiles files,
                               std::function<void()> cancelSend, std::function<void()> cancelReceive)
    : m_events(events), m_screen(screen), m_control(std::move(control)), m_files(std::move(files)),
      m_cancelSend(std::move(cancelSend)), m_cancelReceive(std::move(cancelReceive))
{
  m_control = [send = std::move(m_control)](const std::string &message) {
    try {
      send(message);
    } catch (...) {
      LOG_WARN("clipboard files: could not send control message (connection closed)");
    }
  };
}

ClipboardFiles::~ClipboardFiles()
{
  if (m_pull) {
    m_pull->valid = false;
  }
}

void ClipboardFiles::control(const char *operation, const std::string &id)
{
  QJsonObject object{{"v", 1}, {"op", operation}, {"id", QString::fromStdString(id)}};
  m_control(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
}

void ClipboardFiles::announce(const std::string &paths, bool force)
{
  if (!force && paths == m_localPaths && !m_pull) {
    return;
  }
  m_localPaths = paths;
  m_localId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  if (m_pull) {
    m_pull->valid = false;
    m_pull.reset();
  }
  QJsonArray names;
  for (const auto &path : pathsFromClipboardData(paths)) {
    const QFileInfo info(QString::fromStdString(path));
    if ((!info.isFile() && !info.isDir()) || info.isSymLink()) {
      continue;
    }
    const auto name = sanitizeFileName(info.fileName().toUtf8().toStdString());
    if (name.empty()) {
      continue;
    }
    names.append(QString::fromStdString(name));
  }
  // A clear offer also invalidates stale remote file copies.
  QJsonObject object{{"v", 1}, {"op", "offer"}, {"id", QString::fromStdString(m_localId)}, {"names", names}};
  auto message = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (names.size() > 65535 || message.size() > 1024 * 1024) {
    LOG_ERR("clipboard files: copied file list exceeds protocol limits");
    m_localPaths.clear();
    object.insert("names", QJsonArray{});
    message = QJsonDocument(object).toJson(QJsonDocument::Compact);
  }
  m_control(message.toStdString());
  LOG_INFO("clipboard files: advertised %lld root(s), no file contents sent", static_cast<long long>(names.size()));
}

void ClipboardFiles::handle(const std::string &message)
{
  if (message.size() > 1024 * 1024) {
    return;
  }
  const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(message));
  if (!document.isObject()) {
    return;
  }
  const auto object = document.object();
  if (object.value("v").toInt() != 1 || !isFileTransferEnabled()) {
    return;
  }
  const auto operation = object.value("op").toString();
  const auto id = object.value("id").toString().toStdString();
  if (id.empty() || id.size() > 64) {
    return;
  }
  if (operation == "cancel" && id == m_sendingId) {
    if (m_cancelSend) {
      m_cancelSend();
    }
    sendFinished(false);
    return;
  }
  if (operation == "request") {
    if (id != m_localId || m_localPaths.empty() || !m_sendingId.empty()) {
      control("error", id);
      return;
    }
    const auto paths = pathsFromClipboardData(m_localPaths);
    m_sendingId = id;
    control("begin", id);
    LOG_INFO("clipboard files: paste requested for offer %s", id.c_str());
    if (!m_files(paths)) {
      sendFinished(false);
    }
    return;
  }
  if (operation == "offer") {
    if (m_pull && m_pull->id == id) {
      return;
    }
    if (m_pull) {
      m_pull->valid = false;
    }
    m_localPaths.clear();
    m_localId.clear();
    const auto names = object.value("names").toArray();
    if (names.isEmpty()) {
      m_pull.reset();
      m_screen->clearDelayedFilePaste();
      return;
    }
    if (names.size() > 65535) {
      return;
    }
    for (const auto &value : names) {
      const auto name = value.toString().toStdString();
      if (name.empty() || sanitizeFileName(name) != name) {
        return;
      }
    }
    m_pull = std::make_shared<Pull>();
    m_pull->id = id;
    const auto pull = m_pull;
    const auto send = m_control;
    const auto cancelReceive = m_cancelReceive;
    auto *events = m_events;
    // Captures deliberately exclude this: disconnect/new copy can destroy the
    // protocol owner during native clipboard rendering and nested dispatch.
    m_screen->prepareDelayedFilePaste([pull, send, events, cancelReceive]() {
      if (!pull->valid || !isFileTransferEnabled()) {
        return std::vector<std::string>{};
      }
      if (pull->done && pull->success) {
        return pull->paths;
      }
      if (pull->waiting) {
        return std::vector<std::string>{};
      }
      pull->waiting = true;
      pull->done = false;
      pull->started = false;
      pull->activity = std::chrono::steady_clock::now();
      QJsonObject request{{"v", 1}, {"op", "request"}, {"id", QString::fromStdString(pull->id)}};
      send(QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString());
      const auto paths = wait(events, pull);
      if (pull->valid && !pull->success) {
        if (cancelReceive) {
          cancelReceive();
        }
        QJsonObject cancel{{"v", 1}, {"op", "cancel"}, {"id", QString::fromStdString(pull->id)}};
        send(QJsonDocument(cancel).toJson(QJsonDocument::Compact).toStdString());
      }
      return paths;
    });
  } else if (m_pull && id == m_pull->id && m_pull->waiting) {
    if (operation == "begin") {
      m_pull->started = true;
      progress();
    } else if (operation == "error") {
      failed();
    }
  }
}

std::vector<std::string> ClipboardFiles::wait(IEventQueue *events, const std::shared_ptr<Pull> &pull)
{
  // This runs on the core event thread. Continue dispatching socket, input,
  // keepalive and cancel events while the native paste waits for real paths.
  while (pull->valid && !pull->done) {
    if (std::chrono::steady_clock::now() - pull->activity > std::chrono::seconds(60)) {
      LOG_ERR("clipboard files: paste timed out (no transfer progress for 60 seconds)");
      pull->done = true;
      break;
    }
    Event event;
    if (!events->getEvent(event, 0.05)) {
      continue;
    }
    if (event.getType() == EventTypes::Quit) {
      events->addEvent(std::move(event));
      pull->valid = false;
      break;
    }
    events->dispatchEvent(event);
    Event::deleteData(event);
  }
  pull->waiting = false;
  if (!pull->valid || !pull->success) {
    ipcSendToClient(QStringLiteral("fileTransfer"), QStringLiteral("error|clipboard paste failed or cancelled"));
    return {};
  }
  return pull->paths;
}

bool ClipboardFiles::waiting() const
{
  return m_pull && m_pull->valid && m_pull->waiting && m_pull->started && !m_pull->done;
}
void ClipboardFiles::progress()
{
  if (waiting()) {
    m_pull->activity = std::chrono::steady_clock::now();
  }
}
void ClipboardFiles::received(const std::vector<std::string> &paths)
{
  if (waiting()) {
    m_pull->paths = paths;
    m_pull->success = !paths.empty();
    m_pull->done = true;
  }
}
void ClipboardFiles::failed()
{
  if (m_pull && m_pull->waiting) {
    m_pull->success = false;
    m_pull->done = true;
  }
}
void ClipboardFiles::cancel()
{
  if (m_pull && m_pull->waiting && !m_pull->done) {
    control("cancel", m_pull->id);
    failed();
  }
}
void ClipboardFiles::sendFinished(bool success)
{
  if (!m_sendingId.empty()) {
    if (!success) {
      control("error", m_sendingId);
    }
    m_sendingId.clear();
  }
}
} // namespace deskflow
