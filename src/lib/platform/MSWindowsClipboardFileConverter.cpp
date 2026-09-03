/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFileConverter.h"

#include "base/Log.h"

#include <QString>

#include <shellapi.h>
#include <shlobj.h>
#include <vector>

IClipboard::Format MSWindowsClipboardFileConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFileConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFileConverter::fromIClipboard(const std::string &data) const
{
  if (data.empty()) {
    return nullptr;
  }

  std::vector<std::wstring> paths;
  size_t start = 0;
  while (start < data.size()) {
    const size_t end = data.find('\0', start);
    const size_t len = (end == std::string::npos) ? (data.size() - start) : (end - start);
    if (len > 0) {
      paths.push_back(QString::fromUtf8(data.data() + start, static_cast<qsizetype>(len)).toStdWString());
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  if (paths.empty()) {
    return nullptr;
  }

  size_t chars = 1; // final double-NUL
  for (const auto &path : paths) {
    chars += path.size() + 1;
  }

  const SIZE_T bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GHND | GMEM_DDESHARE, bytes);
  if (handle == nullptr) {
    return nullptr;
  }

  auto *drop = static_cast<DROPFILES *>(GlobalLock(handle));
  if (drop == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  drop->pFiles = sizeof(DROPFILES);
  drop->fWide = TRUE;
  drop->fNC = FALSE;
  drop->pt.x = 0;
  drop->pt.y = 0;

  auto *dest = reinterpret_cast<wchar_t *>(reinterpret_cast<char *>(drop) + sizeof(DROPFILES));
  for (const auto &path : paths) {
    memcpy(dest, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    dest += path.size() + 1;
  }
  *dest = L'\0';

  GlobalUnlock(handle);
  LOG_DEBUG("prepared CF_HDROP with %zu file(s)", paths.size());
  return handle;
}

std::string MSWindowsClipboardFileConverter::toIClipboard(HANDLE data) const
{
  if (data == nullptr) {
    return {};
  }

  auto *drop = static_cast<HDROP>(data);
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  if (count == 0) {
    return {};
  }

  std::string result;
  for (UINT i = 0; i < count; ++i) {
    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
    if (length == 0) {
      continue;
    }
    std::wstring path(length + 1, L'\0');
    DragQueryFileW(drop, i, path.data(), length + 1);
    path.resize(length);

    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      LOG_INFO("skipping directory in clipboard file list: %s", QString::fromStdWString(path).toUtf8().constData());
      continue;
    }

    const QByteArray utf8 = QString::fromStdWString(path).toUtf8();
    if (!utf8.isEmpty()) {
      result.append(utf8.constData(), static_cast<size_t>(utf8.size()));
      result.push_back('\0');
    }
  }

  return result;
}
