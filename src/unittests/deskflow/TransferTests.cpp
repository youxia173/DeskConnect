/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "deskflow/PacketStreamFilter.h"
#include "filetransfer/FileReceiveSession.h"
#include "io/IStream.h"
#include "io/StreamBuffer.h"
#include "net/ISocketMultiplexerJob.h"
#include "net/NetworkAddress.h"
#include "net/SecureSocket.h"
#include "net/SocketMultiplexer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstring>
#include <functional>

namespace {
class ContinuousStream : public deskflow::IStream
{
public:
  static constexpr uint32_t packetSize = 65536;
  uint64_t bytesRead = 0;
  uint64_t remaining = uint64_t(packetSize + 4) * 1024;
  void close() override
  {
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void write(const void *, uint32_t) override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<ContinuousStream *>(this);
  }
  bool isReady() const override
  {
    return remaining != 0;
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(remaining);
  }
  uint32_t read(void *buffer, uint32_t count) override
  {
    count = static_cast<uint32_t>(std::min<uint64_t>(count, remaining));
    auto *out = static_cast<char *>(buffer);
    for (uint32_t i = 0; i < count; ++i) {
      const auto pos = (bytesRead + i) % (packetSize + 4);
      if (out)
        out[i] = pos == 1 ? 1 : 0;
    }
    bytesRead += count;
    remaining -= count;
    return count;
  }
};

class RetrySocket : public SecureSocket
{
public:
  using SecureSocket::SecureSocket;
  using Result = JobResult;
  int writes = 0;
  std::function<void()> onWrite;
  JobResult doWrite() override
  {
    ++writes;
    if (onWrite)
      onWrite();
    return JobResult::New;
  }
};
} // namespace

class TransferTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    m_arch.init();
    QVERIFY(m_temp.isValid());
    Settings::setSettingsFile(m_temp.filePath("settings.ini"));
    Settings::setStateFile(m_temp.filePath("state.ini"));
    Settings::setValue(Settings::FileTransfer::ReceiveDir, m_temp.path());
    m_log.setFilter(LogLevel::Level::Error);
  }

  void continuousBufferReclaimsConsumedMemory()
  {
    StreamBuffer buffer;
    const std::string chunk(64 * 1024, 'x');
    buffer.write(chunk.data(), static_cast<uint32_t>(chunk.size()));
    buffer.write(chunk.data(), static_cast<uint32_t>(chunk.size()));
    // 256 MiB through a queue which never becomes empty.
    for (int i = 0; i < 4096; ++i) {
      const auto size = buffer.getSize();
      const auto *bytes = static_cast<const char *>(buffer.peek(size));
      QVERIFY(std::all_of(bytes, bytes + size, [](char c) { return c == 'x'; }));
      size_t allocated = 0;
      for (const auto &part : buffer.m_chunks) {
        allocated += part.capacity();
      }
      QVERIFY2(allocated <= 4 * size, "Consumed bytes remain allocated during continuous traffic");
      buffer.pop(static_cast<uint32_t>(chunk.size()));
      buffer.write(chunk.data(), static_cast<uint32_t>(chunk.size()));
    }
  }

  void continuousPacketsYieldToConsumers()
  {
    EventQueue events;
    ContinuousStream source;
    PacketStreamFilter packets(&events, &source, false);
    uint64_t received = 0;
    events.addHandler(EventTypes::StreamInputReady, packets.getEventTarget(), [&](const Event &) {
      std::string payload(ContinuousStream::packetSize, '\xff');
      while (packets.isReady()) {
        const auto count = packets.read(payload.data(), static_cast<uint32_t>(payload.size()));
        QCOMPARE(count, ContinuousStream::packetSize);
        QVERIFY(std::all_of(payload.begin(), payload.end(), [](char c) { return c == 0; }));
        received += count;
      }
    });
    while (source.remaining != 0) {
      const auto before = source.bytesRead;
      events.dispatchEvent(Event(EventTypes::StreamInputReady, source.getEventTarget()));
      QVERIFY(source.bytesRead - before <= 256 * 1024);
    }
    QCOMPARE(received, uint64_t(64 * 1024 * 1024));
  }

  void tlsWriteRetryPollsRequiredDirection()
  {
    EventQueue events;
    SocketMultiplexer multiplexer;
    for (bool needsRead : {false, true}) {
      RetrySocket socket(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
      socket.m_writeRetry = true;
      socket.m_writeNeedsRead = needsRead;
      socket.m_secureReady = true;
      // No SSL context exists: attempting SSL_read instead of yielding would
      // also expose an invalid access in the original implementation.
      QCOMPARE(socket.doRead(), RetrySocket::Result::Retry);
      auto *job = socket.newJob();
      QVERIFY(job != nullptr);
      QCOMPARE(job->isReadable(), needsRead);
      QCOMPARE(job->isWritable(), !needsRead);
      QCOMPARE(job->run(!needsRead, needsRead, false), job);
      QCOMPARE(socket.writes, 0);
      auto *next = job->run(needsRead, !needsRead, false);
      QCOMPARE(socket.writes, 1);
      QVERIFY(next != job);
      delete job;
      delete next;
    }
  }

  void tcpReceivePausesAndResumesWithoutLosingBytes()
  {
    EventQueue events;
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    SocketMultiplexer multiplexer;
    TCPSocket receiver(&events, &multiplexer);
    NetworkAddress address("127.0.0.1", listener.serverPort());
    address.resolve();
    receiver.connect(address);
    QVERIFY(listener.waitForNewConnection(3000));
    auto *sender = listener.nextPendingConnection();
    QVERIFY(sender != nullptr);
    QByteArray payload(8 * 1024 * 1024, '\x59');
    QCOMPARE(sender->write(payload), qint64(payload.size()));
    sender->flush();
    QTest::qWait(100);
    QVERIFY(receiver.getSize() <= 1024 * 1024 + 4096);
    QElapsedTimer timeout;
    timeout.start();
    QByteArray result;
    char buffer[65536];
    while (result.size() < payload.size() && timeout.elapsed() < 10000) {
      const auto count = receiver.read(buffer, sizeof(buffer));
      result.append(buffer, count);
      if (count == 0)
        QTest::qWait(1);
    }
    QCOMPARE(result, payload);
    receiver.close();
  }

  void receivesFileWithMatchingHash()
  {
    deskflow::FileReceiveSession session;
    constexpr uint64_t size = 64 * 1024 * 1024;
    QVERIFY(session.begin({"large.bin", "empty.bin"}, size));
    QCOMPARE(session.onChunk(ChunkType::DataStart, std::to_string(size), size), TransferState::Started);
    std::string chunk(64 * 1024, '\0');
    for (size_t i = 0; i < chunk.size(); ++i) {
      chunk[i] = static_cast<char>(i % 251);
    }
    QCryptographicHash expected(QCryptographicHash::Sha256);
    for (uint64_t sent = 0; sent < size; sent += chunk.size()) {
      QCOMPARE(session.onChunk(ChunkType::DataChunk, chunk, size), TransferState::InProgress);
      expected.addData(QByteArrayView(chunk.data(), chunk.size()));
    }
    QCOMPARE(session.onChunk(ChunkType::DataEnd, "", size), TransferState::InProgress);
    QCOMPARE(session.onChunk(ChunkType::DataStart, "0", size), TransferState::Started);
    QCOMPARE(session.onChunk(ChunkType::DataEnd, "", size), TransferState::Finished);
    QCOMPARE(session.receivedPaths().size(), size_t(2));
    QFile received(QString::fromStdString(session.receivedPaths()[0]));
    QVERIFY(received.open(QIODevice::ReadOnly));
    QCryptographicHash actual(QCryptographicHash::Sha256);
    QVERIFY(actual.addData(&received));
    QCOMPARE(actual.result(), expected.result());
    QCOMPARE(QFile(QString::fromStdString(session.receivedPaths()[1])).size(), qint64(0));
  }

  void rejectsInvalidSequenceAndCleansPartialFile()
  {
    deskflow::FileReceiveSession session;
    QVERIFY(session.begin({"partial.bin"}, 100));
    QCOMPARE(session.onChunk(ChunkType::DataEnd, "", 100), TransferState::Error);
    QVERIFY(session.begin({"partial.bin"}, 100));
    QCOMPARE(session.onChunk(ChunkType::DataStart, "10", 100), TransferState::Started);
    QCOMPARE(session.onChunk(ChunkType::DataChunk, "abc", 100), TransferState::InProgress);
    QCOMPARE(session.onChunk(ChunkType::DataStart, "10", 100), TransferState::Error);
    QVERIFY(!QFile::exists(m_temp.filePath("partial.bin")));
    QVERIFY(session.begin({"partial.bin"}, 100));
    QCOMPARE(session.onChunk(ChunkType::DataStart, "10", 100), TransferState::Started);
    QCOMPARE(session.onChunk(ChunkType::DataEnd, "", 100), TransferState::Error);
    QVERIFY(!QFile::exists(m_temp.filePath("partial.bin")));
  }

  void rejectsOverflowAndSupportsLargeHeaders()
  {
    deskflow::FileReceiveSession session;
    QVERIFY(session.begin({"huge.bin"}, 0));
    QCOMPARE(session.onChunk(ChunkType::DataStart, "5368709120", 0), TransferState::Started);
    session.reset();
    QVERIFY(!QFile::exists(m_temp.filePath("huge.bin")));
    QVERIFY(session.begin({"huge.bin"}, 0));
    QCOMPARE(session.onChunk(ChunkType::DataStart, "18446744073709551615", 0), TransferState::Error);
    QVERIFY(!QFile::exists(m_temp.filePath("huge.bin")));
  }

  void preservesExistingFilesAndRejectsUnsafeNames()
  {
    QFile original(m_temp.filePath("existing.txt"));
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("original"), qint64(8));
    original.close();
    deskflow::FileReceiveSession session;
    QVERIFY(session.begin({"existing.txt"}, 100));
    QCOMPARE(session.onChunk(ChunkType::DataStart, "3", 100), TransferState::Started);
    QCOMPARE(session.onChunk(ChunkType::DataChunk, "new", 100), TransferState::InProgress);
    QCOMPARE(session.onChunk(ChunkType::DataEnd, "", 100), TransferState::Finished);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("original"));
    for (const std::string name : {"../escape", "CON", "x:stream", "name.", "name "}) {
      QVERIFY(!session.begin({name}, 100));
    }
  }

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_temp;
};

QTEST_MAIN(TransferTests)
#include "TransferTests.moc"
