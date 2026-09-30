/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

namespace deskflow::gui {

// Called only by the elevated, short-lived GUI helper process on Windows.
int addWindowsDirectAddress(int interfaceIndex, bool server);

} // namespace deskflow::gui
