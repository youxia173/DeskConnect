/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

// important: this is used for settings paths on some platforms,
// and must not be a url. qt automatically converts this to reverse domain
// notation (rdn), e.g. org.deskflow
const auto kOrgDomain = QStringLiteral("deskflow.org");

const auto kUrlGitHub = QStringLiteral("https://github.com/youxia173/DeskConnect");
const auto kUrlSourceQuery = QStringLiteral("source=gui");
const auto kUrlApp = kUrlGitHub;
const auto kUrlHelp = kUrlGitHub;
const auto kUrlDownload = QStringLiteral("%1/releases").arg(kUrlGitHub);
const auto kUrlWiki = kUrlGitHub;
const auto kUrlUpdateCheck = QStringLiteral("https://api.%1/version").arg(kOrgDomain);

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
