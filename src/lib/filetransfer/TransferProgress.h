/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace deskflow {

struct TransferProgressInfo
{
  bool sending = true; //!< true = upload/send, false = download/receive
  std::string name;
  size_t fileIndex = 0; //!< 0-based current file
  size_t fileCount = 0;
  uint64_t bytesDone = 0;  //!< session bytes transferred
  uint64_t bytesTotal = 0; //!< session total (0 if unknown)
};

//! Throttles progress callbacks and derives bytes/sec + ETA.
class TransferProgressReporter
{
public:
  using EmitFn = std::function<void(const TransferProgressInfo &info, double bytesPerSec, int etaSeconds)>;

  explicit TransferProgressReporter(EmitFn emit = {}, int minIntervalMs = 150);

  void setEmit(EmitFn emit);
  void reset();
  void report(const TransferProgressInfo &info, bool force = false);

private:
  EmitFn m_emit;
  int m_minIntervalMs = 150;
  std::chrono::steady_clock::time_point m_start{};
  std::chrono::steady_clock::time_point m_lastEmit{};
  uint64_t m_lastBytes = 0;
  bool m_started = false;
};

//! Build IPC detail after "progress|" (pipe-separated, name sanitized).
std::string formatTransferProgressDetail(
    const TransferProgressInfo &info, double bytesPerSec, int etaSeconds
);

} // namespace deskflow
