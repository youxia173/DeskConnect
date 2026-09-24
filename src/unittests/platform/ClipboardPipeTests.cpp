/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/ClipboardPipe.h"
#include <QTest>
#include <chrono>
#include <thread>

class ClipboardPipeTests : public QObject
{
  Q_OBJECT
  struct Pipe
  {
    int fds[2]{-1, -1};
    Pipe()
    {
      if (::pipe(fds) != 0)
        qFatal("pipe creation failed");
    }
    ~Pipe()
    {
      ::close(fds[0]);
      if (fds[1] >= 0)
        ::close(fds[1]);
    }
    void finish()
    {
      ::close(fds[1]);
      fds[1] = -1;
    }
  };
private Q_SLOTS:
  void deliversLargePayloadWithoutTruncation()
  {
    Pipe pipe;
    const QByteArray payload(1024 * 1024, 'x');
    QByteArray received;
    std::jthread reader([&] {
      char buffer[8192];
      for (;;) {
        const auto count = ::read(pipe.fds[0], buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
          continue;
        if (count <= 0)
          break;
        received.append(buffer, count);
      }
    });
    const bool success = deskflow::ClipboardPipe::write(pipe.fds[1], payload, 1000, 5000);
    pipe.finish();
    reader.join();
    QVERIFY(success);
    QCOMPARE(received, payload);
  }

  void readsCompletePayloadAtExactLimit()
  {
    Pipe pipe;
    QCOMPARE(::write(pipe.fds[1], "hello", 5), ssize_t(5));
    pipe.finish();
    QByteArray result;
    QVERIFY(deskflow::ClipboardPipe::read(pipe.fds[0], 5, result));
    QCOMPARE(result, QByteArray("hello"));
  }
  void discardsStalledPartialRead()
  {
    Pipe pipe;
    QCOMPARE(::write(pipe.fds[1], "partial", 7), ssize_t(7));
    QByteArray result;
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!deskflow::ClipboardPipe::read(pipe.fds[0], 100, result, 30, 100));
    QVERIFY(result.isEmpty());
    QVERIFY(elapsed.elapsed() < 1000);
  }
  void stalledReaderCannotBlockLargeWrite()
  {
    Pipe pipe;
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!deskflow::ClipboardPipe::write(pipe.fds[1], QByteArray(8 * 1024 * 1024, 'x'), 30, 100));
    QVERIFY(elapsed.elapsed() < 1000);
    QVERIFY(!(fcntl(pipe.fds[1], F_GETFL) & O_NONBLOCK));
  }
  void tricklingDataCannotExtendTotalDeadline()
  {
    Pipe pipe;
    std::jthread writer([&] {
      for (int i = 0; i < 25; ++i) {
        if (::write(pipe.fds[1], "x", 1) != 1)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
    QByteArray result;
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!deskflow::ClipboardPipe::read(pipe.fds[0], 100, result, 100, 60));
    QVERIFY(result.isEmpty());
    QVERIFY(elapsed.elapsed() < 1000);
  }
  void oversizeReadIsNotPublishedAsTruncatedText()
  {
    Pipe pipe;
    QCOMPARE(::write(pipe.fds[1], "hello", 5), ssize_t(5));
    pipe.finish();
    QByteArray result;
    QVERIFY(!deskflow::ClipboardPipe::read(pipe.fds[0], 4, result));
    QVERIFY(result.isEmpty());
  }
};
QTEST_GUILESS_MAIN(ClipboardPipeTests)
#include "ClipboardPipeTests.moc"
