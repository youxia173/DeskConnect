/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect / Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui {

/**
 * @brief Helpers for reading the currently connected Wi-Fi SSID.
 */
class WifiInfo
{
public:
  /**
   * @return Current Wi-Fi SSID, or empty if unavailable / not on Wi-Fi.
   */
  static QString currentSsid();
};

} // namespace deskflow::gui
