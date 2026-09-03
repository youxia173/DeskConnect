/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_5.h"

#include "base/Log.h"
#include "deskflow/ProtocolUtil.h"
#include "filetransfer/FileTransfer.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <cstring>

//
// ClientProxy1_5
//

ClientProxy1_5::ClientProxy1_5(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_4(name, stream, server, events)
{
  // do nothing
}

void ClientProxy1_5::sendDragInfo(uint32_t fileCount, const char *info, size_t size)
{
  std::string data(info, size);
  ProtocolUtil::writef(getStream(), kMsgDDragInfo, static_cast<uint16_t>(fileCount), &data);
}

void ClientProxy1_5::fileChunkSending(uint8_t mark, char *data, size_t dataSize)
{
  std::string chunk(data, dataSize);
  ProtocolUtil::writef(getStream(), kMsgDFileTransfer, mark, &chunk);
}

bool ClientProxy1_5::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDFileTransfer, 4) == 0) {
    fileChunkReceived();
  } else if (memcmp(code, kMsgDDragInfo, 4) == 0) {
    dragInfoReceived();
  } else {
    return ClientProxy1_4::parseMessage(code);
  }

  return true;
}

void ClientProxy1_5::fileChunkReceived()
{
  uint8_t mark = 0;
  std::string data;
  if (!ProtocolUtil::readf(getStream(), kMsgDFileTransfer + 4, &mark, &data)) {
    LOG_ERR("failed to read file transfer chunk from \"%s\"", getName().c_str());
    return;
  }
  m_server->onFileChunk(this, mark, data);
}

void ClientProxy1_5::dragInfoReceived()
{
  uint16_t fileCount = 0;
  std::string info;
  if (!ProtocolUtil::readf(getStream(), kMsgDDragInfo + 4, &fileCount, &info)) {
    LOG_ERR("failed to read drag info from \"%s\"", getName().c_str());
    return;
  }
  m_server->onDragInfo(this, fileCount, info);
}
