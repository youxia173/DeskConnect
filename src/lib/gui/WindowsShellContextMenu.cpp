/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "WindowsShellContextMenu.h"

#include "common/Constants.h"

#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QSettings>

namespace deskflow::gui {
namespace {

QString shellVerbKey()
{
  return QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\*\\shell\\DeskConnect.SendFile");
}

QString shellCommandKey()
{
  return shellVerbKey() + QStringLiteral("\\command");
}

QString menuLabel()
{
  return QObject::tr("Send with %1").arg(QString::fromUtf8(kAppName));
}

QString commandLine()
{
  const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  return QStringLiteral("\"%1\" --send-file \"%2\"").arg(exe, QStringLiteral("%1"));
}

} // namespace

bool WindowsShellContextMenu::isSupported()
{
#if defined(Q_OS_WIN)
  return true;
#else
  return false;
#endif
}

bool WindowsShellContextMenu::isRegistered()
{
#if !defined(Q_OS_WIN)
  return false;
#else
  QSettings cmd(shellCommandKey(), QSettings::NativeFormat);
  return !cmd.value(QStringLiteral(".")).toString().isEmpty();
#endif
}

bool WindowsShellContextMenu::setEnabled(bool enabled, QString *errorMessage)
{
#if !defined(Q_OS_WIN)
  Q_UNUSED(enabled);
  if (errorMessage) {
    *errorMessage = QObject::tr("Shell context menu is only available on Windows.");
  }
  return false;
#else
  if (!enabled) {
    QSettings verb(shellVerbKey(), QSettings::NativeFormat);
    verb.clear();
    verb.sync();
    // Remove empty parent key tree leftovers.
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\*\\shell"), QSettings::NativeFormat);
    classes.remove(QStringLiteral("DeskConnect.SendFile"));
    classes.sync();
    return true;
  }

  const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  QSettings verb(shellVerbKey(), QSettings::NativeFormat);
  verb.setValue(QStringLiteral("."), menuLabel());
  verb.setValue(QStringLiteral("Icon"), exe + QStringLiteral(",0"));
  // Only show for files (not folders): * already means all-file types.
  verb.sync();

  QSettings cmd(shellCommandKey(), QSettings::NativeFormat);
  cmd.setValue(QStringLiteral("."), commandLine());
  cmd.sync();

  if (cmd.status() != QSettings::NoError || verb.status() != QSettings::NoError) {
    if (errorMessage) {
      *errorMessage = QObject::tr("Failed to write Explorer context menu registry keys.");
    }
    return false;
  }
  return true;
#endif
}

} // namespace deskflow::gui
