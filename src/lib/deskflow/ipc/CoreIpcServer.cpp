/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"

#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTextStream>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent) : IpcServer(parent, kCoreIpcName, QStringLiteral("core"))
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  if (command == QStringLiteral("sendFiles")) {
    const QString manifestPath = parts.mid(1).join(QLatin1Char('='));
    if (manifestPath.isEmpty()) {
      writeToClientSocket(clientSocket, QStringLiteral("error=missing manifest"));
      return;
    }

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERR("send files: could not read manifest %s", qPrintable(manifestPath));
      writeToClientSocket(clientSocket, QStringLiteral("error=could not read manifest"));
      return;
    }

    QString peer;
    QStringList paths;
    QTextStream in(&file);
    while (!in.atEnd()) {
      const QString line = in.readLine().trimmed();
      if (line.isEmpty()) {
        continue;
      }
      if (peer.isNull() && line.startsWith(QLatin1String("peer="))) {
        peer = line.mid(5);
        continue;
      }
      const QFileInfo info(line);
      if (!info.exists() || !info.isFile()) {
        LOG_WARN("send files: skipping missing path %s", qPrintable(line));
        continue;
      }
      paths.append(info.absoluteFilePath());
    }
    file.remove();

    if (paths.isEmpty()) {
      writeToClientSocket(clientSocket, QStringLiteral("error=no files to send"));
      return;
    }

    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    Q_EMIT sendFilesRequested(peer, paths);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
