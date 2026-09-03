/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "filetransfer/FileSend.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <QFile>
#include <QString>

namespace deskflow {

namespace {
constexpr qint64 kFileChunkSize = 256 * 1024;
}

bool writeFileOffersToStream(IStream *stream, const std::vector<FileOffer> &offers)
{
  if (stream == nullptr || offers.empty() || !isFileTransferEnabled()) {
    return false;
  }

  const uint64_t maxBytes = maxTransferBytes();
  uint64_t total = 0;
  for (const auto &offer : offers) {
    total += offer.size;
  }
  if (maxBytes > 0 && total > maxBytes) {
    LOG_WARN(
        "clipboard file transfer skipped: %llu bytes exceeds limit %llu", static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(maxBytes)
    );
    return false;
  }

  const std::string drag = encodeDragInfo(offers);
  ProtocolUtil::writef(stream, kMsgDDragInfo, static_cast<uint16_t>(offers.size()), &drag);

  for (const auto &offer : offers) {
    QFile file(QString::fromStdString(offer.localPath));
    if (!file.open(QIODevice::ReadOnly)) {
      LOG_ERR("failed to open file for transfer: %s", offer.localPath.c_str());
      return false;
    }

    const std::string sizeStr = QString::number(static_cast<qulonglong>(offer.size)).toStdString();
    ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataStart), &sizeStr);

    uint64_t sent = 0;
    while (sent < offer.size) {
      const QByteArray chunk = file.read(kFileChunkSize);
      if (chunk.isEmpty() && sent < offer.size) {
        LOG_ERR("unexpected EOF transferring %s", offer.localPath.c_str());
        return false;
      }
      std::string payload(chunk.constData(), static_cast<size_t>(chunk.size()));
      ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataChunk), &payload);
      sent += static_cast<uint64_t>(chunk.size());
    }

    std::string empty;
    ProtocolUtil::writef(stream, kMsgDFileTransfer, static_cast<uint8_t>(ChunkType::DataEnd), &empty);
    LOG_INFO("sent file \"%s\" (%llu bytes)", offer.name.c_str(), static_cast<unsigned long long>(offer.size));
  }

  return true;
}

} // namespace deskflow
