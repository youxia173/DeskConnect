/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"
#include "deskflow/MouseTypes.h"

//! Match the configured mouse-locator hotkey / mouse button.
class MouseLocatorTrigger
{
public:
  static bool matchesButton(ButtonID button, KeyModifierMask mask);
  static bool matchesKey(KeyID key, KeyModifierMask mask);
};
