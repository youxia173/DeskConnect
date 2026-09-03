/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "filetransfer/FileSend.h"

#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <QString>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace deskflow {

namespace {
constexpr qint64 kFileChunkSize = 64 * 1024;
// When unlimited, send several chunks per tick so we are not capped near one chunk/timer.
constexpr int kFullSpeedChunksPerPump = 64; // up to 4 MiB per turn
// newOneShotTimer requires duration > 0; keep a tiny yield even at full speed.
constexpr double kMinPumpIntervalSec = 0.001;
} // namespace

FileSendSession::~FileSendSession()
{
  cancel();
}

void FileSendSession::clearTimer()
{
  if (m_events != nullptr && m_timer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
  }
  m_timer = nullptr;
}

void FileSendSession::invokeDone(bool success)
{
  DoneCallback cb;
  cb.swap(m_onDone);
  if (cb) {
    cb(success);
  }
}

void FileSendSession::cancel(bool notifyDone)
{
  clearTimer();
  if (m_file.isOpen()) {
    m_file.close();
  }
  const bool wasActive = m_active;
  m_active = false;
  m_startedFile = false;
  m_forceFullSpeed = false;
  m_stream = nullptr;
  m_events = nullptr;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_completedBytes = 0;
  m_totalBytes = 0;
  m_lastChunkBytes = 0;
  m_fingerprint.clear();
  m_onProgress = {};
  if (wasActive && notifyDone) {
    invokeDone(false);
  } else {
    m_onDone = {};
  }
}

void FileSendSession::setFullSpeedForSession()
{
  if (!m_active || m_forceFullSpeed) {
    return;
  }
  m_forceFullSpeed = true;
  LOG_INFO("file transfer speed limit disabled for this send");
  // Reschedule immediately so the next chunk is not delayed by the old interval.
  if (m_events != nullptr) {
    schedulePump();
  }
}

uint64_t FileSendSession::effectiveLimitBytesPerSec() const
{
  if (m_forceFullSpeed) {
    return 0;
  }
  return transferSpeedLimitBytesPerSec();
}

bool FileSendSession::start(
    IStream *stream, IEventQueue *events, const std::vector<FileOffer> &offers, std::string fingerprint,
    DoneCallback onDone, ProgressCallback onProgress
)
{
  if (stream == nullptr || events == nullptr || offers.empty() || !isFileTransferEnabled()) {
    return false;
  }

  const uint64_t maxBytes = maxTransferBytes();
  uint64_t total = 0;
  for (const auto &offer : offers) {
    total += offer.size;
  }
  if (maxBytes > 0 && total > maxBytes) {
    LOG_WARN(
        "clipboard file transfer skipped: %llu bytes exceeds limit %llu", static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(maxBytes)
    );
    return false;
  }

  // Cancel without invoking done on the new callback.
  clearTimer();
  if (m_file.isOpen()) {
    m_file.close();
  }
  if (m_active) {
    m_active = false;
    DoneCallback oldCb;
    oldCb.swap(m_onDone);
    if (oldCb) {
      oldCb(false);
    }
  }
  m_onDone = {};
  m_onProgress = {};
  m_startedFile = false;
  m_forceFullSpeed = false;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_completedBytes = 0;
  m_totalBytes = 0;
  m_lastChunkBytes = 0;
  m_fingerprint.clear();

  m_stream = stream;
  m_events = events;
  m_offers = offers;
  m_fingerprint = std::move(fingerprint);
  m_onDone = std::move(onDone);
  m_onProgress = std::move(onProgress);
  m_index = 0;
  m_sent = 0;
  m_completedBytes = 0;
  m_totalBytes = total;
  m_startedFile = false;
  m_active = true;

  const std::string drag = encodeDragInfo(m_offers);
  ProtocolUtil::writef(m_stream, kMsgDDragInfo, static_cast<uint16_t>(m_offers.size()), &drag);
  const uint64_t limitBps = effectiveLimitBytesPerSec();
  if (limitBps > 0) {
    LOG_INFO(
        "sending %zu clipboard file(s) asynchronously (limited to %llu KiB/s)", m_offers.size(),
        static_cast<unsigned long long>(limitBps / 1024ull)
    );
  } else {
    LOG_INFO("sending %zu clipboard file(s) asynchronously (full speed)", m_offers.size());
  }
  emitProgress(true);

  schedulePump();
  return true;
}

