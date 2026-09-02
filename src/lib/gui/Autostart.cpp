/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect / Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Autostart.h"

#include "common/Constants.h"
#include "common/PlatformInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

namespace deskflow::gui {
namespace {

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
QString linuxDesktopPath()
{
  return QStringLiteral("%1/.config/autostart/%2.desktop").arg(QDir::homePath(), kAppId);
}
#endif

#if defined(Q_OS_MACOS)
QString macPlistPath()
{
  return QStringLiteral("%1/Library/LaunchAgents/%2.plist").arg(QDir::homePath(), kRevFqdnName);
}
#endif

} // namespace

QString Autostart::autostartId()
{
  return QString::fromUtf8(kAppName);
}

QString Autostart::quotedApplicationPath()
{
  const auto path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  if (path.contains(QLatin1Char(' '))) {
    return QStringLiteral("\"%1\"").arg(path);
  }
  return path;
}

bool Autostart::isSupported()
{
  if (deskflow::platform::isSandboxed()) {
    return false;
  }
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || \
    defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  return true;
#else
  return false;
#endif
}

bool Autostart::isEnabled()
{
  if (!isSupported()) {
    return false;
  }

#if defined(Q_OS_WIN)
  QSettings runKey(
      QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat
  );
  return runKey.contains(autostartId());
#elif defined(Q_OS_MACOS)
  return QFile::exists(macPlistPath());
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  QFile file(linuxDesktopPath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  const auto contents = QString::fromUtf8(file.readAll());
  return !contents.contains(QStringLiteral("Hidden=true"), Qt::CaseInsensitive) &&
         !contents.contains(QStringLiteral("X-GNOME-Autostart-enabled=false"), Qt::CaseInsensitive);
#else
  return false;
#endif
}

bool Autostart::setEnabled(bool enabled)
{
  if (!isSupported()) {
    return false;
  }

#if defined(Q_OS_WIN)
  QSettings runKey(
      QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat
  );
  if (enabled) {
    runKey.setValue(autostartId(), quotedApplicationPath());
  } else {
    runKey.remove(autostartId());
  }
  runKey.sync();
  return runKey.status() == QSettings::NoError;

#elif defined(Q_OS_MACOS)
  const auto path = macPlistPath();
  if (!enabled) {
    return !QFile::exists(path) || QFile::remove(path);
  }

  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  const auto exe = QCoreApplication::applicationFilePath();
  QTextStream out(&file);
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n"
         "<dict>\n"
         "  <key>Label</key>\n"
         "  <string>"
      << kRevFqdnName
      << "</string>\n"
         "  <key>ProgramArguments</key>\n"
         "  <array>\n"
         "    <string>"
      << exe
      << "</string>\n"
         "  </array>\n"
         "  <key>RunAtLoad</key>\n"
         "  <true/>\n"
         "</dict>\n"
         "</plist>\n";
  return true;

#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  const auto path = linuxDesktopPath();
  if (!enabled) {
    return !QFile::exists(path) || QFile::remove(path);
  }

  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  QTextStream out(&file);
  out << "[Desktop Entry]\n"
         "Type=Application\n"
         "Version=1.0\n"
         "Name="
      << kAppName
      << "\n"
         "Comment="
      << kAppDescription
      << "\n"
         "Exec="
      << quotedApplicationPath()
      << "\n"
         "Icon="
      << kRevFqdnName
      << "\n"
         "Terminal=false\n"
         "Categories=Utility;Network;\n"
         "X-GNOME-Autostart-enabled=true\n"
         "Hidden=false\n";
  return true;

#else
  Q_UNUSED(enabled);
  return false;
#endif
}

} // namespace deskflow::gui
