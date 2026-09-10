/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/KeySequence.h"

#include <QString>

//! Convert between settings binding strings and KeySequence for the mouse locator.
/*!
Binding format (Deskflow ButtonID for mouse, KeyMap names for keys):
- mousebutton(2)           -- middle button
- mousebutton(4)           -- side / back
- keystroke(F8)
- keystroke(Control+Shift+f)
*/
namespace MouseLocatorBinding {

inline constexpr auto kDefault = "mousebutton(2)";

QString fromSequence(const KeySequence &seq);
KeySequence toSequence(const QString &binding);

//! Human-readable label (mouse / special keys localized; letters stay as-is).
QString toDisplayString(const KeySequence &seq);

} // namespace MouseLocatorBinding
