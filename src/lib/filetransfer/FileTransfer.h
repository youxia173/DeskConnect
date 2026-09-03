/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace deskflow {

struct FileOffer
{
  std::string name;      //!< basename only (safe for receive)
  uint64_t size = 0;     //!< file size in bytes
  std::string localPath; //!< absolute local path (sender side)
};

//! Parse NUL-separated absolute paths into file offers (skips missing/dirs).
std::vector<FileOffer> fileOffersFromClipboardData(const std::string &filesData);

//! Encode absolute local paths as NUL-separated clipboard Files payload.
std::string clipboardDataFromPaths(const std::vector<std::string> &paths);

//! Convert NUL-separated Files payload to text/uri-list (file:// lines).
std::string uriListFromClipboardData(const std::string &filesData);

//! Convert text/uri-list to NUL-separated absolute paths (skips dirs/non-files).
std::string clipboardDataFromUriList(const std::string &uriList);

//! Keep only the file name component; reject empty / "." / ".." / path separators.
std::string sanitizeFileName(const std::string &name);

//! Encode basename list for DDRG (NUL-separated basenames).
std::string encodeDragInfo(const std::vector<FileOffer> &offers);

//! Decode DDRG payload into basename list.
std::vector<std::string> decodeDragInfo(const std::string &info);

//! Unique path under receiveDir for basename (auto-rename on conflict).
std::string uniqueReceivePath(const std::string &receiveDir, const std::string &baseName);

//! Default receive directory (Downloads/DeskConnect).
std::string defaultReceiveDirectory();

//! Max transfer size in bytes from settings (0 = disabled).
uint64_t maxTransferBytes();

//! Whether file transfer is enabled in settings.
bool isFileTransferEnabled();

//! Whether send pacing is enabled (keeps mouse/keyboard responsive).
bool isTransferSpeedLimited();

//! Target send rate in bytes/sec; 0 means unlimited (full speed).
uint64_t transferSpeedLimitBytesPerSec();

//! Ensure receive directory exists; returns absolute path or empty on failure.
std::string ensureReceiveDirectory();

} // namespace deskflow
