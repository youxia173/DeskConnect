/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsMouseLocator.h"

#include "base/Log.h"
#include "platform/MSWindowsScreen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kFrameMs = 16;
constexpr int kDurationMs = 700;
constexpr wchar_t kClassName[] = L"DeskConnectMouseLocator";

float easeOutCubic(float t)
{
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

void clearPixels(uint32_t *pixels, int size)
{
  std::memset(pixels, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * sizeof(uint32_t));
}

void blendPremul(uint32_t *pixel, uint8_t r, uint8_t g, uint8_t b, float cover)
{
  if (cover <= 0.0f) {
    return;
  }
  if (cover > 1.0f) {
    cover = 1.0f;
  }
  const uint8_t a = static_cast<uint8_t>(255.0f * cover + 0.5f);
  if (a == 0) {
    return;
  }
  // Little-endian DIB pixels are B,G,R,A in memory (premultiplied for ULW).
  const uint32_t sb = (b * a) / 255u;
  const uint32_t sg = (g * a) / 255u;
  const uint32_t sr = (r * a) / 255u;
  const uint32_t src = (static_cast<uint32_t>(a) << 24) | (sr << 16) | (sg << 8) | sb;

  const uint32_t dst = *pixel;
  if (dst == 0) {
    *pixel = src;
    return;
  }

  const uint32_t da = (dst >> 24) & 0xffu;
  const uint32_t inv = 255u - a;
  const uint32_t outA = a + (da * inv) / 255u;
  const uint32_t outB = sb + (((dst & 0xffu) * inv) / 255u);
  const uint32_t outG = sg + ((((dst >> 8) & 0xffu) * inv) / 255u);
  const uint32_t outR = sr + ((((dst >> 16) & 0xffu) * inv) / 255u);
  *pixel = (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

void drawRing(uint32_t *pixels, int size, float radius, float thickness, uint8_t r, uint8_t g, uint8_t b, float alpha)
{
  if (radius <= 0.5f || thickness <= 0.0f || alpha <= 0.0f) {
    return;
  }
  const float cx = (size - 1) * 0.5f;
  const float cy = (size - 1) * 0.5f;
  const float half = thickness * 0.5f;
  const float rOuter = radius + half;
  const float rInner = (std::max)(0.0f, radius - half);
  const int y0 = (std::max)(0, static_cast<int>(cy - rOuter - 2));
  const int y1 = (std::min)(size - 1, static_cast<int>(cy + rOuter + 2));
  const int x0 = (std::max)(0, static_cast<int>(cx - rOuter - 2));
  const int x1 = (std::min)(size - 1, static_cast<int>(cx + rOuter + 2));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      float cover = 0.0f;
      if (d <= rOuter + 1.0f && d >= rInner - 1.0f) {
        const float outer = 1.0f - std::clamp(d - rOuter, 0.0f, 1.0f);
        const float inner = 1.0f - std::clamp(rInner - d, 0.0f, 1.0f);
        cover = outer * inner;
      }
      if (cover > 0.0f) {
        blendPremul(&pixels[y * size + x], r, g, b, cover * alpha);
      }
    }
  }
}

void drawDisk(uint32_t *pixels, int size, float radius, uint8_t r, uint8_t g, uint8_t b, float alpha)
{
  if (radius <= 0.5f || alpha <= 0.0f) {
    return;
  }
  const float cx = (size - 1) * 0.5f;
  const float cy = (size - 1) * 0.5f;
  const int y0 = (std::max)(0, static_cast<int>(cy - radius - 2));
  const int y1 = (std::min)(size - 1, static_cast<int>(cy + radius + 2));
  const int x0 = (std::max)(0, static_cast<int>(cx - radius - 2));
  const int x1 = (std::min)(size - 1, static_cast<int>(cx + radius + 2));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float cover = 1.0f - std::clamp(d - radius, 0.0f, 1.0f);
      if (cover > 0.0f) {
        blendPremul(&pixels[y * size + x], r, g, b, cover * alpha);
      }
    }
  }
}

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

  m_centerX = x;
  m_centerY = y;
  m_frame = 0;
  m_frameCount = (std::max)(1, kDurationMs / static_cast<int>(kFrameMs));

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
    SetWindowPos(
        m_hwnd, HWND_TOPMOST, left, top, m_windowSize, m_windowSize, SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
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
  const float e = easeOutCubic(std::clamp(t, 0.0f, 1.0f));
  const float half = m_windowSize * 0.5f;
  const float maxRadius = half - 8.0f;
  const float minRadius = (std::max)(10.0f, m_windowSize * 0.04f);
  const float radius = maxRadius + (minRadius - maxRadius) * e;
  const float fade = 1.0f - e * 0.35f;
  const float thickness = (std::max)(4.0f, m_windowSize * 0.035f) * (1.0f - 0.25f * e);

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
  clearPixels(pixels, m_windowSize);

  // Soft fill so the cursor is easy to spot on busy desktops.
  drawDisk(pixels, m_windowSize, radius, 255, 196, 0, 0.18f * fade);
  // Outer bright ring.
  drawRing(pixels, m_windowSize, radius, thickness, 255, 220, 40, 0.95f * fade);
  // Inner white ring for contrast on dark and light backgrounds.
  drawRing(pixels, m_windowSize, radius - thickness * 0.85f, thickness * 0.55f, 255, 255, 255, 0.85f * fade);

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
