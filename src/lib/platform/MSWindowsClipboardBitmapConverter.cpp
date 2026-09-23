/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardBitmapConverter.h"

#include "base/Log.h"

#include <QtEndian>

#include <cstdlib>
#include <limits>

namespace {
bool normaliseMalformedMacDib(const std::string &data, std::string &normalisedData)
{
  if (data.size() < sizeof(BITMAPINFOHEADER)) {
    return false;
  }

  const auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(data.data());
  if (header->biWidth <= 0) {
    return false;
  }

  const auto width = static_cast<size_t>(header->biWidth);
  const auto height = static_cast<size_t>(std::abs(static_cast<int64_t>(header->biHeight)));
  if (height == 0 || width > (std::numeric_limits<size_t>::max() - sizeof(BITMAPINFOHEADER)) / 4 / height) {
    return false;
  }
  const auto expectedSize = sizeof(BITMAPINFOHEADER) + width * height * 4;

  // macOS can describe an INFOHEADER-sized 32-bit pixel payload as a V5 DIB.
  // Windows then interprets the first pixels as V5 colour masks. The pixel
  // bytes are ordinary BGRA, so publish a canonical BI_RGB DIB instead.
  if (header->biSize <= sizeof(BITMAPINFOHEADER) || header->biPlanes != 1 || header->biBitCount != 32 ||
      header->biCompression != BI_BITFIELDS || expectedSize != data.size()) {
    return false;
  }

  normalisedData = data.substr(0, sizeof(BITMAPINFOHEADER));
  qToLittleEndian<quint32>(sizeof(BITMAPINFOHEADER), reinterpret_cast<quint8 *>(&normalisedData[0]));
  qToLittleEndian<quint32>(BI_RGB, reinterpret_cast<quint8 *>(&normalisedData[0]) + 16);
  normalisedData += data.substr(sizeof(BITMAPINFOHEADER));
  LOG_INFO("normalised malformed macOS clipboard image to BI_RGB");
  return true;
}
} // namespace

//
// MSWindowsClipboardBitmapConverter
//

IClipboard::Format MSWindowsClipboardBitmapConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

UINT MSWindowsClipboardBitmapConverter::getWin32Format() const
{
  return CF_DIB;
}

HANDLE
MSWindowsClipboardBitmapConverter::fromIClipboard(const std::string &data) const
{
  std::string normalisedData;
  const auto *clipboardData = &data;
  if (normaliseMalformedMacDib(data, normalisedData)) {
    clipboardData = &normalisedData;
  }

  // copy to memory handle
  HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, clipboardData->size());
  if (gData != nullptr) {
    // get a pointer to the allocated memory
    char *dst = (char *)GlobalLock(gData);
    if (dst != nullptr) {
      memcpy(dst, clipboardData->data(), clipboardData->size());
      GlobalUnlock(gData);
    } else {
      GlobalFree(gData);
      gData = nullptr;
    }
  }

  return gData;
}

std::string MSWindowsClipboardBitmapConverter::toIClipboard(HANDLE data) const
{
  // get datator
  LPVOID src = GlobalLock(data);
  if (src == nullptr) {
    return std::string();
  }
  const size_t srcSize = GlobalSize(data);
  if (srcSize < sizeof(BITMAPINFOHEADER)) {
    GlobalUnlock(data);
    return {};
  }

  // check image type
  const BITMAPINFO *bitmap = static_cast<const BITMAPINFO *>(src);
  const auto &header = bitmap->bmiHeader;
  const auto depth = header.biBitCount;
  if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > srcSize || header.biWidth <= 0 ||
      header.biHeight == 0 || header.biHeight == std::numeric_limits<LONG>::min() || header.biPlanes != 1 ||
      (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32) ||
      (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS)) {
    GlobalUnlock(data);
    return {};
  }
  const uint64_t height = static_cast<uint64_t>(std::abs(static_cast<int64_t>(header.biHeight)));
  const uint64_t stride = ((static_cast<uint64_t>(header.biWidth) * depth + 31) / 32) * 4;
  uint64_t pixelOffset = header.biSize;
  // V4/V5 headers already contain their channel masks.
  if (header.biCompression == BI_BITFIELDS && header.biSize == sizeof(BITMAPINFOHEADER)) {
    pixelOffset += 3 * sizeof(DWORD);
  }
  const uint64_t colors = header.biClrUsed != 0 ? header.biClrUsed : (depth < 16 ? (1ull << depth) : 0);
  pixelOffset += colors * sizeof(RGBQUAD);
  if (pixelOffset > srcSize || stride * height > srcSize - pixelOffset) {
    LOG_WARN("truncated clipboard bitmap");
    GlobalUnlock(data);
    return {};
  }
  LOG(
      (CLOG_INFO "bitmap: %dx%d %d", bitmap->bmiHeader.biWidth, bitmap->bmiHeader.biHeight,
       (int)bitmap->bmiHeader.biBitCount)
  );
  if (bitmap->bmiHeader.biPlanes == 1 && (bitmap->bmiHeader.biBitCount == 24 || bitmap->bmiHeader.biBitCount == 32) &&
      bitmap->bmiHeader.biCompression == BI_RGB) {
    // already in canonical form
    std::string image(static_cast<char const *>(src), srcSize);
    GlobalUnlock(data);
    return image;
  }

  // create a destination DIB section
  LOG_INFO("convert image from: depth=%d comp=%d", bitmap->bmiHeader.biBitCount, bitmap->bmiHeader.biCompression);
  void *raw;
  BITMAPINFOHEADER info;
  LONG w = bitmap->bmiHeader.biWidth;
  LONG h = bitmap->bmiHeader.biHeight;
  const LONG absHeight = (h < 0) ? -h : h;
  info.biSize = sizeof(BITMAPINFOHEADER);
  info.biWidth = w;
  info.biHeight = h;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  info.biSizeImage = 0;
  info.biXPelsPerMeter = 1000;
  info.biYPelsPerMeter = 1000;
  info.biClrUsed = 0;
  info.biClrImportant = 0;
  HDC dc = GetDC(nullptr);
  HBITMAP dst = CreateDIBSection(dc, (BITMAPINFO *)&info, DIB_RGB_COLORS, &raw, nullptr, 0);
  if (dst == nullptr || raw == nullptr) {
    LOG_WARN("failed to allocate destination bitmap for clipboard image");
    ReleaseDC(nullptr, dc);
    GlobalUnlock(data);
    return std::string();
  }

  // find the start of the pixel data
  const char *srcBits = static_cast<const char *>(src) + pixelOffset;

  // copy source image to destination image
  HDC dstDC = CreateCompatibleDC(dc);
  HGDIOBJ oldBitmap = SelectObject(dstDC, dst);
  SetDIBitsToDevice(dstDC, 0, 0, w, absHeight, 0, 0, 0, absHeight, srcBits, bitmap, DIB_RGB_COLORS);
  SelectObject(dstDC, oldBitmap);
  DeleteDC(dstDC);
  GdiFlush();

  // extract data
  std::string image((const char *)&info, info.biSize);
  image.append((const char *)raw, static_cast<size_t>(4) * static_cast<size_t>(w) * static_cast<size_t>(absHeight));

  // clean up GDI
  DeleteObject(dst);
  ReleaseDC(nullptr, dc);

  // release handle
  GlobalUnlock(data);

  return image;
}
