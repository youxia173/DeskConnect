/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QApplication>
#include <QDir>
#include <QFileInfoList>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <dwmapi.h>
#include <windows.h>
#endif

#include "common/Constants.h"
#include "common/Settings.h"

namespace deskflow::gui {

inline QString themeId()
{
  const auto theme = Settings::value(Settings::Gui::Theme).toString().toLower();
  // Legacy "cyber" maps to dark.
  if (theme == QLatin1String("dark") || theme == QLatin1String("cyber")) {
    return QStringLiteral("dark");
  }
  return QStringLiteral("light");
}

inline bool isDarkMode()
{
  return themeId() == QLatin1String("dark");
}

inline QString iconMode()
{
  return isDarkMode() ? QStringLiteral("dark") : QStringLiteral("light");
}

inline void updateIconTheme()
{
  const auto themeName = QStringLiteral("%1-%2").arg(kAppId, iconMode());
  if (QIcon::themeName().isEmpty() || QIcon::themeName().startsWith(kAppId))
    QIcon::setThemeName(themeName);
  else
    QIcon::setFallbackThemeName(themeName);
  QIcon::setFallbackSearchPaths({QStringLiteral(":/icons/%1").arg(themeName)});
}

inline QPalette lightPalette()
{
  QPalette p;
  p.setColor(QPalette::Window, QColor(245, 245, 245));
  p.setColor(QPalette::WindowText, QColor(30, 30, 30));
  p.setColor(QPalette::Base, QColor(255, 255, 255));
  p.setColor(QPalette::AlternateBase, QColor(235, 235, 235));
  p.setColor(QPalette::Text, QColor(30, 30, 30));
  p.setColor(QPalette::Button, QColor(240, 240, 240));
  p.setColor(QPalette::ButtonText, QColor(30, 30, 30));
  p.setColor(QPalette::Highlight, QColor(0, 120, 215));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
  p.setColor(QPalette::ToolTipText, Qt::black);
  p.setColor(QPalette::Link, QColor(0, 100, 200));
  p.setColor(QPalette::PlaceholderText, QColor(120, 120, 120));
  return p;
}

inline QPalette darkPalette()
{
  QPalette p;
  p.setColor(QPalette::Window, QColor(45, 45, 45));
  p.setColor(QPalette::WindowText, QColor(220, 220, 220));
  p.setColor(QPalette::Base, QColor(35, 35, 35));
  p.setColor(QPalette::AlternateBase, QColor(55, 55, 55));
  p.setColor(QPalette::Text, QColor(220, 220, 220));
  p.setColor(QPalette::Button, QColor(55, 55, 55));
  p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
  p.setColor(QPalette::Highlight, QColor(42, 130, 218));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
  p.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
  p.setColor(QPalette::Link, QColor(90, 170, 255));
  p.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
  return p;
}

//! Apply Light / Dark theme (window chrome follows app, not OS).
inline void applyAppTheme()
{
  auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (app == nullptr) {
    return;
  }

#if !defined(Q_OS_MACOS)
  if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    app->setStyle(fusion);
  }
#endif

  const bool dark = isDarkMode();
  app->setStyleSheet(QString());
  app->setPalette(dark ? darkPalette() : lightPalette());

  // Native title bar (min/max/close row) follows the app theme, not Windows.
  if (auto *hints = app->styleHints()) {
    hints->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
  }

#if defined(Q_OS_WIN)
  // Reinforce DWM immersive dark mode on already-created top-level windows.
  for (QWidget *widget : app->topLevelWidgets()) {
    if (!widget->isWindow()) {
      continue;
    }
    const WId wid = widget->winId();
    if (wid == 0) {
      continue;
    }
    HWND hwnd = reinterpret_cast<HWND>(wid);
    const BOOL useDark = dark ? TRUE : FALSE;
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
#endif
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    // Attribute 19 was used on older Windows 10 builds.
    DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
  }
#endif

  updateIconTheme();
}

} // namespace deskflow::gui

inline QFont fixedFont()
{
#if defined(Q_OS_WIN)
  QFont f({"Hack", "Liberation Mono", "Monospace", "Andale Mono"});
  f.setStyleHint(QFont::Monospace);
#else
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#endif

#if defined(Q_OS_MACOS)
  f.setPointSize(12);
#endif
  return f;
}
