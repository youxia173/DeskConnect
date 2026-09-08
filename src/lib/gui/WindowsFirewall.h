/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui {

//! Windows Firewall helpers (inbound allow for core + TCP port).
class WindowsFirewall
{
public:
  static bool isSupported();

  //! Adds/updates inbound allow rules. May show a UAC prompt. Returns false on cancel/failure.
  static bool addAllowRules(int port, QString *errorMessage = nullptr);
};

} // namespace deskflow::gui
