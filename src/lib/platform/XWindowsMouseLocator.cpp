/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Qt / Settings before X11: Xlib #define None/Bool/Status breaks Settings.h enums.
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/Settings.h"

#include "platform/XWindowsMouseLocator.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr double kFrameSec = 0.016;
constexpr int kWindowSize = 320;

} // namespace

XWindowsMouseLocator::~XWindowsMouseLocator()
{
  hide();
}

void XWindowsMouseLocator::hide()
{
  clearTimer();
  if (m_display != nullptr && m_window != None) {
    XDestroyWindow(m_display, m_window);
    XFlush(m_display);
    m_window = None;
  }
  m_frame = 0;
  m_pixels.clear();
}

void XWindowsMouseLocator::clearTimer()
{
  if (m_events != nullptr && m_timer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
  }
  m_timer = nullptr;
}

void XWindowsMouseLocator::schedule()
{
  clearTimer();
  if (m_events == nullptr) {
    return;
  }
  m_timer = m_events->newOneShotTimer(kFrameSec, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { onTimer(); });
}

void XWindowsMouseLocator::show(Display *display, Window root, int x, int y, IEventQueue *events)
{
  if (display == nullptr || root == None || events == nullptr) {
    return;
  }

  m_display = display;
  m_root = root;
  m_events = events;
  m_centerX = x;
  m_centerY = y;
  m_frame = 0;
  m_style = MouseLocatorPainter::parseStyle(
      Settings::value(Settings::Core::MouseLocatorStyle).toString().toStdString()
  );
  m_frameCount = (std::max)(1, MouseLocatorPainter::durationMs(m_style) / 16);
  m_windowSize = kWindowSize;
  m_pixels.assign(static_cast<size_t>(m_windowSize) * static_cast<size_t>(m_windowSize), 0);

  int eventBase = 0;
  int errorBase = 0;
  m_haveShape = (XShapeQueryExtension(m_display, &eventBase, &errorBase) != 0);

  if (m_window != None) {
    XDestroyWindow(m_display, m_window);
    m_window = None;
  }

  XSetWindowAttributes attr = {};
  attr.override_redirect = True;
  attr.backing_store = NotUseful;
  attr.save_under = True;
  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.event_mask = ExposureMask;

  m_window = XCreateWindow(
      m_display, m_root, m_centerX - m_windowSize / 2, m_centerY - m_windowSize / 2, static_cast<unsigned>(m_windowSize),
      static_cast<unsigned>(m_windowSize), 0, CopyFromParent, InputOutput, CopyFromParent,
      CWOverrideRedirect | CWBackingStore | CWSaveUnder | CWBackPixel | CWBorderPixel | CWEventMask, &attr
  );
  if (m_window == None) {
    LOG_ERR("mouse locator: XCreateWindow failed");
    return;
  }

  Atom state = XInternAtom(m_display, "_NET_WM_STATE", False);
  Atom above = XInternAtom(m_display, "_NET_WM_STATE_ABOVE", False);
  if (state != None && above != None) {
    XChangeProperty(m_display, m_window, state, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char *>(&above), 1);
  }

  // Do not accept input so clicks pass through to apps underneath.
  if (m_haveShape) {
    Pixmap empty = XCreatePixmap(m_display, m_window, 1, 1, 1);
    GC gc = XCreateGC(m_display, empty, 0, nullptr);
    XSetForeground(m_display, gc, 0);
    XFillRectangle(m_display, empty, gc, 0, 0, 1, 1);
    XShapeCombineMask(m_display, m_window, ShapeInput, 0, 0, empty, ShapeSet);
    XFreeGC(m_display, gc);
    XFreePixmap(m_display, empty);
  }

  LOG_INFO("mouse locator: show at %d,%d", m_centerX, m_centerY);
  XMapRaised(m_display, m_window);
  paint();
  schedule();
}

