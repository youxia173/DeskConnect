/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "filetransfer/FileTransfer.h"
#include <functional>
#include <memory>

class IEventQueue;
class EventQueueTimer;

// Read-only virtual files keep URI/metadata clipboard probes separate from
// actual content reads. All network callbacks stay on the core event thread.
class XWindowsClipboardFiles
{
public:
  using Pull = std::function<std::vector<std::string>()>;
  using List = std::function<std::vector<deskflow::FileOffer>()>;
  using Publish = std::function<void(const std::vector<std::string> &)>;
  XWindowsClipboardFiles(IEventQueue *events, Pull pull, List list, Publish publish);
  ~XWindowsClipboardFiles();
  XWindowsClipboardFiles(const XWindowsClipboardFiles &) = delete;
  XWindowsClipboardFiles &operator=(const XWindowsClipboardFiles &) = delete;
  static bool available();

private:
  struct State;
  void poll();
  void schedule();
  void clearTimer();
  IEventQueue *m_events;
  EventQueueTimer *m_timer = nullptr;
  std::shared_ptr<State> m_state;
};
