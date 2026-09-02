/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect / Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "WifiInfo.h"

#include <QProcess>
#include <QRegularExpression>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wlanapi.h>
#endif

namespace deskflow::gui {
namespace {

QString normalizeSsid(QString ssid)
{
  return ssid.trimmed();
}

#if defined(Q_OS_WIN)
QString windowsCurrentSsid()
{
  HANDLE client = nullptr;
  DWORD negotiatedVersion = 0;
  if (WlanOpenHandle(2, nullptr, &negotiatedVersion, &client) != ERROR_SUCCESS) {
    return {};
  }

  QString result;
  PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
  if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces) {
    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
      const auto &info = interfaces->InterfaceInfo[i];
      if (info.isState != wlan_interface_state_connected) {
        continue;
      }

      DWORD dataSize = 0;
      PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
      if (WlanQueryInterface(
              client, &info.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &dataSize,
              reinterpret_cast<PVOID *>(&attrs), nullptr
          ) != ERROR_SUCCESS ||
          !attrs) {
        continue;
      }

      const auto &ssid = attrs->wlanAssociationAttributes.dot11Ssid;
      if (ssid.uSSIDLength > 0) {
        result = QString::fromUtf8(reinterpret_cast<const char *>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
        if (result.contains(QChar::ReplacementCharacter)) {
          result = QString::fromLatin1(reinterpret_cast<const char *>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
        }
      }
      WlanFreeMemory(attrs);
      if (!result.isEmpty()) {
        break;
      }
    }
    WlanFreeMemory(interfaces);
  }

  WlanCloseHandle(client, nullptr);
  return normalizeSsid(result);
}
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
QString linuxCurrentSsid()
{
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("ACTIVE,SSID"), QStringLiteral("dev"), QStringLiteral("wifi")});
  if (process.waitForFinished(1500) && process.exitStatus() == QProcess::NormalExit) {
    const auto lines = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const auto &line : lines) {
      if (line.startsWith(QStringLiteral("yes:"))) {
        return normalizeSsid(line.mid(4));
      }
    }
  }

  process.start(QStringLiteral("iwgetid"), {QStringLiteral("-r")});
  if (process.waitForFinished(1500) && process.exitStatus() == QProcess::NormalExit) {
    return normalizeSsid(QString::fromUtf8(process.readAllStandardOutput()));
  }

  return {};
}
#endif

#if defined(Q_OS_MACOS)
QString macCurrentSsid()
{
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(QStringLiteral("/usr/sbin/networksetup"), {QStringLiteral("-getairportnetwork"), QStringLiteral("en0")});
  if (!process.waitForFinished(1500) || process.exitStatus() != QProcess::NormalExit) {
    return {};
  }

  const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  static const QRegularExpression re(QStringLiteral("^Current Wi-Fi Network:\\s*(.+)$"));
  const auto match = re.match(output);
  if (match.hasMatch()) {
    return normalizeSsid(match.captured(1));
  }
  return {};
}
#endif

} // namespace

QString WifiInfo::currentSsid()
{
#if defined(Q_OS_WIN)
  return windowsCurrentSsid();
#elif defined(Q_OS_MACOS)
  return macCurrentSsid();
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  return linuxCurrentSsid();
#else
  return {};
#endif
}

} // namespace deskflow::gui
