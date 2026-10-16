/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/XWindowsClipboardFiles.h"
#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <future>

class XWindowsClipboardFilesTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void metadataDoesNotDownloadAndReadDoes()
  {
    if (!QFileInfo::exists("/dev/fuse") || QStandardPaths::findExecutable("fusermount3").isEmpty())
      QSKIP("Integration test requires /dev/fuse and fusermount3 (unprivileged mounts enabled)");
    Arch arch;
    arch.init();
    Log log;
    EventQueue events;
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    QVERIFY(QDir().mkpath(cache.filePath("original/empty")));
    QFile source(cache.filePath("original/report.txt"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("payload"), qint64(7));
    source.close();
    int pulls = 0;
    std::vector<std::string> published;
    auto files = std::make_unique<XWindowsClipboardFiles>(&events, [&] {
      ++pulls;
      return std::vector<std::string>{cache.filePath("original").toStdString()};
    }, [] {
      return std::vector<deskflow::FileOffer>{{"original/", 0, {}},
          {"original/empty/", 0, {}}, {"original/report.txt", 7, {}}};
    }, [&](const auto &paths) { published = paths; });
    const auto pumpUntil = [&](auto condition) {
      QElapsedTimer time;
      time.start();
      while (!condition() && time.elapsed() < 5000) {
        Event event;
        if (events.getEvent(event, 0.02)) {
          events.dispatchEvent(event);
          Event::deleteData(event);
        }
      }
      return condition();
    };
    QVERIFY2(pumpUntil([&] { return !published.empty(); }), "virtual file mount was not published");
    QCOMPARE(pulls, 0);
    QCOMPARE(published.size(), size_t(1));
    const auto root = QString::fromStdString(published.front());
    QCOMPARE(QFileInfo(root).fileName(), QStringLiteral("original"));
    QVERIFY(QFileInfo(root).isDir());
    QVERIFY(QFileInfo(root + "/empty").isDir());
    QCOMPARE(QFileInfo(root + "/report.txt").size(), qint64(7));
    QVERIFY(QDir(root).entryList().contains("report.txt"));
    // Clipboard URI conversion itself probes QFileInfo; it too must stay metadata-only.
    const auto uris = deskflow::uriListFromClipboardData(deskflow::clipboardDataFromPaths(published));
    QVERIFY(!deskflow::clipboardDataFromUriList(uris).empty());
    QCOMPARE(pulls, 0);
    auto read = std::async(std::launch::async, [root] {
      QFile file(root + "/report.txt");
      return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    });
    const bool finished = pumpUntil([&] { return read.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
    if (!finished) files.reset(); // wake any blocked read before joining
    const auto bytes = read.get();
    QVERIFY(finished);
    QCOMPARE(bytes, QByteArray("payload"));
    QCOMPARE(pulls, 1);
    QFile again(root + "/report.txt");
    QVERIFY(again.open(QIODevice::ReadOnly));
    QCOMPARE(again.readAll(), QByteArray("payload"));
    QCOMPARE(pulls, 1);
    again.close();
    files.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root), 5000);

    // Cancel a content read before the core has dispatched the pull request.
    // The FUSE reader must wake without downloading or waiting for a timeout.
    published.clear();
    pulls = 0;
    files = std::make_unique<XWindowsClipboardFiles>(&events, [&] {
      ++pulls;
      return std::vector<std::string>{};
    }, [] {
      return std::vector<deskflow::FileOffer>{{"pending.txt", 7, {}}};
    }, [&](const auto &paths) { published = paths; });
    QVERIFY(pumpUntil([&] { return !published.empty(); }));
    const auto pending = QString::fromStdString(published.front());
    auto cancelledRead = std::async(std::launch::async, [pending] {
      QFile file(pending);
      return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    });
    const bool wasWaiting = cancelledRead.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    files.reset();
    const auto cancelledBytes = cancelledRead.get();
    QVERIFY(wasWaiting);
    QVERIFY(cancelledBytes.isEmpty());
    QCOMPARE(pulls, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(pending), 5000);
  }
};
QTEST_GUILESS_MAIN(XWindowsClipboardFilesTests)
#include "XWindowsClipboardFilesTests.moc"
