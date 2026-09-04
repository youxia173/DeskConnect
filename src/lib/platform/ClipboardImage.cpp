/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ClipboardImage.h"

#include "base/Log.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QUrl>
#include <QtEndian>

#include <cstring>

namespace deskflow {

namespace {
constexpr int kBmpFileHeaderSize = 14;
constexpr int kBmpSignatureSize = 2;
constexpr quint32 kMinDibHeaderSize = 40;

std::string qImageToDib(QImage image)
{
  if (image.isNull()) {
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

  const auto dib = ClipboardImage::bmpFileToDib(bmp);
  return std::string(dib.constData(), static_cast<size_t>(dib.size()));
}
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

  const auto *bytes = reinterpret_cast<const uchar *>(png.data());
  const int size = static_cast<int>(png.size());

  QImage image;
  // Auto-detect first: QQ often labels JPEG/WebP as image/png.
  if (!image.loadFromData(bytes, size) && !image.loadFromData(bytes, size, "PNG") &&
      !image.loadFromData(bytes, size, "JPEG") && !image.loadFromData(bytes, size, "JPG") &&
      !image.loadFromData(bytes, size, "WEBP") && !image.loadFromData(bytes, size, "GIF") &&
      !image.loadFromData(bytes, size, "BMP")) {
    LOG_DEBUG(
        "clipboard image decode failed (%zu bytes, magic=%02x%02x%02x%02x)", png.size(),
        size > 0 ? bytes[0] : 0, size > 1 ? bytes[1] : 0, size > 2 ? bytes[2] : 0, size > 3 ? bytes[3] : 0
    );
    return {};
  }

  return qImageToDib(image);
}

std::string ClipboardImage::fileToDib(const std::string &path)
{
  if (path.empty()) {
    return {};
  }
  QImage image(QString::fromStdString(path));
  if (image.isNull()) {
    LOG_DEBUG("clipboard image file load failed: %s", path.c_str());
    return {};
  }
  LOG_DEBUG("loaded clipboard image from file (%s)", path.c_str());
  return qImageToDib(image);
}

std::string ClipboardImage::dibFromHtml(const std::string &html)
{
  if (html.empty()) {
    return {};
  }

  const QString text = QString::fromUtf8(html.data(), static_cast<int>(html.size()));
  static const QRegularExpression re(
      QStringLiteral(R"((?:src|href)\s*=\s*["']([^"']+)["'])"), QRegularExpression::CaseInsensitiveOption
  );

  QRegularExpressionMatchIterator it = re.globalMatch(text);
  while (it.hasNext()) {
    const QString ref = it.next().captured(1).trimmed();
    if (ref.isEmpty() || ref.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
      continue;
    }

    QString path;
    const QUrl url(ref);
    if (url.isLocalFile()) {
      path = url.toLocalFile();
    } else if (ref.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
      path = QUrl(ref).toLocalFile();
    } else if (ref.startsWith(QLatin1Char('/'))) {
      path = ref;
    } else {
      continue;
    }

    if (path.isEmpty() || !QFile::exists(path)) {
      continue;
    }

    auto dib = fileToDib(path.toStdString());
    if (!dib.empty()) {
      return dib;
    }
  }

  return {};
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
