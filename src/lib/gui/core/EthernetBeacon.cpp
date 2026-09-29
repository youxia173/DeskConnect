/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EthernetBeacon.h"

#include "common/Settings.h"
#include "gui/core/NetworkMonitor.h"

#include <QTimer>
#include <QUdpSocket>

namespace deskflow::gui {

EthernetBeacon::EthernetBeacon(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
  m_timer->setInterval(1000);
  connect(m_timer, &QTimer::timeout, this, &EthernetBeacon::tick);
}

EthernetBeacon::~EthernetBeacon()
{
  stop();
}

void EthernetBeacon::start(bool server)
{
  const bool roleChanged = m_running && m_server != server;
  m_server = server;
  if (m_running && !roleChanged) {
    tick();
    return;
  }
  if (roleChanged) {
    stop();
  }
  m_running = true;
  m_timer->start();
  tick();
}

void EthernetBeacon::stop()
{
  m_running = false;
  m_timer->stop();
  if (m_send) {
    m_send->close();
    m_send->deleteLater();
    m_send = nullptr;
  }
  if (m_listen) {
    m_listen->close();
    m_listen->deleteLater();
    m_listen = nullptr;
  }
  m_localIp.clear();
}

void EthernetBeacon::tick()
{
  if (!m_running) {
    return;
  }

  const auto nic = NetworkMonitor::ethernetNic();
  if (!nic) {
    if (!m_localIp.isEmpty()) {
      m_localIp.clear();
      Q_EMIT localNicChanged(QString());
    }
    return;
  }

  if (m_localIp != nic->ip) {
    m_localIp = nic->ip;
    m_prefixLength = nic->prefixLength;
    if (m_send) {
      m_send->close();
      m_send->deleteLater();
      m_send = nullptr;
    }
    Q_EMIT localNicChanged(m_localIp);
  }

  if (m_server) {
    if (!m_send) {
      m_send = new QUdpSocket(this);
      if (!m_send->bind(QHostAddress(m_localIp), 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        m_send->deleteLater();
        m_send = nullptr;
        return;
      }
    }
    const auto port = Settings::value(Settings::Core::Port).toInt();
    const auto payload = QStringLiteral("DC1 %1 %2").arg(m_localIp).arg(port).toUtf8();
    m_send->writeDatagram(payload, nic->broadcast, kDiscoveryPort);
    if (nic->broadcast != QHostAddress::Broadcast) {
      m_send->writeDatagram(payload, QHostAddress::Broadcast, kDiscoveryPort);
    }
    return;
  }

  if (!m_listen) {
    m_listen = new QUdpSocket(this);
    connect(m_listen, &QUdpSocket::readyRead, this, &EthernetBeacon::readDatagrams);
    if (!m_listen->bind(
            QHostAddress::AnyIPv4, kDiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint
        )) {
      m_listen->deleteLater();
      m_listen = nullptr;
    }
  }
}

void EthernetBeacon::readDatagrams()
{
  if (!m_listen) {
    return;
  }
  while (m_listen->hasPendingDatagrams()) {
    QByteArray buffer;
    buffer.resize(static_cast<int>(m_listen->pendingDatagramSize()));
    QHostAddress sender;
    m_listen->readDatagram(buffer.data(), buffer.size(), &sender);
    if (!buffer.startsWith("DC1 ")) {
      continue;
    }
    const auto parts = QString::fromUtf8(buffer).trimmed().split(QLatin1Char(' '));
    if (parts.size() != 3) {
      continue;
    }
    const QHostAddress announced(parts.at(1));
    bool portOk = false;
    const int port = parts.at(2).toInt(&portOk);
    if (!portOk || port < 1 || port > 65535 || announced.protocol() != QHostAddress::IPv4Protocol) {
      continue;
    }
    QHostAddress senderV4 = sender;
    if (sender.protocol() == QHostAddress::IPv6Protocol) {
      senderV4 = QHostAddress(sender.toIPv4Address());
    }
    if (senderV4.protocol() != QHostAddress::IPv4Protocol || senderV4.toIPv4Address() != announced.toIPv4Address()) {
      continue;
    }
    if (announced.toString() == m_localIp) {
      continue;
    }
    if (!NetworkMonitor::sameEthernetLink(m_localIp, announced.toString(), m_prefixLength)) {
      continue;
    }
    Q_EMIT peerFound(announced.toString());
  }
}

} // namespace deskflow::gui
