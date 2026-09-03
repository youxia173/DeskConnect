/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ClipboardImage.h"

#include "base/Log.h"

#include <QBuffer>
#include <QDataStream>
#include <QImage>
#include <QtEndian>

#include <cstring>

namespace deskflow {

namespace {
constexpr int kBmpFileHeaderSize = 14;
constexpr int kBmpSignatureSize = 2;
constexpr quint32 kMinDibHeaderSize = 40;
} // namespace

QByteArray ClipboardImage::dibToBmpFile(const QByteArray &dib)
{
  if (dib.size() < static_cast<qint64>(sizeof(quint32))) {
    return {};
  }

  quint32 headerSize = 0;
  std::memcpy(&headerSize, dib.constData(), sizeof(headerSize));
  headerSize = qFromLittleEndian(headerSize);
  if (headerSize < kMinDibHeaderSize || headerSize > static_cast<quint32>(dib.size())) {
    return {};
  }

  const auto fileSize = static_cast<quint32>(kBmpFileHeaderSize + dib.size());
  const quint32 pixelOffset = kBmpFileHeaderSize + headerSize;

  QByteArray bmp;
  QDataStream ds(&bmp, QIODevice::WriteOnly);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds.writeRawData("BM", kBmpSignatureSize);
  ds << fileSize;
  ds << quint32(0);
  ds << pixelOffset;
  ds.writeRawData(dib.constData(), static_cast<int>(dib.size()));
  return bmp;
}

QByteArray ClipboardImage::bmpFileToDib(const QByteArray &bmp)
{
  if (bmp.size() < kBmpFileHeaderSize) {
    return {};
  }
  return bmp.mid(kBmpFileHeaderSize);
}

std::string ClipboardImage::pngToDib(const std::string &png)
{
  if (png.empty()) {
    return {};
  }

  QImage image;
  if (!image.loadFromData(reinterpret_cast<const uchar *>(png.data()), static_cast<int>(png.size()), "PNG")) {
    LOG_DEBUG("clipboard png decode failed (%zu bytes)", png.size());
    return {};
  }

  // Prefer 32bpp so alpha survives; fall back to 24bpp RGB.
  if (image.hasAlphaChannel()) {
    image = image.convertToFormat(QImage::Format_ARGB32);
  } else {
    image = image.convertToFormat(QImage::Format_RGB888);
  }

  QByteArray bmp;
  QBuffer buf(&bmp);
  buf.open(QIODevice::WriteOnly);
  if (!image.save(&buf, "BMP")) {
    LOG_WARN("failed to encode clipboard image as bmp");
    return {};
  }

  const auto dib = bmpFileToDib(bmp);
  return std::string(dib.constData(), static_cast<size_t>(dib.size()));
}

std::string ClipboardImage::dibToPng(const std::string &dib)
{
  if (dib.empty()) {
    return {};
  }

  const auto bmpFile = dibToBmpFile(QByteArray(dib.data(), static_cast<int>(dib.size())));
  if (bmpFile.isEmpty()) {
    LOG_DEBUG("clipboard dib is malformed (%zu bytes)", dib.size());
    return {};
  }

  QImage image;
  if (!image.loadFromData(bmpFile, "BMP")) {
    LOG_DEBUG("clipboard bmp decode failed");
    return {};
  }

  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  if (!image.save(&buf, "PNG")) {
    LOG_WARN("failed to encode clipboard image as png");
    return {};
  }

  return std::string(png.constData(), static_cast<size_t>(png.size()));
}

} // namespace deskflow
