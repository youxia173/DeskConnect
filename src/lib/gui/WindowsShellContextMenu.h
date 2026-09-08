/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui {

//! Register / remove Explorer "Send with DeskConnect" for files (HKCU).
class WindowsShellContextMenu
{
public:
  static bool isSupported();
  static bool isRegistered();
  //! Create or refresh the verb so the command path matches this install.
  static bool setEnabled(bool enabled, QString *errorMessage = nullptr);
};

} // namespace deskflow::gui
