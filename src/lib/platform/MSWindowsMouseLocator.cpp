/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsMouseLocator.h"

#include "base/Log.h"
#include "common/Settings.h"
#include "platform/MSWindowsScreen.h"
#include "platform/MouseLocatorPainter.h"

#include <algorithm>
#include <vector>

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kFrameMs = 16;
constexpr wchar_t kClassName[] = L"DeskConnectMouseLocator";

} // namespace

MSWindowsMouseLocator::~MSWindowsMouseLocator()
{
  hide();
}

ATOM MSWindowsMouseLocator::registerClass()
{
  static ATOM atom = 0;
  if (atom != 0) {
    return atom;
  }

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &MSWindowsMouseLocator::wndProc;
  wc.hInstance = MSWindowsScreen::getWindowInstance();
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  atom = RegisterClassExW(&wc);
  if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LOG_ERR("mouse locator: RegisterClassEx failed (%lu)", GetLastError());
  }
  return atom;
}

LRESULT CALLBACK MSWindowsMouseLocator::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  auto *self = reinterpret_cast<MSWindowsMouseLocator *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  if (self != nullptr && msg == WM_TIMER && wParam == kTimerId) {
    self->onTimer();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MSWindowsMouseLocator::show(int x, int y)
{
  HINSTANCE instance = MSWindowsScreen::getWindowInstance();
  if (instance == nullptr) {
    return;
  }
  if (registerClass() == 0) {
    return;
  }

  m_style = MouseLocatorPainter::parseStyle(
      Settings::value(Settings::Core::MouseLocatorStyle).toString().toStdString()
  );
  m_centerX = x;
  m_centerY = y;
  m_frame = 0;
  const int duration = MouseLocatorPainter::durationMs(m_style);
  m_frameCount = (std::max)(1, duration / static_cast<int>(kFrameMs));

  UINT dpi = 96;
  if (m_hwnd != nullptr) {
    dpi = GetDpiForWindow(m_hwnd);
  } else {
    const HDC screen = GetDC(nullptr);
    if (screen != nullptr) {
      dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
      ReleaseDC(nullptr, screen);
    }
  }
  m_windowSize = static_cast<int>((320 * dpi + 48) / 96);
  if (m_windowSize < 200) {
    m_windowSize = 200;
  }

  const int left = m_centerX - m_windowSize / 2;
  const int top = m_centerY - m_windowSize / 2;

  if (m_hwnd == nullptr) {
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"",
        WS_POPUP, left, top, m_windowSize, m_windowSize, nullptr, nullptr, instance, this
    );
    if (m_hwnd == nullptr) {
      LOG_ERR("mouse locator: CreateWindowEx failed (%lu)", GetLastError());
      return;
    }
  } else {
    SetWindowPos(m_hwnd, HWND_TOPMOST, left, top, m_windowSize, m_windowSize, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }

  ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
  paintFrame();

  if (m_timer != 0) {
    KillTimer(m_hwnd, m_timer);
  }
  m_timer = SetTimer(m_hwnd, kTimerId, kFrameMs, nullptr);
}

void MSWindowsMouseLocator::hide()
{
  destroyWindow();
}

void MSWindowsMouseLocator::destroyWindow()
{
  if (m_hwnd != nullptr) {
    if (m_timer != 0) {
      KillTimer(m_hwnd, m_timer);
      m_timer = 0;
    }
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
  }
  m_frame = 0;
}

void MSWindowsMouseLocator::onTimer()
{
  ++m_frame;
  if (m_frame >= m_frameCount) {
    hide();
    return;
  }
  paintFrame();
}

void MSWindowsMouseLocator::paintFrame()
{
  if (m_hwnd == nullptr || m_windowSize <= 0) {
    return;
  }

  const float t = static_cast<float>(m_frame) / static_cast<float>((std::max)(1, m_frameCount - 1));

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = m_windowSize;
  bmi.bmiHeader.biHeight = -m_windowSize;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  HBITMAP dib = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib == nullptr || bits == nullptr) {
    return;
  }

  auto *pixels = static_cast<uint32_t *>(bits);
  MouseLocatorPainter::paint(pixels, m_windowSize, m_style, t);

  HDC screenDc = GetDC(nullptr);
  HDC memDc = CreateCompatibleDC(screenDc);
  HGDIOBJ old = SelectObject(memDc, dib);

  POINT ptSrc = {0, 0};
  POINT ptDst = {m_centerX - m_windowSize / 2, m_centerY - m_windowSize / 2};
  SIZE size = {m_windowSize, m_windowSize};
  BLENDFUNCTION blend = {};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  UpdateLayeredWindow(m_hwnd, screenDc, &ptDst, &size, memDc, &ptSrc, 0, &blend, ULW_ALPHA);

  SelectObject(memDc, old);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);
  DeleteObject(dib);
}
