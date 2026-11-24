// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "AndroidRecovery.h"
#include "base/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QResource>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <chrono>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Explicit reference retains this resource when linking the static server lib.
static void initRecoveryResource()
{
  Q_INIT_RESOURCE(android_recovery);
}

AndroidRecovery::~AndroidRecovery()
{
  {
    std::lock_guard lock(m_mutex);
    m_stopping = true;
  }
  m_changed.notify_all();
  if (m_worker.joinable())
    m_worker.join();
}

void AndroidRecovery::update(const std::string &screen, const std::string &id, bool enabled)
{
  std::lock_guard lock(m_mutex);
  if (enabled) {
    if (!m_devices.contains(screen) && m_devices.size() >= 32) {
      LOG_WARN("android recovery: device limit reached");
      return;
    }
    if (m_devices.contains(screen) && m_devices.at(screen) == id)
      return;
    m_devices[screen] = id;
  } else {
    if (m_devices.erase(screen) == 0)
      return;
  }
  m_dirty = true;
  if (!m_worker.joinable() && !m_devices.empty())
    m_worker = std::thread(&AndroidRecovery::run, this);
  m_changed.notify_all();
}

void AndroidRecovery::forget(const std::string &screen)
{
  update(screen, {}, false);
}

void AndroidRecovery::run()
{
  initRecoveryResource();
  QTemporaryDir directory;
  const QString script = directory.filePath("wireless-input.py");
  if (!directory.isValid() || !QFile::copy(":/android/wireless-input.py", script)) {
    LOG_ERR("android recovery: cannot extract bundled worker");
    return;
  }
  // Explicit overrides support portable installs; no user's SDK path is baked in.
  auto adb = qEnvironmentVariable("DESKCONNECT_ADB");
  if (adb.isEmpty()) {
    const auto bundled = QCoreApplication::applicationDirPath() + "/platform-tools/adb";
#ifdef Q_OS_WIN
    if (QFile::exists(bundled + ".exe"))
      adb = bundled + ".exe";
#else
    if (QFile::exists(bundled))
      adb = bundled;
#endif
    if (adb.isEmpty())
      adb = QStandardPaths::findExecutable("adb");
    for (const char *env : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
      if (adb.isEmpty() && !qEnvironmentVariable(env).isEmpty())
        adb = QStandardPaths::findExecutable("adb", {qEnvironmentVariable(env) + "/platform-tools"});
    }
  }
  auto python = qEnvironmentVariable("DESKCONNECT_PYTHON");
#ifdef Q_OS_WIN
  // Prefer a real python.exe over Windows' python3 Store alias.
  if (python.isEmpty())
    python = QStandardPaths::findExecutable("python");
#endif
  if (python.isEmpty())
    python = QStandardPaths::findExecutable("python3");
  if (python.isEmpty())
    python = QStandardPaths::findExecutable("python");
  if (adb.isEmpty() || python.isEmpty()) {
    LOG_WARN(
        "android recovery unavailable: install Python 3.10+ and Android platform-tools on the server; "
        "set DESKCONNECT_PYTHON / DESKCONNECT_ADB if they are not on PATH"
    );
    return;
  }

  QProcess process;
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
    args->flags |= CREATE_NO_WINDOW;
  });
#endif
  process.setProcessChannelMode(QProcess::MergedChannels);
  QByteArray output;
  auto flushLogs = [&] {
    output += process.readAllStandardOutput();
    qsizetype end;
    while ((end = output.indexOf('\n')) >= 0) {
      const auto line = output.left(end).trimmed();
      LOG_INFO("android recovery: %s", line.constData());
      output.remove(0, end + 1);
    }
    if (output.size() > 8192)
      output.clear();
  };
  bool warned = false;
  while (true) {
    QJsonArray ids;
    bool dirty;
    {
      std::unique_lock lock(m_mutex);
      m_changed.wait_for(lock, std::chrono::milliseconds(100), [this] { return m_stopping || m_dirty; });
      if (m_stopping)
        break;
      dirty = m_dirty;
      m_dirty = false;
      for (const auto &[screen, id] : m_devices)
        ids.append(QString::fromStdString(id));
    }
    if (ids.isEmpty()) {
      if (process.state() != QProcess::NotRunning) {
        process.closeWriteChannel();
        if (!process.waitForFinished(500)) {
          process.kill();
          process.waitForFinished(500);
        }
        flushLogs();
      }
      continue;
    }
    if (process.state() == QProcess::NotRunning) {
      process.start(python, {"-u", script, "--adb", adb, "server"});
      if (!process.waitForStarted(1000)) {
        if (!warned)
          LOG_WARN("android recovery: Python worker could not start");
        warned = true;
        std::unique_lock lock(m_mutex);
        m_changed.wait_for(lock, std::chrono::seconds(10), [this] { return m_stopping; });
        continue;
      }
      dirty = true;
      warned = false;
    }
    if (dirty) {
      process.write(QJsonDocument(ids).toJson(QJsonDocument::Compact) + '\n');
      process.waitForBytesWritten(100);
    }
    process.waitForReadyRead(50);
    flushLogs();
    if (process.state() == QProcess::NotRunning) {
      LOG_WARN("android recovery: worker exited; retrying in 10 seconds");
      std::unique_lock lock(m_mutex);
      m_changed.wait_for(lock, std::chrono::seconds(10), [this] { return m_stopping; });
    }
  }
  // EOF is also the parent-death signal: the worker never survives its server.
  process.closeWriteChannel();
  if (!process.waitForFinished(500)) {
    process.kill();
    process.waitForFinished(500);
  }
  flushLogs();
}
