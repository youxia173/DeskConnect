/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/Server.h"

void ServerTests::SwitchToScreenInfo_alloc_screen()
{
  auto actual = new Server::SwitchToScreenInfo("test");
  QCOMPARE(actual->m_screen, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndSceens()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_screens, "test");
  delete info;
}

void ServerTests::clientCloseQueuesMessageWithoutWaitingForPeer()
{
  class BackloggedStream : public deskflow::IStream
  {
  public:
    QByteArray output;
    bool waitedForPeer = false;
    bool closed = false;
    void close() override
    {
      closed = true;
    }
    uint32_t read(void *, uint32_t) override
    {
      return 0;
    }
    void write(const void *data, uint32_t size) override
    {
      output.append(static_cast<const char *>(data), size);
    }
    void flush() override
    {
      waitedForPeer = true;
    }
    void shutdownInput() override
    {
    }
    void shutdownOutput() override
    {
    }
    void *getEventTarget() const override
    {
      return const_cast<BackloggedStream *>(this);
    }
    bool isReady() const override
    {
      return false;
    }
    uint32_t getSize() const override
    {
      return 0;
    }
    uint32_t getOutputSize() const override
    {
      return static_cast<uint32_t>(output.size());
    }
  };
  Arch arch;
  arch.init();
  Log log;
  EventQueue events;
  auto *stream = new BackloggedStream;
  ClientProxy1_0 client("stalled-peer", stream, &events);
  stream->output.clear();
  client.close(kMsgCClose);
  QVERIFY(!stream->waitedForPeer);
  QVERIFY(!stream->closed);
  QCOMPARE(stream->output, QByteArray(kMsgCClose));
}

QTEST_MAIN(ServerTests)
