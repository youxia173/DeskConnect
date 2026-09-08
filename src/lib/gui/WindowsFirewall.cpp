/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "WindowsFirewall.h"

#include "common/Constants.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif

namespace deskflow::gui {

bool WindowsFirewall::isSupported()
{
#if defined(Q_OS_WIN)
  return true;
#else
  return false;
#endif
}

bool WindowsFirewall::addAllowRules(int port, QString *errorMessage)
{
#if !defined(Q_OS_WIN)
  Q_UNUSED(port);
  if (errorMessage) {
    *errorMessage = QObject::tr("Firewall rules are only available on Windows.");
  }
  return false;
#else
  if (port < 1 || port > 65535) {
    if (errorMessage) {
      *errorMessage = QObject::tr("Invalid port: %1").arg(port);
    }
    return false;
  }

  const auto corePath = QDir::toNativeSeparators(
      QCoreApplication::applicationDirPath() + QStringLiteral("/deskflow-core.exe")
  );
  if (!QFileInfo::exists(corePath)) {
    if (errorMessage) {
      *errorMessage = QObject::tr("Could not find deskflow-core.exe next to the application.");
    }
    return false;
  }

  const auto ruleCore = QStringLiteral("%1 Core").arg(QString::fromUtf8(kAppName));
  const auto rulePort = QStringLiteral("%1 TCP Port").arg(QString::fromUtf8(kAppName));

  const auto batPath = QDir::toNativeSeparators(QDir::temp().filePath(QStringLiteral("deskconnect-firewall.cmd")));
  QFile bat(batPath);
  if (!bat.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    if (errorMessage) {
      *errorMessage = QObject::tr("Could not write temporary firewall script.");
    }
    return false;
  }

  // Escape quotes for cmd: program path may contain spaces.
  const auto quotedCore = QStringLiteral("\"%1\"").arg(corePath);
  QByteArray script;
  script += "@echo off\r\n";
  script += "netsh advfirewall firewall delete rule name=\"" + ruleCore.toLocal8Bit() + "\" >nul 2>&1\r\n";
  script += "netsh advfirewall firewall delete rule name=\"" + rulePort.toLocal8Bit() + "\" >nul 2>&1\r\n";
  script += "netsh advfirewall firewall add rule name=\"" + ruleCore.toLocal8Bit() +
            "\" dir=in action=allow program=" + quotedCore.toLocal8Bit() + " enable=yes profile=any\r\n";
  script += "if errorlevel 1 exit /b 1\r\n";
  script += "netsh advfirewall firewall add rule name=\"" + rulePort.toLocal8Bit() +
            "\" dir=in action=allow protocol=TCP localport=" + QByteArray::number(port) +
            " enable=yes profile=any\r\n";
  script += "if errorlevel 1 exit /b 1\r\n";
  script += "exit /b 0\r\n";
  bat.write(script);
  bat.close();

  const std::wstring batW = batPath.toStdWString();
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";
  sei.lpFile = batW.c_str();
  sei.nShow = SW_HIDE;

  if (!ShellExecuteExW(&sei)) {
    const DWORD err = GetLastError();
    QFile::remove(batPath);
    if (errorMessage) {
      if (err == ERROR_CANCELLED) {
        *errorMessage = QObject::tr("Administrator approval was cancelled.");
      } else {
        *errorMessage = QObject::tr("Failed to start elevated firewall helper (error %1).").arg(err);
      }
    }
    return false;
  }

  DWORD exitCode = 1;
  if (sei.hProcess) {
    WaitForSingleObject(sei.hProcess, 60000);
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
  }
  QFile::remove(batPath);

  if (exitCode != 0) {
    if (errorMessage) {
      *errorMessage = QObject::tr(
                          "Failed to add firewall rules (exit code %1). "
                          "Try running DeskConnect as administrator."
      )
                          .arg(exitCode);
    }
    return false;
  }
  return true;
#endif
}

} // namespace deskflow::gui
