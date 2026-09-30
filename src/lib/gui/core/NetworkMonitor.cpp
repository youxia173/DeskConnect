/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NetworkMonitor.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#endif

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
  for (const auto &iface : allInterfaces) {
    if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning) ||
        (iface.flags() & QNetworkInterface::IsLoopBack)) {
      continue;
    }

    const bool isP2P = (iface.flags() & QNetworkInterface::IsPointToPoint);
    const bool isVirtualType = iface.type() == QNetworkInterface::Virtual;
    const bool isVirtual = isVirtualInterface(iface.humanReadableName()) || isP2P || isVirtualType;
    const auto addressEntries = iface.addressEntries();

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

std::optional<QSet<int>> ethernetGatewayIndexes()
{
  QSet<int> result;
#ifdef Q_OS_WIN
  ULONG size = 0;
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW)
    return std::nullopt;
  QByteArray storage(static_cast<qsizetype>(size), 0);
  auto *adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size) != NO_ERROR)
    return std::nullopt;
  for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->FirstGatewayAddress)
      result.insert(static_cast<int>(adapter->IfIndex));
  }
#elif defined(Q_OS_LINUX)
  // Linux keeps the default route here even when NetworkManager supplied it.
  QFile routes(QStringLiteral("/proc/net/route"));
  if (!routes.open(QIODevice::ReadOnly))
    return std::nullopt;
  {
    const auto lines = routes.readAll().split('\n');
    for (const auto &line : lines) {
      const auto fields = line.simplified().split(' ');
      if (fields.size() < 4 || fields[1] != "00000000")
        continue;
      bool valid = false;
      const auto flags = fields[3].toUInt(&valid, 16);
      if (valid && (flags & 1u)) {
        const auto iface = QNetworkInterface::interfaceFromName(QString::fromLocal8Bit(fields[0]));
        if (iface.isValid())
          result.insert(iface.index());
      }
    }
  }
#else
  return std::nullopt;
#endif
  return result;
}

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

QString NetworkMonitor::directServerIp()
{
  return QStringLiteral("169.254.248.1");
}

QString NetworkMonitor::directClientIp()
{
  return QStringLiteral("169.254.248.2");
}

QString NetworkMonitor::ethernetInterfaceName()
{
  const auto interfaces = QNetworkInterface::allInterfaces();
  const auto gatewayIndexes = ethernetGatewayIndexes();
  if (!gatewayIndexes)
    return {};
  QString unaddressed;
  QString linkLocal;
  for (const auto &iface : interfaces) {
    if ((iface.flags() & QNetworkInterface::IsUp) && (iface.flags() & QNetworkInterface::IsRunning) &&
        !(iface.flags() & QNetworkInterface::IsLoopBack) && !isVirtualInterface(iface.name()) &&
        !isVirtualInterface(iface.humanReadableName()) && isEthernetNic(iface) &&
        !gatewayIndexes->contains(iface.index())) {
      bool hasIpv4 = false;
      bool hasNetworkAddress = false;
      for (const auto &entry : iface.addressEntries()) {
        if (entry.ip().protocol() == QHostAddress::IPv4Protocol) {
          hasIpv4 = true;
          if (!entry.ip().isLinkLocal())
            hasNetworkAddress = true;
        } else if (entry.ip().protocol() == QHostAddress::IPv6Protocol && !entry.ip().isLinkLocal()) {
          hasNetworkAddress = true;
        }
      }
      // A normal LAN address means this cable is already attached to a
      // network, even if it has no default gateway.
      if (hasNetworkAddress)
        continue;
      for (const auto &entry : iface.addressEntries()) {
        if (entry.ip() == QHostAddress(directServerIp()) || entry.ip() == QHostAddress(directClientIp()))
          return iface.name();
      }
      if (hasIpv4 && linkLocal.isEmpty())
        linkLocal = iface.name();
      if (!hasIpv4 && unaddressed.isEmpty())
        unaddressed = iface.name();
    }
  }
  if (!linkLocal.isEmpty())
    return linkLocal;
  return unaddressed;
}

std::optional<EthernetNic> NetworkMonitor::ethernetNic(const QString &preferredIp)
{
  const auto selectedInterface = ethernetInterfaceName();
  if (selectedInterface.isEmpty())
    return std::nullopt;
  struct Candidate
  {
    EthernetNic nic;
    bool linkLocal = false;
  };
  QList<Candidate> found;

  const auto allInterfaces = QNetworkInterface::allInterfaces();
  for (const auto &iface : allInterfaces) {
    if (iface.name() != selectedInterface)
      continue;
    if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning) ||
        (iface.flags() & QNetworkInterface::IsLoopBack)) {
      continue;
    }
    if (isVirtualInterface(iface.humanReadableName()) || isVirtualInterface(iface.name()) ||
        !isEthernetNic(iface)) {
      continue;
    }

    for (const auto &entry : iface.addressEntries()) {
      const QHostAddress address = entry.ip();
      if (address.protocol() != QHostAddress::IPv4Protocol || address.isLoopback()) {
        continue;
      }
      EthernetNic nic;
      nic.interfaceName = iface.name();
      nic.ip = address.toString();
      const int reportedPrefix = entry.prefixLength();
      if (reportedPrefix > 0 && reportedPrefix < 32) {
        nic.prefixLength = reportedPrefix;
      } else {
        nic.prefixLength = address.isLinkLocal() ? 16 : 24;
      }
      nic.broadcast = entry.broadcast();
      if (nic.broadcast.isNull()) {
        const quint32 mask = nic.prefixLength == 32 ? 0xffffffffu : (0xffffffffu << (32 - nic.prefixLength));
        nic.broadcast = QHostAddress((address.toIPv4Address() & mask) | ~mask);
      }
      found.append(Candidate{nic, address.isLinkLocal()});
    }
  }

  const auto preferred = std::find_if(found.cbegin(), found.cend(), [&preferredIp](const Candidate &item) {
    return !preferredIp.isEmpty() && item.nic.ip == preferredIp;
  });
  if (preferred != found.cend()) {
    return preferred->nic;
  }
  const auto fixedDirect = std::find_if(found.cbegin(), found.cend(), [](const Candidate &item) {
    return item.nic.ip == NetworkMonitor::directServerIp() || item.nic.ip == NetworkMonitor::directClientIp();
  });
  if (fixedDirect != found.cend()) {
    return fixedDirect->nic;
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
