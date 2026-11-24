// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <QVariantMap>

namespace deskflow::gui::bluetooth {

// Session-only overrides. The normal settings, TLS trust store and LAN server
// configuration are never replaced when entering/leaving Bluetooth mode.
class Session
{
public:
  Session() = default;
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

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
    return m_settingsPath;
  }
  void clear();

private:
  QString m_settingsPath, m_serverConfigPath;
};
} // namespace deskflow::gui::bluetooth
