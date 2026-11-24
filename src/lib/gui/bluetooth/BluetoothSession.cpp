// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "BluetoothSession.h"
#include "common/Settings.h"

#include <QFileInfo>
#include <QSettings>

namespace deskflow::gui::bluetooth {
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
  m_settings = std::make_unique<QTemporaryFile>(dir + QStringLiteral("/.bluetooth-XXXXXX.ini"));
  if (!m_settings->open()) {
    error = m_settings->errorString();
    clear();
    return false;
  }
  m_settings->close();
  QString serverPath;
  if (server) {
    QFile original(source.serverConfigFile);
    m_serverConfig = std::make_unique<QTemporaryFile>(dir + QStringLiteral("/.bluetooth-XXXXXX.conf"));
    if (!original.open(QIODevice::ReadOnly) || original.size() > 4 * 1024 * 1024 || !m_serverConfig->open()) {
      error = QObject::tr("无法为蓝牙会话读取或创建服务端配置。");
      clear();
      return false;
    }
    // Config::readSectionOptions reads sharing/heartbeat from settings. Ensure
    // even a custom config with no options section loads these safety overrides.
    const auto config = original.readAll() + QByteArray("\nsection: options\nend\n");
    if (m_serverConfig->write(config) != config.size() || !m_serverConfig->flush()) {
      error = m_serverConfig->errorString();
      clear();
      return false;
    }
    serverPath = m_serverConfig->fileName();
    m_serverConfig->close();
  }
  bool saved;
  {
    QSettings runtime(m_settings->fileName(), QSettings::IniFormat);
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
      runtime.setValue(Settings::Server::ExternalConfigFile, serverPath);
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
