/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "platform/MouseLocatorPainter.h"

//! Short-lived click-through overlay that highlights the cursor.
class MSWindowsMouseLocator
{
public:
  MSWindowsMouseLocator() = default;
  MSWindowsMouseLocator(const MSWindowsMouseLocator &) = delete;
  MSWindowsMouseLocator &operator=(const MSWindowsMouseLocator &) = delete;
  ~MSWindowsMouseLocator();

  //! Show (or restart) the locator centered at screen coordinates \p x, \p y.
  void show(int x, int y);
  void hide();

private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static ATOM registerClass();
  void onTimer();
  void paintFrame();
  void destroyWindow();

  HWND m_hwnd = nullptr;
  UINT_PTR m_timer = 0;
  int m_centerX = 0;
  int m_centerY = 0;
  int m_frame = 0;
  int m_frameCount = 0;
  int m_windowSize = 0;
  MouseLocatorPainter::Style m_style = MouseLocatorPainter::Style::ShrinkRing;
};
