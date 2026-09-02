/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect / Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui {

/**
 * @brief Login/boot autostart for the GUI application (Windows Run key,
 * Linux XDG autostart, macOS LaunchAgent).
 */
class Autostart
{
public:
  static bool isSupported();
  static bool isEnabled();
  static bool setEnabled(bool enabled);

private:
  static QString autostartId();
  static QString quotedApplicationPath();
};

} // namespace deskflow::gui