void FileSendSession::schedulePump()
{
  if (!m_active || m_events == nullptr) {
    return;
  }
  clearTimer();

  double interval = kMinPumpIntervalSec;
  const uint64_t limitBps = effectiveLimitBytesPerSec();
  if (limitBps > 0) {
    const uint64_t bytes = m_lastChunkBytes > 0 ? m_lastChunkBytes : static_cast<uint64_t>(kFileChunkSize);
    interval = static_cast<double>(bytes) / static_cast<double>(limitBps);
    if (interval < kMinPumpIntervalSec) {
      interval = kMinPumpIntervalSec;
    }
  }

  m_timer = m_events->newOneShotTimer(interval, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { pump(); });
}

bool FileSendSession::openCurrentFile()
{
  if (m_index >= m_offers.size()) {
    return false;
  }
  const auto &offer = m_offers[m_index];
  m_file.setFileName(QString::fromStdString(offer.localPath));
  if (!m_file.open(QIODevice::ReadOnly)) {
    LOG_ERR("failed to open file for transfer: %s", offer.localPath.c_str());
    return false;
  }
  m_sent = 0;
  m_startedFile = true;
  const std::string sizeStr = QString::number(static_cast<qulonglong>(offer.size)).toStdString();
  ProtocolUtil::writef(m_stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataStart), &sizeStr);
  LOG_INFO(
      "sending file \"%s\" (%llu bytes)", offer.name.c_str(), static_cast<unsigned long long>(offer.size)
  );
  emitProgress(true);
  return true;
}

void FileSendSession::finishCurrentFile()
{
  std::string empty;
  ProtocolUtil::writef(m_stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataEnd), &empty);
  if (m_file.isOpen()) {
    m_file.close();
  }
  if (m_index < m_offers.size()) {
    m_completedBytes += m_offers[m_index].size;
  }
  m_startedFile = false;
  ++m_index;
  m_sent = 0;
  m_lastChunkBytes = 0;
  emitProgress(true);
}

void FileSendSession::completeOk()
{
  LOG_INFO("clipboard file transfer send complete (%zu file(s))", m_offers.size());
  emitProgress(true);
  const auto fp = m_fingerprint;
  clearTimer();
  if (m_file.isOpen()) {
    m_file.close();
  }
  m_active = false;
  m_startedFile = false;
  m_forceFullSpeed = false;
  m_stream = nullptr;
  m_events = nullptr;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_completedBytes = 0;
  m_totalBytes = 0;
  m_lastChunkBytes = 0;
  m_onProgress = {};
  m_fingerprint = fp;
  invokeDone(true);
}

void FileSendSession::fail(const char *reason)
{
  LOG_ERR("clipboard file transfer aborted: %s", reason);
  clearTimer();
  if (m_file.isOpen()) {
    m_file.close();
  }
  m_active = false;
  m_startedFile = false;
  m_forceFullSpeed = false;
  m_stream = nullptr;
  m_events = nullptr;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_completedBytes = 0;
  m_totalBytes = 0;
  m_lastChunkBytes = 0;
  m_onProgress = {};
  m_fingerprint.clear();
  invokeDone(false);
}

uint64_t FileSendSession::sessionBytesDone() const
{
  return m_completedBytes + m_sent;
}

void FileSendSession::emitProgress(bool force)
{
  if (!m_onProgress || m_offers.empty()) {
    return;
  }
  TransferProgressInfo info;
  info.sending = true;
  info.fileCount = m_offers.size();
  info.fileIndex = std::min(m_index, m_offers.size() - 1);
  info.name = m_offers[info.fileIndex].name;
  info.bytesDone = sessionBytesDone();
  info.bytesTotal = m_totalBytes;
  // force is reserved for call-site throttling helpers; always notify here.
  (void)force;
  m_onProgress(info);
}

