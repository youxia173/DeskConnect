/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class QTimer;
class QUdpSocket;

namespace deskflow::gui {

// Server broadcasts its listen address on the Ethernet NIC. Client listens on that link only.
class EthernetBeacon : public QObject
{
  Q_OBJECT

public:
  static constexpr quint16 kDiscoveryPort = 24801;

  explicit EthernetBeacon(QObject *parent = nullptr);
  ~EthernetBeacon() override;

  void start(bool server);
  void stop();
  [[nodiscard]] bool isRunning() const
  {
    return m_running;
  }

Q_SIGNALS:
  void peerFound(const QString &ip);
  void localNicChanged(const QString &ip);

private:
  void tick();
  void readDatagrams();

  bool m_running = false;
  bool m_server = false;
  QString m_localIp;
  int m_prefixLength = 32;
  QTimer *m_timer = nullptr;
  QUdpSocket *m_send = nullptr;
  QUdpSocket *m_listen = nullptr;
};

} // namespace deskflow::gui
