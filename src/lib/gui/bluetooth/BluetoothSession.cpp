// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "BluetoothSession.h"
#include "common/Settings.h"

#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>

namespace deskflow::gui::bluetooth {
namespace {
bool createSessionFile(const QString &fileTemplate, QString &path, QString &error, const QByteArray &data = {})
{
  QTemporaryFile file(fileTemplate);
  if (!file.open() || file.write(data) != data.size() || !file.flush()) {
    error = file.errorString();
    return false;
  }
  path = file.fileName();
  file.setAutoRemove(false);
  // close() keeps QTemporaryFile's native handle open. Destroy it before
  // QSettings atomically replaces the file; Windows otherwise denies the save.
  // Session now owns the path and removes it on failure, stop or destruction.
  return true;
}
} // namespace

Session::~Session()
{
  clear();
}

void Session::clear()
{
  if (!m_settingsPath.isEmpty())
    QFile::remove(m_settingsPath);
  if (!m_serverConfigPath.isEmpty())
    QFile::remove(m_serverConfigPath);
  m_settingsPath.clear();
  m_serverConfigPath.clear();
}

bool Session::prepare(bool server, quint16 port, QString &error)
{
  Settings::save();
  Snapshot source;
  source.settingsFile = Settings::settingsFile();
  source.serverConfigFile = Settings::serverConfigFile();
  QSettings saved(source.settingsFile, QSettings::IniFormat);
  for (const auto &key : saved.allKeys())
    source.values.insert(key, saved.value(key));
  // Materialize defaults and state values before changing the settings path.
  for (const auto &key : Settings::validKeys())
    source.values.insert(key, Settings::value(key));
  return prepare(source, server, port, error);
}

bool Session::prepare(const Snapshot &source, bool server, quint16 port, QString &error)
{
  clear();
  if (!port) {
    error = QObject::tr("蓝牙本地端口无效。");
    return false;
  }
  // Linux/portable trust stores are relative to the settings directory.
  const auto dir = QFileInfo(source.settingsFile).absolutePath();
  if (!createSessionFile(dir + QStringLiteral("/.bluetooth-XXXXXX.ini"), m_settingsPath, error)) {
    return false;
  }
  if (server) {
    QFile original(source.serverConfigFile);
    if (!original.open(QIODevice::ReadOnly) || original.size() > 4 * 1024 * 1024) {
      error = QObject::tr("无法为蓝牙会话读取或创建服务端配置。");
      clear();
      return false;
    }
    // Config::readSectionOptions reads sharing/heartbeat from settings. Ensure
    // even a custom config with no options section loads these safety overrides.
    const auto config = original.readAll() + QByteArray("\nsection: options\nend\n");
    if (!createSessionFile(dir + QStringLiteral("/.bluetooth-XXXXXX.conf"), m_serverConfigPath, error, config)) {
      clear();
      return false;
    }
  }
  bool saved;
  {
    QSettings runtime(m_settingsPath, QSettings::IniFormat);
    for (auto it = source.values.cbegin(); it != source.values.cend(); ++it)
      runtime.setValue(it.key(), it.value());
    runtime.setValue(Settings::Core::CoreMode, server ? Settings::CoreMode::Server : Settings::CoreMode::Client);
    runtime.setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
    runtime.setValue(Settings::Core::Interface, QStringLiteral("127.0.0.1"));
    runtime.setValue(Settings::Core::Port, port);
    runtime.setValue(Settings::Core::DirectEthernet, false);
    runtime.setValue(Settings::Client::RemoteHost, QStringLiteral("127.0.0.1"));
    runtime.setValue(Settings::Security::TlsEnabled, true);
    runtime.setValue(Settings::Security::CheckPeers, true);
    runtime.setValue(Settings::Server::EnableClipboard, false);
    runtime.setValue(Settings::FileTransfer::Enabled, false);
    runtime.setValue(Settings::Server::EnableHeatbeat, true);
    runtime.setValue(Settings::Server::Heartbeat, 1000);
    if (server) {
      runtime.setValue(Settings::Server::ExternalConfig, true);
      runtime.setValue(Settings::Server::ExternalConfigFile, m_serverConfigPath);
    }
    runtime.sync();
    saved = runtime.status() == QSettings::NoError;
  }
  if (!saved) {
    error = QObject::tr("无法保存蓝牙会话配置。");
    clear();
  }
  return saved;
}
} // namespace deskflow::gui::bluetooth
