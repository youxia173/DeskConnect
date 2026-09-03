/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "filetransfer/FileTransfer.h"
#include "filetransfer/TransferProgress.h"
#include "deskflow/ProtocolTypes.h"

#include <QFile>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace deskflow {

//! Assembles incoming DDRG + DFTR into files under the receive directory.
class FileReceiveSession
{
public:
  using ProgressCallback = std::function<void(const TransferProgressInfo &)>;

  void reset();

  //! Handle DDRG basenames (expected order of following DFTR transfers).
  bool begin(const std::vector<std::string> &names, uint64_t maxTotalBytes, ProgressCallback onProgress = {});

  //! Handle one DFTR message (mark + payload). Returns Finished when all files done.
  TransferState onChunk(uint8_t mark, const std::string &data, uint64_t maxFileBytes);

  bool isActive() const
  {
    return m_active;
  }

  const std::vector<std::string> &receivedPaths() const
  {
    return m_receivedPaths;
  }

private:
  bool openNextFile(uint64_t expectedSize);
  void closeCurrent(bool success);
  void emitProgress();

  bool m_active = false;
  std::vector<std::string> m_names;
  std::vector<uint64_t> m_expectedSizes;
  size_t m_index = 0;
  uint64_t m_expectedSize = 0;
  uint64_t m_written = 0;
  uint64_t m_totalWritten = 0;
  uint64_t m_maxTotalBytes = 0;
  std::string m_receiveDir;
  std::string m_currentPath;
  QFile m_out;
  std::vector<std::string> m_receivedPaths;
  ProgressCallback m_onProgress;
};

} // namespace deskflow
