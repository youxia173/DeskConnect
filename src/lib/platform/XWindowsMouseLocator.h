/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <X11/Xlib.h>

class EventQueueTimer;
class IEventQueue;

//! Short-lived override-redirect ring around the cursor (X11).
class XWindowsMouseLocator
{
public:
  XWindowsMouseLocator() = default;
  XWindowsMouseLocator(const XWindowsMouseLocator &) = delete;
  XWindowsMouseLocator &operator=(const XWindowsMouseLocator &) = delete;
  ~XWindowsMouseLocator();

  void show(Display *display, Window root, int x, int y, IEventQueue *events);
  void hide();

private:
  void clearTimer();
  void schedule();
  void onTimer();
  void paint();

  Display *m_display = nullptr;
  Window m_root = None;
  Window m_window = None;
  IEventQueue *m_events = nullptr;
  EventQueueTimer *m_timer = nullptr;
  int m_centerX = 0;
  int m_centerY = 0;
  int m_frame = 0;
  int m_frameCount = 0;
  int m_windowSize = 0;
  bool m_haveShape = false;
};
