/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class QTimer;
class QUdpSocket;
class QProcess;

namespace deskflow::gui {

// Server broadcasts its listen address on the Ethernet NIC. Client listens on that link only.
class EthernetBeacon : public QObject
{
  Q_OBJECT

public:
  static constexpr quint16 kDiscoveryPort = 24801;
  static QString serverIp();

  explicit EthernetBeacon(QObject *parent = nullptr);
  ~EthernetBeacon() override;

  void start(bool server);
  void stop();
  void retryAddressSetup();
  [[nodiscard]] bool addressReady() const;
  [[nodiscard]] bool serverAddressReady() const;
  [[nodiscard]] bool isRunning() const
  {
    return m_running;
  }

Q_SIGNALS:
  void peerFound(const QString &ip);
  void localNicChanged(const QString &ip);
  void addressSetupFailed(const QString &reason);

private:
  void tick();
  void readDatagrams();
  void configureAddress(const QString &interfaceName);

  bool m_running = false;
  bool m_server = false;
  QString m_localIp;
  int m_prefixLength = 32;
  QTimer *m_timer = nullptr;
  QUdpSocket *m_send = nullptr;
  QUdpSocket *m_listen = nullptr;
  QProcess *m_addressProcess = nullptr;
  QString m_setupAttemptedInterface;
  bool m_addressSetupFailed = false;
};

} // namespace deskflow::gui
