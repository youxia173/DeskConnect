/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "filetransfer/FileTransfer.h"

#include "base/Log.h"
#include "common/Constants.h"
#include "common/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace deskflow {

namespace {

std::vector<std::string> splitNul(const std::string &data)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < data.size()) {
    const size_t end = data.find('\0', start);
    const size_t len = (end == std::string::npos) ? (data.size() - start) : (end - start);
    if (len > 0) {
      parts.emplace_back(data.substr(start, len));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

} // namespace

std::string sanitizeFileName(const std::string &name)
{
  QFileInfo info(QString::fromStdString(name));
  const QString base = info.fileName();
  if (base.isEmpty() || base == QLatin1String(".") || base == QLatin1String("..") || base.contains(QLatin1Char('/')) ||
      base.contains(QLatin1Char('\\'))) {
    return {};
  }
  return base.toStdString();
}

std::vector<FileOffer> fileOffersFromClipboardData(const std::string &filesData)
{
  std::vector<FileOffer> offers;
  for (const auto &path : splitNul(filesData)) {
    QFileInfo info(QString::fromStdString(path));
    if (!info.exists() || !info.isFile()) {
      LOG_INFO("skipping non-file clipboard path: %s", path.c_str());
      continue;
    }
    FileOffer offer;
    offer.localPath = info.absoluteFilePath().toStdString();
    offer.name = sanitizeFileName(info.fileName().toStdString());
    offer.size = static_cast<uint64_t>(info.size());
    if (offer.name.empty()) {
      continue;
    }
    offers.push_back(std::move(offer));
  }
  return offers;
}

std::string clipboardDataFromPaths(const std::vector<std::string> &paths)
{
  std::string data;
  for (const auto &path : paths) {
    if (path.empty()) {
      continue;
    }
    data.append(path);
    data.push_back('\0');
  }
  return data;
}

std::string uriListFromClipboardData(const std::string &filesData)
{
  QStringList lines;
  for (const auto &path : splitNul(filesData)) {
    if (path.empty()) {
      continue;
    }
    const QUrl url = QUrl::fromLocalFile(QString::fromStdString(path));
    if (url.isValid()) {
      lines.append(url.toString(QUrl::FullyEncoded));
    }
  }
  if (lines.isEmpty()) {
    return {};
  }
  return lines.join(QLatin1Char('\n')).append(QLatin1Char('\n')).toStdString();
}

std::string clipboardDataFromUriList(const std::string &uriList)
{
  std::string data;
  const QString text = QString::fromStdString(uriList);
  const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    QUrl url(line);
    if (!url.isValid()) {
      continue;
    }
    // Accept file:// and bare local paths some apps emit.
    QString local;
    if (url.isLocalFile()) {
      local = url.toLocalFile();
    } else if (line.startsWith(QLatin1Char('/'))) {
      local = line;
    } else {
      continue;
    }

    QFileInfo info(local);
    if (!info.exists() || !info.isFile()) {
      LOG_INFO("skipping non-file uri-list entry: %s", qPrintable(local));
      continue;
    }
    const std::string path = info.absoluteFilePath().toStdString();
    data.append(path);
    data.push_back('\0');
  }
  return data;
}

std::string encodeDragInfo(const std::vector<FileOffer> &offers)
{
  std::string data;
  for (const auto &offer : offers) {
    data.append(offer.name);
    data.push_back('\0');
  }
  return data;
}

std::vector<std::string> decodeDragInfo(const std::string &info)
{
  std::vector<std::string> names;
  for (const auto &part : splitNul(info)) {
    const auto name = sanitizeFileName(part);
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return names;
}

std::string uniqueReceivePath(const std::string &receiveDir, const std::string &baseName)
{
  const QString dir = QString::fromStdString(receiveDir);
  const QString name = QString::fromStdString(baseName);
  QFileInfo candidate(dir, name);
  if (!candidate.exists()) {
    return candidate.absoluteFilePath().toStdString();
  }

  const QString stem = candidate.completeBaseName();
  const QString suffix = candidate.suffix();
  for (int i = 1; i < 10000; ++i) {
    const QString numbered =
        suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(stem).arg(i)
                         : QStringLiteral("%1 (%2).%3").arg(stem).arg(i).arg(suffix);
    QFileInfo next(dir, numbered);
    if (!next.exists()) {
      return next.absoluteFilePath().toStdString();
    }
  }
  return {};
}

std::string defaultReceiveDirectory()
{
  const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  return QDir(downloads).filePath(QString::fromUtf8(kAppName)).toStdString();
}

uint64_t maxTransferBytes()
{
  const auto mb = Settings::value(Settings::FileTransfer::MaxSizeMb).toULongLong();
  return mb * 1024ull * 1024ull;
}

bool isFileTransferEnabled()
{
  return Settings::value(Settings::FileTransfer::Enabled).toBool();
}

std::string ensureReceiveDirectory()
{
  QString dir = Settings::value(Settings::FileTransfer::ReceiveDir).toString();
  if (dir.isEmpty()) {
    dir = QString::fromStdString(defaultReceiveDirectory());
  }
  if (!QDir().mkpath(dir)) {
    LOG_ERR("failed to create file receive directory: %s", qPrintable(dir));
    return {};
  }
  return QDir(dir).absolutePath().toStdString();
}

} // namespace deskflow
