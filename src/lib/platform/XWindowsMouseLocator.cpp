/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"

// X11 (Status/Bool) must come after Qt headers pulled in above.
#include "platform/XWindowsMouseLocator.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kFrameSec = 0.016;
constexpr int kDurationMs = 700;
constexpr int kWindowSize = 320;

float easeOutCubic(float t)
{
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

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
  m_frameCount = (std::max)(1, kDurationMs / 16);
  m_windowSize = kWindowSize;

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
  // Bright yellow; ring is cut via Shape so only the circle shows.
  attr.background_pixel = 0x40E0FF; // BGR: yellow-ish (X11 often packs as BGR for TrueColor)
  {
    // Prefer a vivid yellow from the default colormap.
    Colormap cmap = DefaultColormap(m_display, DefaultScreen(m_display));
    XColor color = {};
    color.red = 0xffff;
    color.green = 0xc800;
    color.blue = 0x0000;
    if (XAllocColor(m_display, cmap, &color) != 0) {
      attr.background_pixel = color.pixel;
    }
  }
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
  if (m_display == nullptr || m_window == None) {
    return;
  }

  const float t = static_cast<float>(m_frame) / static_cast<float>((std::max)(1, m_frameCount - 1));
  const float e = easeOutCubic(std::clamp(t, 0.0f, 1.0f));
  const float half = m_windowSize * 0.5f;
  const float maxRadius = half - 8.0f;
  const float minRadius = 12.0f;
  const float radius = maxRadius + (minRadius - maxRadius) * e;
  const int thickness = (std::max)(6, static_cast<int>(m_windowSize * 0.045f * (1.0f - 0.2f * e)));

  const int outer = static_cast<int>(radius + thickness * 0.5f);
  const int inner = (std::max)(0, static_cast<int>(radius - thickness * 0.5f));

  if (m_haveShape) {
    // Reliable ring: fill outer disk, then clear inner disk (XDrawArc on 1-bit often fails).
    Pixmap mask =
        XCreatePixmap(m_display, m_window, static_cast<unsigned>(m_windowSize), static_cast<unsigned>(m_windowSize), 1);
    GC gc = XCreateGC(m_display, mask, 0, nullptr);
    XSetForeground(m_display, gc, 0);
    XFillRectangle(m_display, mask, gc, 0, 0, static_cast<unsigned>(m_windowSize), static_cast<unsigned>(m_windowSize));
    XSetForeground(m_display, gc, 1);
    XFillArc(
        m_display, mask, gc, static_cast<int>(half) - outer, static_cast<int>(half) - outer, outer * 2, outer * 2, 0,
        360 * 64
    );
    if (inner > 1) {
      XSetForeground(m_display, gc, 0);
      XFillArc(
          m_display, mask, gc, static_cast<int>(half) - inner, static_cast<int>(half) - inner, inner * 2, inner * 2, 0,
          360 * 64
      );
    }
    XShapeCombineMask(m_display, m_window, ShapeBounding, 0, 0, mask, ShapeSet);
    XFreeGC(m_display, gc);
    XFreePixmap(m_display, mask);
    XClearWindow(m_display, m_window);
  } else {
    // No Shape: draw ring with Xlib into the window.
    XClearWindow(m_display, m_window);
    GC gc = XCreateGC(m_display, m_window, 0, nullptr);
    Colormap cmap = DefaultColormap(m_display, DefaultScreen(m_display));
    XColor color = {};
    color.red = 0xffff;
    color.green = 0xc800;
    color.blue = 0x0000;
    if (XAllocColor(m_display, cmap, &color) != 0) {
      XSetForeground(m_display, gc, color.pixel);
    }
    XSetLineAttributes(m_display, gc, thickness, LineSolid, CapRound, JoinRound);
    const int diam = static_cast<int>(radius * 2.0f);
    XDrawArc(
        m_display, m_window, gc, static_cast<int>(half - radius), static_cast<int>(half - radius), diam, diam, 0,
        360 * 64
    );
    XFreeGC(m_display, gc);
  }

  XRaiseWindow(m_display, m_window);
  XFlush(m_display);
}
