/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "filetransfer/FileTransfer.h"
#include "filetransfer/TransferProgress.h"
#include "io/IStream.h"

#include <QFile>

#include <functional>
#include <string>
#include <vector>

class EventQueueTimer;
class IEventQueue;

namespace deskflow {

//! Async DDRG/DFTR sender that yields to the event loop between chunks.
/*!
Synchronous bulk writes block keepalives and input. This session sends at most
one chunk per timer tick so mouse/keyboard and CALV keep working during large
transfers.
*/
class FileSendSession
{
public:
  using DoneCallback = std::function<void(bool success)>;
  using ProgressCallback = std::function<void(const TransferProgressInfo &)>;

  FileSendSession() = default;
  FileSendSession(const FileSendSession &) = delete;
  FileSendSession &operator=(const FileSendSession &) = delete;
  ~FileSendSession();

  //! Begin sending. Cancels any in-progress send first.
  //! \p fingerprint identifies clipboard file payload (skip duplicate sends).
  //! \p onDone optional; invoked when the send finishes or fails/cancels.
  //! \p onProgress optional; invoked as chunks are written (may be frequent).
  bool start(
      IStream *stream, IEventQueue *events, const std::vector<FileOffer> &offers, std::string fingerprint,
      DoneCallback onDone = {}, ProgressCallback onProgress = {}
  );

  void cancel(bool notifyDone = true);
  //! Drop the speed limit for the remainder of this send (does not change settings).
  void setFullSpeedForSession();
  bool isActive() const
  {
    return m_active;
  }
  bool isFullSpeedForSession() const
  {
    return m_forceFullSpeed;
  }
  const std::string &fingerprint() const
  {
    return m_fingerprint;
  }

private:
  void clearTimer();
  void schedulePump();
  void pump();
  bool openCurrentFile();
  void finishCurrentFile();
  void completeOk();
  void fail(const char *reason);
  void invokeDone(bool success);
  void emitProgress(bool force = false);
  uint64_t sessionBytesDone() const;
  uint64_t effectiveLimitBytesPerSec() const;

  IStream *m_stream = nullptr;
  IEventQueue *m_events = nullptr;
  EventQueueTimer *m_timer = nullptr;
  std::vector<FileOffer> m_offers;
  size_t m_index = 0;
  QFile m_file;
  uint64_t m_sent = 0;
  uint64_t m_completedBytes = 0;
  uint64_t m_totalBytes = 0;
  uint64_t m_lastChunkBytes = 0;
  bool m_active = false;
  bool m_startedFile = false;
  bool m_forceFullSpeed = false;
  std::string m_fingerprint;
  DoneCallback m_onDone;
  ProgressCallback m_onProgress;
};

//! Legacy helper: start a throwaway session (prefer FileSendSession members).
bool writeFileOffersToStream(IStream *stream, const std::vector<FileOffer> &offers);

} // namespace deskflow
