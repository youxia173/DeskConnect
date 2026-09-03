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

#include <algorithm>

namespace deskflow {

namespace {
constexpr qint64 kFileChunkSize = 64 * 1024;
// Yield between chunks so keepalives and input keep flowing.
constexpr double kPumpIntervalSec = 0.0;
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

void FileSendSession::cancel()
{
  clearTimer();
  if (m_file.isOpen()) {
    m_file.close();
  }
  const bool wasActive = m_active;
  m_active = false;
  m_startedFile = false;
  m_stream = nullptr;
  m_events = nullptr;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_fingerprint.clear();
  if (wasActive) {
    invokeDone(false);
  } else {
    m_onDone = {};
  }
}

bool FileSendSession::start(
    IStream *stream, IEventQueue *events, const std::vector<FileOffer> &offers, std::string fingerprint,
    DoneCallback onDone
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
  m_startedFile = false;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_fingerprint.clear();

  m_stream = stream;
  m_events = events;
  m_offers = offers;
  m_fingerprint = std::move(fingerprint);
  m_onDone = std::move(onDone);
  m_index = 0;
  m_sent = 0;
  m_startedFile = false;
  m_active = true;

  const std::string drag = encodeDragInfo(m_offers);
  ProtocolUtil::writef(m_stream, kMsgDDragInfo, static_cast<uint16_t>(m_offers.size()), &drag);
  LOG_INFO("sending %zu clipboard file(s) asynchronously", m_offers.size());

  schedulePump();
  return true;
}

void FileSendSession::schedulePump()
{
  if (!m_active || m_events == nullptr) {
    return;
  }
  clearTimer();
  m_timer = m_events->newOneShotTimer(kPumpIntervalSec, nullptr);
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
  return true;
}

void FileSendSession::finishCurrentFile()
{
  std::string empty;
  ProtocolUtil::writef(m_stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataEnd), &empty);
  if (m_file.isOpen()) {
    m_file.close();
  }
  m_startedFile = false;
  ++m_index;
  m_sent = 0;
}

void FileSendSession::completeOk()
{
  LOG_INFO("clipboard file transfer send complete (%zu file(s))", m_offers.size());
  const auto fp = m_fingerprint;
  clearTimer();
  if (m_file.isOpen()) {
    m_file.close();
  }
  m_active = false;
  m_startedFile = false;
  m_stream = nullptr;
  m_events = nullptr;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
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
  m_stream = nullptr;
  m_events = nullptr;
  m_offers.clear();
  m_index = 0;
  m_sent = 0;
  m_fingerprint.clear();
  invokeDone(false);
}

void FileSendSession::pump()
{
  clearTimer();
  if (!m_active || m_stream == nullptr) {
    return;
  }

  try {
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
      schedulePump();
      return;
    }

    const qint64 toRead = static_cast<qint64>(std::min<uint64_t>(
        static_cast<uint64_t>(kFileChunkSize), offer.size - m_sent
    ));
    const QByteArray chunk = m_file.read(toRead);
    if (chunk.isEmpty() && m_sent < offer.size) {
      fail("unexpected EOF");
      return;
    }
    std::string payload(chunk.constData(), static_cast<size_t>(chunk.size()));
    ProtocolUtil::writef(m_stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataChunk), &payload);
    m_sent += static_cast<uint64_t>(chunk.size());

    if (m_sent >= offer.size) {
      finishCurrentFile();
    }
    schedulePump();
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
    }

    std::string empty;
    ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataEnd), &empty);
    LOG_INFO("sent file \"%s\" (%llu bytes)", offer.name.c_str(), static_cast<unsigned long long>(offer.size));
  }

  return true;
}

} // namespace deskflow
