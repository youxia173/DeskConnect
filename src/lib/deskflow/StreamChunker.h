/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"

#include <cstdint>
#include <deque>
#include <string>

class EventQueueTimer;
class IEventQueue;

namespace deskflow {
class IStream;
}

//! Paced clipboard sender
/*!
Clipboard images are large: a full screen DIB is tens of megabytes. Handing all
of it to the socket at once puts mouse, keyboard and keep alive messages behind
it, so the cursor freezes until the link has drained. This queues chunks only
while the stream has little output pending, which keeps input ahead of clipboard
data and lets the link set the pace. Sends are queued because the receiver
assembles one clipboard at a time.
*/
class StreamChunker
{
public:
  StreamChunker() = default;
  StreamChunker(const StreamChunker &) = delete;
  StreamChunker &operator=(const StreamChunker &) = delete;
  ~StreamChunker();

  //! Queue \p data for sending, paced against the stream's output backlog.
  void sendClipboard(std::string data, ClipboardID id, uint32_t sequence, deskflow::IStream *stream, IEventQueue *events);

  //! Drop anything not sent yet.
  void cancel();

private:
  struct Pending
  {
    std::string data;
    size_t sent = 0;
    ClipboardID id = 0;
    uint32_t sequence = 0;
    bool started = false;
  };

  void clearTimer();
  void schedule();
  void pump();

  deskflow::IStream *m_stream = nullptr;
  IEventQueue *m_events = nullptr;
  EventQueueTimer *m_timer = nullptr;
  std::deque<Pending> m_queue;
};