void XWindowsMouseLocator::onTimer()
{
  clearTimer();
  ++m_frame;
  if (m_frame >= m_frameCount) {
    hide();
    return;
  }
  paint();
  schedule();
}

void XWindowsMouseLocator::paint()
{
  if (m_display == nullptr || m_window == None || m_pixels.empty()) {
    return;
  }

  const float t = static_cast<float>(m_frame) / static_cast<float>((std::max)(1, m_frameCount - 1));
  MouseLocatorPainter::paint(m_pixels.data(), m_windowSize, m_style, t);

  if (m_haveShape) {
    Pixmap mask =
        XCreatePixmap(m_display, m_window, static_cast<unsigned>(m_windowSize), static_cast<unsigned>(m_windowSize), 1);
    GC gc = XCreateGC(m_display, mask, 0, nullptr);
    XSetForeground(m_display, gc, 0);
    XFillRectangle(m_display, mask, gc, 0, 0, static_cast<unsigned>(m_windowSize), static_cast<unsigned>(m_windowSize));
    XSetForeground(m_display, gc, 1);
    // Fill opaque runs per row (far fewer round-trips than per-pixel draws).
    for (int y = 0; y < m_windowSize; ++y) {
      int runStart = -1;
      for (int x = 0; x <= m_windowSize; ++x) {
        const bool on =
            (x < m_windowSize) &&
            (((m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_windowSize) + static_cast<size_t>(x)] >> 24) &
              0xffu) > 24u);
        if (on && runStart < 0) {
          runStart = x;
        } else if (!on && runStart >= 0) {
          XFillRectangle(m_display, mask, gc, runStart, y, static_cast<unsigned>(x - runStart), 1);
          runStart = -1;
        }
      }
    }
    XShapeCombineMask(m_display, m_window, ShapeBounding, 0, 0, mask, ShapeSet);
    XFreeGC(m_display, gc);
    XFreePixmap(m_display, mask);
  }

  XWindowAttributes wa = {};
  if (XGetWindowAttributes(m_display, m_window, &wa) == 0 || wa.visual == nullptr) {
    XClearWindow(m_display, m_window);
    XFlush(m_display);
    return;
  }

  const int depth = wa.depth;
  std::vector<uint32_t> xrgb(static_cast<size_t>(m_windowSize) * static_cast<size_t>(m_windowSize), 0);
  for (int i = 0; i < m_windowSize * m_windowSize; ++i) {
    const uint32_t px = m_pixels[static_cast<size_t>(i)];
    const uint32_t a = (px >> 24) & 0xffu;
    if (a == 0) {
      continue;
    }
    uint32_t r = (px >> 16) & 0xffu;
    uint32_t g = (px >> 8) & 0xffu;
    uint32_t b = px & 0xffu;
    if (a < 255) {
      r = (r * 255u) / a;
      g = (g * 255u) / a;
      b = (b * 255u) / a;
    }
    if (depth >= 24) {
      xrgb[static_cast<size_t>(i)] = (r << 16) | (g << 8) | b;
    } else {
      xrgb[static_cast<size_t>(i)] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
  }

  XImage *image = XCreateImage(
      m_display, wa.visual, static_cast<unsigned>(depth), ZPixmap, 0, reinterpret_cast<char *>(xrgb.data()),
      static_cast<unsigned>(m_windowSize), static_cast<unsigned>(m_windowSize), 32, m_windowSize * 4
  );
  if (image == nullptr) {
    XClearWindow(m_display, m_window);
    XFlush(m_display);
    return;
  }
  GC gc = XCreateGC(m_display, m_window, 0, nullptr);
  XPutImage(
      m_display, m_window, gc, image, 0, 0, 0, 0, static_cast<unsigned>(m_windowSize), static_cast<unsigned>(m_windowSize)
  );
  XFreeGC(m_display, gc);
  image->data = nullptr; // owned by vector
  XDestroyImage(image);
  XRaiseWindow(m_display, m_window);
  XFlush(m_display);
}
