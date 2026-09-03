/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardPNGConverter.h"

#include "base/Log.h"
#include "platform/ClipboardImage.h"

MSWindowsClipboardPNGConverter::MSWindowsClipboardPNGConverter()
    : m_format(RegisterClipboardFormat(L"PNG"))
{
}

IClipboard::Format MSWindowsClipboardPNGConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

UINT MSWindowsClipboardPNGConverter::getWin32Format() const
{
  return m_format;
}

HANDLE MSWindowsClipboardPNGConverter::fromIClipboard(const std::string &data) const
{
  if (m_format == 0 || data.empty()) {
    return nullptr;
  }

  const auto png = deskflow::ClipboardImage::dibToPng(data);
  if (png.empty()) {
    return nullptr;
  }

  HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, png.size());
  if (gData == nullptr) {
    return nullptr;
  }

  void *dst = GlobalLock(gData);
  if (dst == nullptr) {
    GlobalFree(gData);
    return nullptr;
  }
  memcpy(dst, png.data(), png.size());
  GlobalUnlock(gData);
  return gData;
}

std::string MSWindowsClipboardPNGConverter::toIClipboard(HANDLE data) const
{
  if (data == nullptr) {
    return {};
  }

  const auto size = GlobalSize(data);
  const void *src = GlobalLock(data);
  if (src == nullptr || size == 0) {
    if (src != nullptr) {
      GlobalUnlock(data);
    }
    return {};
  }

  std::string png(static_cast<const char *>(src), size);
  GlobalUnlock(data);

  const auto dib = deskflow::ClipboardImage::pngToDib(png);
  if (!dib.empty()) {
    LOG_DEBUG("converted clipboard PNG (%zu bytes) to DIB (%zu bytes)", png.size(), dib.size());
  }
  return dib;
}
