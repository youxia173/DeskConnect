/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/StreamChunker.h"

#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <QString>

#include <algorithm>

namespace {

constexpr size_t g_chunkSize = 64 * 1024;
// Keep the socket backlog small so input messages are never far behind.
constexpr uint32_t g_maxQueuedOutputBytes = 128 * 1024;
constexpr double g_pumpIntervalSec = 0.004;

void writeChunk(deskflow::IStream *stream, ClipboardID id, uint32_t sequence, uint8_t mark, const std::string &payload)
{
  std::string data = payload;
  ProtocolUtil::writef(stream, kMsgDClipboard, id, sequence, mark, &data);
}

} // namespace

StreamChunker::~StreamChunker()
{
  cancel();
}

void StreamChunker::cancel()
{
  clearTimer();
  m_queue.clear();
}

void StreamChunker::clearTimer()
{
  if (m_events != nullptr && m_timer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
  }
  m_timer = nullptr;
}

void StreamChunker::sendClipboard(
    std::string data, ClipboardID id, uint32_t sequence, deskflow::IStream *stream, IEventQueue *events
)
{
  if (stream == nullptr || events == nullptr) {
    return;
  }

  m_stream = stream;
  m_events = events;

  Pending pending;
  pending.data = std::move(data);
  pending.id = id;
  pending.sequence = sequence;
  m_queue.push_back(std::move(pending));

  pump();
}

void StreamChunker::schedule()
{
  clearTimer();
  m_timer = m_events->newOneShotTimer(g_pumpIntervalSec, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { pump(); });
}

void StreamChunker::pump()
{
  clearTimer();
  if (m_stream == nullptr) {
    m_queue.clear();
    return;
  }

  try {
    while (!m_queue.empty()) {
      if (m_stream->getOutputSize() >= g_maxQueuedOutputBytes) {
        schedule();
        return;
      }

      auto &pending = m_queue.front();
      if (!pending.started) {
        pending.started = true;
        writeChunk(
            m_stream, pending.id, pending.sequence, ChunkType::DataStart,
            QString::number(static_cast<qulonglong>(pending.data.size())).toStdString()
        );
        continue;
      }

      if (pending.sent < pending.data.size()) {
        const size_t n = std::min(g_chunkSize, pending.data.size() - pending.sent);
        writeChunk(m_stream, pending.id, pending.sequence, ChunkType::DataChunk, pending.data.substr(pending.sent, n));
        pending.sent += n;
        continue;
      }

      writeChunk(m_stream, pending.id, pending.sequence, ChunkType::DataEnd, std::string());
      LOG_DEBUG("sent clipboard %d size=%zu", pending.id, pending.sent);
      m_queue.pop_front();
    }
  } catch (...) {
    LOG_WARN("clipboard send failed");
    m_queue.clear();
  }
}
