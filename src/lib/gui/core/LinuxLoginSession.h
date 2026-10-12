/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QByteArray>
#include <QString>

namespace deskflow::gui {

QByteArray gdmConfigWithDefaultXorg(const QByteArray &contents);
QString defaultXorgUnavailableReason();
bool configureDefaultXorg(QString &error);

} // namespace deskflow::gui
