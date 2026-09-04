/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <string>

namespace deskflow {

//! Convert clipboard bitmap payloads between PNG and DIB (BMP without file header).
class ClipboardImage
{
public:
  //! Convert image bytes (PNG/JPEG/etc.) to IClipboard::Format::Bitmap (DIB) bytes.
  static std::string pngToDib(const std::string &png);

  //! Convert IClipboard::Format::Bitmap (DIB) bytes to PNG bytes.
  static std::string dibToPng(const std::string &dib);

  //! Load an image file from a local path into DIB bytes.
  static std::string fileToDib(const std::string &path);

  //! If HTML contains a local file:// (or path) img src, load it as DIB.
  static std::string dibFromHtml(const std::string &html);

  static QByteArray dibToBmpFile(const QByteArray &dib);
  static QByteArray bmpFileToDib(const QByteArray &bmp);
};

} // namespace deskflow
