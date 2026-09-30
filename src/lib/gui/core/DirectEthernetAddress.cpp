/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DirectEthernetAddress.h"

#include "NetworkMonitor.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <netioapi.h>

namespace deskflow::gui {

int addWindowsDirectAddress(int interfaceIndex, bool server)
{
  if (interfaceIndex <= 0)
    return ERROR_INVALID_PARAMETER;

  MIB_UNICASTIPADDRESS_ROW address{};
  InitializeUnicastIpAddressEntry(&address);
  address.InterfaceIndex = static_cast<NET_IFINDEX>(interfaceIndex);
  address.Address.Ipv4.sin_family = AF_INET;
  address.OnLinkPrefixLength = 16;
  const auto ip = (server ? NetworkMonitor::directServerIp() : NetworkMonitor::directClientIp()).toStdWString();
  if (InetPtonW(AF_INET, ip.c_str(), &address.Address.Ipv4.sin_addr) != 1)
    return ERROR_INVALID_PARAMETER;

  // This adds a temporary secondary address without changing DHCP or the
  // adapter's saved settings. The caller has already requested UAC elevation.
  const auto result = CreateUnicastIpAddressEntry(&address);
  const bool created = result == NO_ERROR;
  if (!created && result != ERROR_OBJECT_ALREADY_EXISTS)
    return static_cast<int>(result);

  // Windows may expose the address while duplicate-address detection is still
  // running. Do not let the server bind until it is usable.
  for (int attempt = 0; attempt < 50; ++attempt) {
    auto current = address;
    if (GetUnicastIpAddressEntry(&current) == NO_ERROR) {
      if (current.OnLinkPrefixLength != 16) {
        if (created)
          DeleteUnicastIpAddressEntry(&address);
        return ERROR_INVALID_PARAMETER;
      }
      if (current.DadState == IpDadStatePreferred)
        return 0;
      if (current.DadState == IpDadStateDuplicate) {
        if (created)
          DeleteUnicastIpAddressEntry(&address);
        return ERROR_DUP_NAME;
      }
    }
    Sleep(100);
  }
  if (created)
    DeleteUnicastIpAddressEntry(&address);
  return ERROR_TIMEOUT;
}

} // namespace deskflow::gui
