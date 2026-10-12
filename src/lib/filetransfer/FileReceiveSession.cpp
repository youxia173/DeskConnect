/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "filetransfer/FileReceiveSession.h"

#include "base/Log.h"

#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <limits>
#include <thread>

namespace deskflow {

void FileReceiveSession::reset()
{
  const bool incomplete = m_active;
  closeCurrent(false);
  if (incomplete && !m_receiveDir.empty()) {
    const auto directory = QString::fromStdString(m_receiveDir);
    try {
      // A cancelled tree may contain thousands of files. Cleanup must not
      // hold the event thread (and Linux input grabs) while deleting them.
      std::thread([directory]() { QDir(directory).removeRecursively(); }).detach();
    } catch (...) {
      LOG_WARN("could not schedule incomplete transfer cleanup: %s", m_receiveDir.c_str());
    }
  }
  m_active = false;
  m_names.clear();
  m_expectedSizes.clear();
  m_index = 0;
  m_expectedSize = 0;
  m_written = 0;
  m_totalWritten = 0;
  m_maxTotalBytes = 0;
  m_receiveDir.clear();
  m_currentPath.clear();
  m_receivedPaths.clear();
  m_onProgress = {};
}

bool FileReceiveSession::begin(
    const std::vector<std::string> &names, uint64_t maxTotalBytes, ProgressCallback onProgress
)
{
  reset();
  if (names.empty()) {
    return false;
  }
  QSet<QString> entries;
  QSet<QString> directories;
  if (names.size() > 65535) {
    return false;
  }
  for (const auto &name : names) {
    QString relative = QString::fromStdString(name);
    if (relative.endsWith('/')) {
      relative.chop(1);
    }
    if (!isSafeTransferPath(name) || entries.contains(relative.toCaseFolded())) {
      return false;
    }
    const auto slash = relative.lastIndexOf('/');
    if (slash >= 0 && !directories.contains(relative.left(slash).toCaseFolded())) {
      return false;
    }
    if (name.back() == '/') {
      directories.insert(relative.toCaseFolded());
    }
    entries.insert(relative.toCaseFolded());
  }
  const auto base = ensureReceiveDirectory();
  if (base.empty()) {
    return false;
  }
  // Each transfer owns a new staging directory. Never traverse an existing
  // destination tree (including symlinks), overwrite files or expose partial roots.
  QTemporaryDir directory(QDir(QString::fromStdString(base)).filePath("DeskConnect-XXXXXX"));
  if (!directory.isValid()) {
    return false;
  }
  directory.setAutoRemove(false);
  m_receiveDir = directory.path().toUtf8().toStdString();
  m_names = names;
  m_expectedSizes.assign(names.size(), 0);
  m_maxTotalBytes = maxTotalBytes;
  m_onProgress = std::move(onProgress);
  m_active = true;
  LOG_INFO("receiving %zu file(s) via clipboard transfer", m_names.size());
  emitProgress();
  return true;
}

bool FileReceiveSession::openNextFile(uint64_t expectedSize)
{
  closeCurrent(false);
  if (m_index >= m_names.size()) {
    LOG_ERR("file transfer: unexpected extra file");
    return false;
  }
  if (expectedSize > static_cast<uint64_t>(std::numeric_limits<qint64>::max()) ||
      expectedSize > std::numeric_limits<uint64_t>::max() - m_totalWritten ||
      (m_maxTotalBytes > 0 &&
       (m_totalWritten > m_maxTotalBytes || expectedSize > m_maxTotalBytes - m_totalWritten))) {
    LOG_ERR("file transfer exceeds size limit");
    return false;
  }

  const auto &name = m_names[m_index];
  m_directory = name.back() == '/';
  const QString relative = QString::fromStdString(m_directory ? name.substr(0, name.size() - 1) : name);
  const QString path = QDir(QString::fromStdString(m_receiveDir)).filePath(relative);
  m_currentPath = path.toUtf8().toStdString();
  if (m_currentPath.empty()) {
    LOG_ERR("file transfer: could not allocate receive path");
    return false;
  }

  // QFile handles UTF-8 paths on Windows; std::ofstream does not.
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  if (m_directory) {
    if (expectedSize != 0 || !QDir().mkpath(path)) {
      return false;
    }
    m_expectedSize = 0;
    m_expectedSizes[m_index] = 0;
    m_written = 0;
    return true;
  }
  m_out.setFileName(QString::fromUtf8(m_currentPath.data(), static_cast<qsizetype>(m_currentPath.size())));
  if (!m_out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    LOG_ERR("file transfer: failed to open %s for write", m_currentPath.c_str());
    // We do not own this path when exclusive creation fails.
    m_currentPath.clear();
    return false;
  }

  m_expectedSize = expectedSize;
  m_expectedSizes[m_index] = expectedSize;
  m_written = 0;
  LOG_INFO(
      "receiving file \"%s\" (%llu bytes)", m_names[m_index].c_str(), static_cast<unsigned long long>(expectedSize)
  );
  emitProgress();
  return true;
}

void FileReceiveSession::closeCurrent(bool success)
{
  if (m_out.isOpen()) {
    m_out.close();
  }
  if (!m_currentPath.empty()) {
    if (success) {
      auto relative = m_names[m_index];
      if (relative.back() == '/') {
        relative.pop_back();
      }
      if (relative.find('/') == std::string::npos) {
        m_receivedPaths.push_back(m_currentPath);
      }
      m_totalWritten += m_written;
      ++m_index;
    } else {
      QFile::remove(QString::fromUtf8(m_currentPath.data(), static_cast<qsizetype>(m_currentPath.size())));
    }
    m_currentPath.clear();
  }
  m_expectedSize = 0;
  m_written = 0;
  m_directory = false;
}

void FileReceiveSession::emitProgress()
{
  if (!m_onProgress || m_names.empty()) {
    return;
  }

  TransferProgressInfo info;
  info.sending = false;
  info.fileCount = m_names.size();
  info.fileIndex = std::min(m_index, m_names.size() - 1);
  info.name = m_names[info.fileIndex];
  info.bytesDone = m_totalWritten + m_written;

  uint64_t knownTotal = 0;
  size_t knownCount = 0;
  for (uint64_t size : m_expectedSizes) {
    if (size > 0) {
      knownTotal += size;
      ++knownCount;
    }
  }
  if (knownCount == m_names.size()) {
    info.bytesTotal = knownTotal;
  } else if (knownCount > 0) {
    // Estimate remaining unknown files from the average of known sizes.
    const double avg = static_cast<double>(knownTotal) / static_cast<double>(knownCount);
    info.bytesTotal = knownTotal + static_cast<uint64_t>(avg * static_cast<double>(m_names.size() - knownCount));
  } else {
    info.bytesTotal = 0;
  }

  m_onProgress(info);
}

TransferState FileReceiveSession::onChunk(uint8_t mark, const std::string &data, uint64_t maxFileBytes)
{
  using enum TransferState;
  if (!m_active) {
    return Error;
  }

  if (mark == ChunkType::DataStart) {
    if (m_out.isOpen() || m_directory) {
      LOG_ERR("file transfer: start before previous file ended");
      reset();
      return Error;
    }
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
    if (!m_out.isOpen() || m_directory) {
      LOG_ERR("file transfer: data before start");
      reset();
      return Error;
    }
    if (m_written > m_expectedSize || data.size() > m_expectedSize - m_written) {
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
    emitProgress();
    return InProgress;
  }

  if (mark == ChunkType::DataEnd) {
    if (!m_out.isOpen() && !m_directory) {
      LOG_ERR("file transfer: end before start");
      reset();
      return Error;
    }
    // Zero-byte files: openNextFile left stream open with expectedSize 0.
    if (m_out.isOpen() || m_directory) {
      if (m_written != m_expectedSize) {
        LOG_ERR(
            "file transfer: size mismatch expected=%llu actual=%llu", static_cast<unsigned long long>(m_expectedSize),
            static_cast<unsigned long long>(m_written)
        );
        reset();
        return Error;
      }
      if (!m_directory && !m_out.flush()) {
        LOG_ERR("file transfer: failed to flush received file");
        reset();
        return Error;
      }
      closeCurrent(true);
      emitProgress();
    }
    if (m_index >= m_names.size()) {
      m_active = false;
      LOG_INFO("clipboard file transfer complete (%zu file(s))", m_receivedPaths.size());
      emitProgress();
      return Finished;
    }
    return InProgress;
  }

  LOG_ERR("file transfer: unknown mark %u", mark);
  // Control marks (e.g. full-speed handshake) are handled by the proxy/server.
  // Ignore unknown marks so a peer with newer protocol does not abort the file.
  return InProgress;
}

} // namespace deskflow
