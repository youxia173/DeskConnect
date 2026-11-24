// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "common/Settings.h"
#include "gui/bluetooth/BluetoothFrame.h"
#include "gui/bluetooth/BluetoothSession.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::gui::bluetooth;

class BluetoothTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void fragmentedAndCoalescedFrames()
  {
    QByteArray tlsBytes;
    for (int i = 0; i < 256; ++i)
      tlsBytes += char(i);
    const QByteArray nonce("\0\0\0\1", 4);
    const auto wire = Frames::encode(Frames::Data, tlsBytes) + Frames::encode(Frames::Ping, nonce) +
                      Frames::encode(Frames::Pong, nonce);
    // Every possible split, including inside the header and binary TLS payload.
    for (qsizetype split = 0; split <= wire.size(); ++split) {
      Frames parser;
      QList<Frames::Frame> received;
      QVERIFY(parser.feed(wire.left(split), received));
      QVERIFY(parser.feed(wire.mid(split), received));
      QCOMPARE(received.size(), 3);
      QCOMPARE(received[0].type, Frames::Data);
      QCOMPARE(received[0].payload, tlsBytes);
      QCOMPARE(received[1].type, Frames::Ping);
      QCOMPARE(received[2].payload, nonce);
    }
  }

  void byteAtATimeAndMaximumFrame()
  {
    Frames parser;
    QList<Frames::Frame> received;
    const QByteArray data(Frames::MaxPayload, char(0xff));
    const auto wire = Frames::encode(Frames::Data, data);
    for (char byte : wire)
      QVERIFY(parser.feed(QByteArray(1, byte), received));
    QCOMPARE(received.size(), 1);
    QCOMPARE(received[0].payload, data);
  }

  void rejectsBadHeaderWithoutWaitingForPayload()
  {
    const auto good = Frames::encode(Frames::Data, "x");
    for (int index : {0, 3, 4, 5}) {
      auto broken = good.left(Frames::HeaderSize);
      broken[index] = char(0xff); // magic, version, type, oversized length
      Frames parser;
      QList<Frames::Frame> received;
      QVERIFY(!parser.feed(broken, received));
      QVERIFY(received.isEmpty());
      QVERIFY(!parser.feed(good, received)); // failure is sticky until reconnect
    }
    QVERIFY(Frames::encode(Frames::Data, {}).isEmpty());
    QVERIFY(Frames::encode(Frames::Ping, "123").isEmpty());
    QVERIFY(Frames::encode(Frames::Data, QByteArray(Frames::MaxPayload + 1, 'x')).isEmpty());
  }

  void rejectsUnboundedInput()
  {
    Frames parser;
    QList<Frames::Frame> received;
    QVERIFY(!parser.feed(QByteArray(Frames::MaxBuffered + 1, 'x'), received));
    QVERIFY(received.isEmpty());
  }

  void sessionPreservesNetworkSettingsAndTlsPaths()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Session::Snapshot source;
    source.settingsFile = dir.filePath("original.ini");
    source.values = {
        {Settings::Core::Interface, "192.168.8.10"},
        {Settings::Core::Port, 24800},
        {Settings::Client::RemoteHost, "original-peer"},
        {Settings::Security::Certificate, dir.filePath("tls/original.pem")},
        {Settings::FileTransfer::Enabled, true},
        {Settings::Server::EnableClipboard, true},
        {QStringLiteral("internalConfig/screens/1/name"), "unchanged-screen"}
    };
    QFile original(source.settingsFile);
    QVERIFY(original.open(QIODevice::WriteOnly));
    const QByteArray originalBytes("[untouched]\nvalue=keep\n");
    QCOMPARE(original.write(originalBytes), originalBytes.size());
    original.close();
    Session session;
    QString error;
    QVERIFY2(session.prepare(source, false, 39001, error), qPrintable(error));
    const auto path = session.settingsFile();
    QCOMPARE(QFileInfo(path).absolutePath(), QFileInfo(source.settingsFile).absolutePath());
    {
      QSettings runtime(path, QSettings::IniFormat);
      QCOMPARE(runtime.value(Settings::Core::Interface).toString(), QStringLiteral("127.0.0.1"));
      QCOMPARE(runtime.value(Settings::Core::Port).toInt(), 39001);
      QCOMPARE(runtime.value(Settings::Client::RemoteHost).toString(), QStringLiteral("127.0.0.1"));
      QVERIFY(runtime.value(Settings::Security::TlsEnabled).toBool());
      QVERIFY(runtime.value(Settings::Security::CheckPeers).toBool());
      QCOMPARE(runtime.value(Settings::Security::Certificate), source.values[Settings::Security::Certificate]);
      QVERIFY(!runtime.value(Settings::Server::EnableClipboard).toBool());
      QVERIFY(!runtime.value(Settings::FileTransfer::Enabled).toBool());
      QCOMPARE(runtime.value("internalConfig/screens/1/name").toString(), QStringLiteral("unchanged-screen"));
      QCOMPARE(runtime.value(Settings::Server::Heartbeat).toInt(), 1000);
      // The core can save this INI while the GUI still owns the session.
      runtime.setValue("test/coreWrite", true);
      runtime.sync();
      QCOMPARE(runtime.status(), QSettings::NoError);
    }
    session.clear();
    QVERIFY(!QFile::exists(path));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes);
    QCOMPARE(source.values[Settings::Core::Port].toInt(), 24800);
    QVERIFY(source.values[Settings::Server::EnableClipboard].toBool());
  }

  void serverConfigIsCopiedAndTemporaryFilesAreRemoved()
  {
    QTemporaryDir dir;
    Session::Snapshot source;
    source.settingsFile = dir.filePath("settings.ini");
    source.serverConfigFile = dir.filePath("server.conf");
    QFile original(source.serverConfigFile);
    QVERIFY(original.open(QIODevice::WriteOnly));
    const QByteArray originalBytes("section: screens\n  server:\n  client:\nend\n");
    QCOMPARE(original.write(originalBytes), originalBytes.size());
    original.close();
    QString runtimePath, configPath;
    {
      Session session;
      QString error;
      QVERIFY2(session.prepare(source, true, 39002, error), qPrintable(error));
      runtimePath = session.settingsFile();
      QSettings runtime(runtimePath, QSettings::IniFormat);
      QVERIFY(runtime.value(Settings::Server::ExternalConfig).toBool());
      configPath = runtime.value(Settings::Server::ExternalConfigFile).toString();
      QVERIFY(configPath != source.serverConfigFile);
      QFile config(configPath);
      QVERIFY(config.open(QIODevice::ReadOnly));
      QCOMPARE(config.readAll(), originalBytes + "\nsection: options\nend\n");
    }
    QVERIFY(!QFile::exists(runtimePath));
    QVERIFY(!QFile::exists(configPath));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes);
  }

  void failedSessionDoesNotLeavePartialConfig()
  {
    QTemporaryDir dir;
    Session::Snapshot source;
    source.settingsFile = dir.filePath("settings.ini");
    source.serverConfigFile = dir.filePath("missing.conf");
    Session session;
    QString error;
    QVERIFY(!session.prepare(source, true, 39003, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(session.settingsFile().isEmpty());
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
    QVERIFY(!session.prepare(source, false, 0, error));
  }
};

QTEST_GUILESS_MAIN(BluetoothTests)
#include "BluetoothTests.moc"
