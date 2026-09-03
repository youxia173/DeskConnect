/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "filetransfer/FileReceiveSession.h"

#include "base/Log.h"

#include <QString>

namespace deskflow {

void FileReceiveSession::reset()
{
  closeCurrent(false);
  m_active = false;
  m_names.clear();
  m_index = 0;
  m_expectedSize = 0;
  m_written = 0;
  m_totalWritten = 0;
  m_maxTotalBytes = 0;
  m_receiveDir.clear();
  m_currentPath.clear();
  m_receivedPaths.clear();
}

bool FileReceiveSession::begin(const std::vector<std::string> &names, uint64_t maxTotalBytes)
{
  reset();
  if (names.empty()) {
    return false;
  }
  m_receiveDir = ensureReceiveDirectory();
  if (m_receiveDir.empty()) {
    return false;
  }
  m_names = names;
  m_maxTotalBytes = maxTotalBytes;
  m_active = true;
  LOG_INFO("receiving %zu file(s) via clipboard transfer", m_names.size());
  return true;
}

bool FileReceiveSession::openNextFile(uint64_t expectedSize)
{
  closeCurrent(false);
  if (m_index >= m_names.size()) {
    LOG_ERR("file transfer: unexpected extra file");
    return false;
  }
  if (m_maxTotalBytes > 0 && m_totalWritten + expectedSize > m_maxTotalBytes) {
    LOG_ERR("file transfer exceeds size limit");
    return false;
  }

  m_currentPath = uniqueReceivePath(m_receiveDir, m_names[m_index]);
  if (m_currentPath.empty()) {
    LOG_ERR("file transfer: could not allocate receive path");
    return false;
  }

  // QFile handles UTF-8 paths on Windows; std::ofstream does not.
  m_out.setFileName(QString::fromUtf8(m_currentPath.data(), static_cast<qsizetype>(m_currentPath.size())));
  if (!m_out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    LOG_ERR("file transfer: failed to open %s for write", m_currentPath.c_str());
    return false;
  }

  m_expectedSize = expectedSize;
  m_written = 0;
  LOG_INFO(
      "receiving file \"%s\" (%llu bytes)", m_names[m_index].c_str(), static_cast<unsigned long long>(expectedSize)
  );
  return true;
}

void FileReceiveSession::closeCurrent(bool success)
{
  if (m_out.isOpen()) {
    m_out.close();
  }
  if (!m_currentPath.empty()) {
    if (success) {
      m_receivedPaths.push_back(m_currentPath);
      m_totalWritten += m_written;
      ++m_index;
    } else {
      QFile::remove(QString::fromUtf8(m_currentPath.data(), static_cast<qsizetype>(m_currentPath.size())));
    }
    m_currentPath.clear();
  }
  m_expectedSize = 0;
  m_written = 0;
}

TransferState FileReceiveSession::onChunk(uint8_t mark, const std::string &data, uint64_t maxFileBytes)
{
  using enum TransferState;
  if (!m_active) {
    return Error;
  }

  if (mark == ChunkType::DataStart) {
    bool ok = false;
    const auto expected = QString::fromUtf8(data.data(), static_cast<qsizetype>(data.size())).toULongLong(&ok);
    if (!ok) {
      LOG_ERR("file transfer: invalid size header");
      reset();
      return Error;
    }
    if (maxFileBytes > 0 && expected > maxFileBytes) {
      LOG_ERR("file transfer: file too large");
      reset();
      return Error;
    }
    if (!openNextFile(static_cast<uint64_t>(expected))) {
      reset();
      return Error;
    }
    return Started;
  }

  if (mark == ChunkType::DataChunk) {
    if (!m_out.isOpen()) {
      LOG_ERR("file transfer: data before start");
      reset();
      return Error;
    }
    if (m_written + data.size() > m_expectedSize) {
      LOG_ERR("file transfer: chunk exceeds declared size");
      reset();
      return Error;
    }
    const qint64 written = m_out.write(data.data(), static_cast<qint64>(data.size()));
    if (written < 0 || static_cast<size_t>(written) != data.size()) {
      LOG_ERR("file transfer: write failed");
      reset();
      return Error;
    }
    m_written += data.size();
    return InProgress;
  }

  if (mark == ChunkType::DataEnd) {
    if (!m_out.isOpen() && m_expectedSize != 0) {
      LOG_ERR("file transfer: end before start");
      reset();
      return Error;
    }
    // Zero-byte files: openNextFile left stream open with expectedSize 0.
    if (m_out.isOpen() || m_expectedSize == 0) {
      if (m_written != m_expectedSize) {
        LOG_ERR(
            "file transfer: size mismatch expected=%llu actual=%llu", static_cast<unsigned long long>(m_expectedSize),
            static_cast<unsigned long long>(m_written)
        );
        reset();
        return Error;
      }
      closeCurrent(true);
    }
    if (m_index >= m_names.size()) {
      m_active = false;
      LOG_INFO("clipboard file transfer complete (%zu file(s))", m_receivedPaths.size());
      return Finished;
    }
    return InProgress;
  }

  LOG_ERR("file transfer: unknown mark %u", mark);
  reset();
  return Error;
}

} // namespace deskflow
