/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "filetransfer/TransferProgress.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace deskflow {

TransferProgressReporter::TransferProgressReporter(EmitFn emit, int minIntervalMs)
    : m_emit(std::move(emit)),
      m_minIntervalMs(std::max(50, minIntervalMs))
{
}

void TransferProgressReporter::setEmit(EmitFn emit)
{
  m_emit = std::move(emit);
}

void TransferProgressReporter::reset()
{
  m_started = false;
  m_lastBytes = 0;
}

void TransferProgressReporter::report(const TransferProgressInfo &info, bool force)
{
  if (!m_emit) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!m_started) {
    m_started = true;
    m_start = now;
    m_lastEmit = now;
    m_lastBytes = info.bytesDone;
    m_emit(info, 0.0, -1);
    return;
  }

  const auto sinceEmit =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastEmit).count();
  const bool finished =
      info.bytesTotal > 0 && info.bytesDone >= info.bytesTotal && info.fileIndex + 1 >= info.fileCount;
  if (!force && !finished && sinceEmit < m_minIntervalMs) {
    return;
  }

  const auto elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count();
  double bps = 0.0;
  if (elapsedMs > 0) {
    bps = static_cast<double>(info.bytesDone) * 1000.0 / static_cast<double>(elapsedMs);
  }

  int eta = -1;
  if (bps > 1.0 && info.bytesTotal > info.bytesDone) {
    eta = static_cast<int>(std::lround(static_cast<double>(info.bytesTotal - info.bytesDone) / bps));
  } else if (info.bytesTotal > 0 && info.bytesDone >= info.bytesTotal) {
    eta = 0;
  }

  m_lastEmit = now;
  m_lastBytes = info.bytesDone;
  m_emit(info, bps, eta);
}

std::string formatTransferProgressDetail(
    const TransferProgressInfo &info, double bytesPerSec, int etaSeconds
)
{
  std::string name = info.name;
  for (char &ch : name) {
    if (ch == '|' || ch == '\n' || ch == '\r') {
      ch = '_';
    }
  }

  std::ostringstream out;
  out << (info.sending ? "send" : "recv") << '|' << name << '|' << info.fileIndex << '|' << info.fileCount
      << '|' << info.bytesDone << '|' << info.bytesTotal << '|' << static_cast<uint64_t>(bytesPerSec) << '|'
      << etaSeconds;
  return out.str();
}

} // namespace deskflow
