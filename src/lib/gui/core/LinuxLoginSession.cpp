/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LinuxLoginSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace deskflow::gui {

namespace {
const QString kGdmConfig = QStringLiteral("/etc/gdm3/custom.conf");
}

QByteArray gdmConfigWithDefaultXorg(const QByteArray &contents)
{
  const QByteArray newline = contents.contains("\r\n") ? QByteArray("\r\n") : QByteArray("\n");
  const QRegularExpression section(QStringLiteral("^\\s*\\[([^\\]]+)\\]\\s*(?:[#;].*)?$"));
  const QRegularExpression setting(QStringLiteral("^\\s*[#;]?\\s*WaylandEnable\\s*="));
  QByteArray result;
  bool inDaemon = false;
  bool foundDaemon = false;
  bool foundSetting = false;
  const auto appendSetting = [&] {
    if (!result.isEmpty() && !result.endsWith('\n'))
      result += newline;
    result += "WaylandEnable=false";
    result += newline;
  };
  const auto lines = contents.split('\n');
  for (qsizetype i = 0; i < lines.size(); ++i) {
    const auto &line = lines[i];
    // split() produces a final empty item for the existing trailing newline.
    if (i == lines.size() - 1 && line.isEmpty())
      break;
    const auto text = QString::fromUtf8(line).trimmed();
    const auto match = section.match(text);
    if (match.hasMatch()) {
      if (inDaemon && !foundSetting)
        appendSetting();
      inDaemon = match.captured(1) == QStringLiteral("daemon");
      foundDaemon |= inDaemon;
      foundSetting = false;
    }
    if (inDaemon && setting.match(text).hasMatch()) {
      appendSetting();
      foundSetting = true;
    } else {
      result += line;
      if (i < lines.size() - 1)
        result += '\n';
    }
  }
  if (!foundDaemon) {
    if (!result.isEmpty() && !result.endsWith('\n'))
      result += newline;
    result += "[daemon]";
    result += newline;
    appendSetting();
  } else if (inDaemon && !foundSetting) {
    appendSetting();
  }
  return result;
}

QString defaultXorgUnavailableReason()
{
#ifdef Q_OS_LINUX
  QFile displayManager(QStringLiteral("/etc/X11/default-display-manager"));
  if (!displayManager.open(QIODevice::ReadOnly))
    return QStringLiteral("Could not identify the login manager. This action supports Ubuntu with GDM.");
  const auto manager = QFileInfo(QString::fromUtf8(displayManager.readAll()).trimmed()).fileName();
  if (manager != QStringLiteral("gdm3") && manager != QStringLiteral("gdm"))
    return QStringLiteral("The login manager is not GDM. Its configuration was not changed.");
  if (QDir(QStringLiteral("/usr/share/xsessions")).entryList({QStringLiteral("*.desktop")}, QDir::Files).isEmpty())
    return QStringLiteral("No Xorg login session is installed. This Ubuntu version may only support Wayland.");
  if (!QFileInfo(kGdmConfig).isFile() || QFileInfo(kGdmConfig).isSymLink())
    return QStringLiteral("The Ubuntu GDM configuration is missing or is a symbolic link.");
  return {};
#else
  return QStringLiteral("This action is available only on Linux.");
#endif
}

bool configureDefaultXorg(QString &error)
{
#ifdef Q_OS_LINUX
  if (geteuid() != 0) {
    error = QStringLiteral("Administrator authorization is required.");
    return false;
  }
  error = defaultXorgUnavailableReason();
  if (!error.isEmpty())
    return false;
  QLockFile lock(kGdmConfig + QStringLiteral(".deskconnect.lock"));
  if (!lock.tryLock()) {
    error = QStringLiteral("Another login configuration change is in progress.");
    return false;
  }
  QFile original(kGdmConfig);
  if (!original.open(QIODevice::ReadOnly) || original.size() > 1024 * 1024) {
    error = QStringLiteral("Could not read the GDM configuration.");
    return false;
  }
  const auto permissions = original.permissions();
  const auto contents = original.readAll();
  if (original.error() != QFileDevice::NoError) {
    error = original.errorString();
    return false;
  }
  original.close();
  const auto updated = gdmConfigWithDefaultXorg(contents);
  if (updated == contents)
    return true;
  const auto backup = kGdmConfig + QStringLiteral(".deskconnect.bak");
  if (QFileInfo::exists(backup)) {
    const QFileInfo info(backup);
    if (!info.isFile() || info.isSymLink() || info.ownerId() != 0) {
      error = QStringLiteral("The existing GDM backup is not a regular administrator-owned file.");
      return false;
    }
  } else if (!QFile::copy(kGdmConfig, backup)) {
    error = QStringLiteral("Could not back up the GDM configuration.");
    return false;
  }
  QSaveFile output(kGdmConfig);
  if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(permissions) ||
      output.write(updated) != updated.size() || !output.commit()) {
    error = QStringLiteral("Could not save the GDM configuration: %1").arg(output.errorString());
    return false;
  }
  return true;
#else
  error = defaultXorgUnavailableReason();
  return false;
#endif
}

} // namespace deskflow::gui
