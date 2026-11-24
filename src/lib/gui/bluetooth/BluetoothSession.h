// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <QTemporaryFile>
#include <QVariantMap>
#include <memory>

namespace deskflow::gui::bluetooth {

// Session-only overrides. The normal settings, TLS trust store and LAN server
// configuration are never replaced when entering/leaving Bluetooth mode.
class Session
{
public:
  struct Snapshot
  {
    QString settingsFile;
    QString serverConfigFile;
    QVariantMap values;
  };
  bool prepare(bool server, quint16 port, QString &error);
  bool prepare(const Snapshot &source, bool server, quint16 port, QString &error);
  QString settingsFile() const
  {
    return m_settings ? m_settings->fileName() : QString();
  }
  void clear()
  {
    m_settings.reset();
    m_serverConfig.reset();
  }

private:
  std::unique_ptr<QTemporaryFile> m_settings, m_serverConfig;
};
} // namespace deskflow::gui::bluetooth
