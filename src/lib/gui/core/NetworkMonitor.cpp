/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NetworkMonitor.h"

#include <QAbstractSocket>
#include <QList>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <optional>

namespace deskflow::gui {

bool NetworkMonitor::isVirtualInterface(const QString &interfaceName)
{
  // Common virtual network interface patterns
  static const auto virtualRegEx = QRegularExpression(
      QStringLiteral("^vboxnet|vmnet|docker|virbr|veth|br\\-|tun|utun|awdl|p2p|llw|anpi|tap|vEth"),
      QRegularExpression::CaseInsensitiveOption
  );
  return virtualRegEx.match(interfaceName).hasMatch();
}

NetworkMonitor::NetworkMonitor(QObject *parent) : QObject(parent), m_checkTimer(new QTimer(this))
{
  connect(m_checkTimer, &QTimer::timeout, this, &NetworkMonitor::updateNetworkState);
}

void NetworkMonitor::startMonitoring(int intervalMs)
{
  if (m_isMonitoring) {
    return;
  }

  updateNetworkState();

  m_checkTimer->start(intervalMs);
  m_isMonitoring = true;
}

void NetworkMonitor::stopMonitoring()
{
  if (!m_isMonitoring) {
    return;
  }

  m_checkTimer->stop();
  m_isMonitoring = false;
}

QStringList NetworkMonitor::validAddresses()
{
  QList<QHostAddress> physicalIP4;
  QList<QHostAddress> physicalIP6;
  QList<QHostAddress> virtualIP4;
  QList<QHostAddress> virtualIP6;
  QSet<QHostAddress> uniqueAddresses;

  const auto allInterfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : allInterfaces) {
    if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning) ||
        (interface.flags() & QNetworkInterface::IsLoopBack)) {
      continue;
    }

    const bool isP2P = (interface.flags() & QNetworkInterface::IsPointToPoint);
    const bool isVirtualType = interface.type() == QNetworkInterface::Virtual;
    const bool isVirtual = isVirtualInterface(interface.humanReadableName()) || isP2P || isVirtualType;
    const auto addressEntries = interface.addressEntries();

    for (const auto &entry : addressEntries) {
      const QHostAddress address = entry.ip();

      if (address.isLinkLocal() || address.isLoopback() || uniqueAddresses.contains(address)) {
        continue;
      }

      uniqueAddresses.insert(address);

      if (address.protocol() == QHostAddress::IPv6Protocol) {
        if (isVirtual)
          virtualIP6.append(address);
        else
          physicalIP6.append(address);
      } else {
        if (isVirtual)
          virtualIP4.append(address);
        else
          physicalIP4.append(address);
      }
    }
  }

  std::ranges::sort(physicalIP4, [](const QHostAddress &a, const QHostAddress &b) {
    if (a.isPrivateUse() != b.isPrivateUse())
      return a.isPrivateUse();
    return a.toIPv4Address() < b.toIPv4Address();
  });

  std::ranges::sort(virtualIP4, [](const QHostAddress &a, const QHostAddress &b) {
    return a.toIPv4Address() < b.toIPv4Address();
  });

  std::ranges::sort(physicalIP6, [](const QHostAddress &a, const QHostAddress &b) {
    if (a.isPrivateUse() != b.isPrivateUse())
      return a.isPrivateUse();
    return a.toString() < b.toString();
  });

  std::ranges::sort(virtualIP6, [](const QHostAddress &a, const QHostAddress &b) {
    return a.toString() < b.toString();
  });

  auto result = physicalIP4;
  result.append(virtualIP4);
  result.append(physicalIP6);
  result.append(virtualIP6);

  QStringList ipList;
  for (const auto &host : result) {
    ipList.append(host.toString());
  }
  return ipList;
}

namespace {

bool isEthernetNic(const QNetworkInterface &iface)
{
  if (iface.type() == QNetworkInterface::Wifi || iface.type() == QNetworkInterface::Ieee80211 ||
      iface.type() == QNetworkInterface::Loopback || iface.type() == QNetworkInterface::Virtual ||
      iface.type() == QNetworkInterface::Ppp || iface.type() == QNetworkInterface::Slip) {
    return false;
  }
  if (iface.type() == QNetworkInterface::Ethernet) {
    return true;
  }
  static const auto namePattern = QRegularExpression(
      QStringLiteral("以太网|ethernet|\\beth\\d|\\benp|\\benx|\\beno"), QRegularExpression::CaseInsensitiveOption
  );
  const auto label = iface.humanReadableName() + QLatin1Char(' ') + iface.name();
  return namePattern.match(label).hasMatch();
}

bool inPrefix(const QHostAddress &local, const QHostAddress &peer, int prefixLength)
{
  if (local.protocol() != QHostAddress::IPv4Protocol || peer.protocol() != QHostAddress::IPv4Protocol) {
    return false;
  }
  if (prefixLength <= 0 || prefixLength > 32) {
    return false;
  }
  const quint32 mask = prefixLength == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefixLength));
  return (local.toIPv4Address() & mask) == (peer.toIPv4Address() & mask);
}

} // namespace

bool NetworkMonitor::sameEthernetLink(const QString &localIp, const QString &peerIp, int prefixLength)
{
  return inPrefix(QHostAddress(localIp), QHostAddress(peerIp), prefixLength);
}

std::optional<EthernetNic> NetworkMonitor::ethernetNic()
{
  struct Candidate
  {
    EthernetNic nic;
    bool linkLocal = false;
  };
  QList<Candidate> found;

  const auto allInterfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : allInterfaces) {
    if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning) ||
        (interface.flags() & QNetworkInterface::IsLoopBack)) {
      continue;
    }
    if (isVirtualInterface(interface.humanReadableName()) || isVirtualInterface(interface.name()) ||
        !isEthernetNic(interface)) {
      continue;
    }

    for (const auto &entry : interface.addressEntries()) {
      const QHostAddress address = entry.ip();
      if (address.protocol() != QHostAddress::IPv4Protocol || address.isLoopback()) {
        continue;
      }
      EthernetNic nic;
      nic.ip = address.toString();
      const int reportedPrefix = entry.prefixLength();
      if (reportedPrefix > 0 && reportedPrefix < 32) {
        nic.prefixLength = reportedPrefix;
      } else {
        nic.prefixLength = address.isLinkLocal() ? 16 : 24;
      }
      nic.broadcast = entry.broadcast();
      if (nic.broadcast.isNull()) {
        nic.broadcast = QHostAddress::Broadcast;
      }
      found.append(Candidate{nic, address.isLinkLocal()});
    }
  }

  const auto linkLocal = std::find_if(found.cbegin(), found.cend(), [](const Candidate &item) {
    return item.linkLocal;
  });
  if (linkLocal != found.cend()) {
    return linkLocal->nic;
  }
  if (!found.isEmpty()) {
    return found.first().nic;
  }
  return std::nullopt;
}

void NetworkMonitor::setIpAddresses(const QStringList &newAddresses)
{
  if (newAddresses == m_lastAddresses)
    return;
  m_lastAddresses = newAddresses;
  Q_EMIT ipAddressesChanged(m_lastAddresses);
}

void NetworkMonitor::updateNetworkState()
{
  setIpAddresses(validAddresses());
}

} // namespace deskflow::gui