void FileSendSession::pump()
{
  clearTimer();
  if (!m_active || m_stream == nullptr) {
    return;
  }

  try {
    const uint64_t limitBps = effectiveLimitBytesPerSec();
    const int maxChunks = (limitBps == 0) ? kFullSpeedChunksPerPump : 1;

    for (int chunk = 0; chunk < maxChunks; ++chunk) {
      if (!m_active || m_stream == nullptr) {
        return;
      }

      if (!m_startedFile) {
        if (m_index >= m_offers.size()) {
          completeOk();
          return;
        }
        if (!openCurrentFile()) {
          fail("could not open next file");
          return;
        }
      }

      const auto &offer = m_offers[m_index];
      if (m_sent >= offer.size) {
        finishCurrentFile();
        // Continue into the next file within this burst when unlimited.
        if (limitBps != 0) {
          schedulePump();
          return;
        }
        continue;
      }

      const qint64 toRead = static_cast<qint64>(std::min<uint64_t>(
          static_cast<uint64_t>(kFileChunkSize), offer.size - m_sent
      ));
      const QByteArray data = m_file.read(toRead);
      if (data.isEmpty() && m_sent < offer.size) {
        fail("unexpected EOF");
        return;
      }
      std::string payload(data.constData(), static_cast<size_t>(data.size()));
      ProtocolUtil::writef(m_stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataChunk), &payload);
      m_sent += static_cast<uint64_t>(data.size());
      m_lastChunkBytes = static_cast<uint64_t>(data.size());
      emitProgress(false);

      if (m_sent >= offer.size) {
        finishCurrentFile();
        if (limitBps != 0) {
          break;
        }
      }
    }

    if (m_active) {
      schedulePump();
    }
  } catch (...) {
    fail("write failed");
  }
}

bool writeFileOffersToStream(IStream *stream, const std::vector<FileOffer> &offers)
{
  // Sync fallback for callers without an event queue (should be rare).
  // Prefer FileSendSession::start for production paths.
  if (stream == nullptr || offers.empty() || !isFileTransferEnabled()) {
    return false;
  }

  const uint64_t maxBytes = maxTransferBytes();
  uint64_t total = 0;
  for (const auto &offer : offers) {
    total += offer.size;
  }
  if (maxBytes > 0 && total > maxBytes) {
    LOG_WARN(
        "clipboard file transfer skipped: %llu bytes exceeds limit %llu", static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(maxBytes)
    );
    return false;
  }

  const std::string drag = encodeDragInfo(offers);
  ProtocolUtil::writef(stream, kMsgDDragInfo, static_cast<uint16_t>(offers.size()), &drag);

  const uint64_t limitBps = transferSpeedLimitBytesPerSec();

  for (const auto &offer : offers) {
    QFile file(QString::fromStdString(offer.localPath));
    if (!file.open(QIODevice::ReadOnly)) {
      LOG_ERR("failed to open file for transfer: %s", offer.localPath.c_str());
      return false;
    }

    const std::string sizeStr = QString::number(static_cast<qulonglong>(offer.size)).toStdString();
    ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataStart), &sizeStr);

    uint64_t sent = 0;
    while (sent < offer.size) {
      const QByteArray chunk = file.read(kFileChunkSize);
      if (chunk.isEmpty() && sent < offer.size) {
        LOG_ERR("unexpected EOF transferring %s", offer.localPath.c_str());
        return false;
      }
      std::string payload(chunk.constData(), static_cast<size_t>(chunk.size()));
      ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataChunk), &payload);
      sent += static_cast<uint64_t>(chunk.size());
      if (limitBps > 0 && chunk.size() > 0) {
        const double seconds = static_cast<double>(chunk.size()) / static_cast<double>(limitBps);
        const int ms = static_cast<int>(std::ceil(seconds * 1000.0));
        if (ms > 0) {
          QThread::msleep(static_cast<unsigned long>(ms));
        }
      }
    }

    std::string empty;
    ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataEnd), &empty);
    LOG_INFO("sent file \"%s\" (%llu bytes)", offer.name.c_str(), static_cast<unsigned long long>(offer.size));
  }

  return true;
}

} // namespace deskflow
