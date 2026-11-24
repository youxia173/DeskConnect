// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <QObject>
#include <memory>

namespace deskflow::gui::bluetooth {

// Owns one RFCOMM connection and a loopback TCP endpoint for the existing TLS core.
// All I/O is asynchronous. Destroying/stopping the bridge closes both ends.
class Transport : public QObject
{
  Q_OBJECT
public:
  explicit Transport(QObject *parent = nullptr);
  ~Transport() override;
  static bool available();
  bool start(bool server, const QString &peer, QString &error);
  void stop();
  quint16 port() const;
Q_SIGNALS:
  void status(const QString &message);

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace deskflow::gui::bluetooth
