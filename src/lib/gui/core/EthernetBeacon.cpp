/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EthernetBeacon.h"

#include "common/Settings.h"
#include "gui/core/NetworkMonitor.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QProcess>
#include <QTimer>
#include <QUdpSocket>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace deskflow::gui {

QString EthernetBeacon::serverIp()
{
  return NetworkMonitor::directServerIp();
}

EthernetBeacon::EthernetBeacon(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
  m_timer->setInterval(1000);
  connect(m_timer, &QTimer::timeout, this, &EthernetBeacon::tick);
}

EthernetBeacon::~EthernetBeacon()
{
  stop();
}

void EthernetBeacon::start(bool server)
{
  const bool roleChanged = m_running && m_server != server;
  m_server = server;
  if (m_running && !roleChanged) {
    tick();
    return;
  }
  if (roleChanged) {
    stop();
  }
  m_running = true;
  m_timer->start();
  tick();
}

void EthernetBeacon::stop()
{
  m_running = false;
  m_timer->stop();
  if (m_addressProcess) {
    m_addressProcess->kill();
    m_addressProcess = nullptr;
  }
  m_setupAttemptedInterface.clear();
  m_addressSetupFailed = false;
  if (m_send) {
    m_send->close();
    m_send->deleteLater();
    m_send = nullptr;
  }
  if (m_listen) {
    m_listen->close();
    m_listen->deleteLater();
    m_listen = nullptr;
  }
  m_localIp.clear();
}

void EthernetBeacon::retryAddressSetup()
{
  if (!m_running || m_addressProcess)
    return;
  m_setupAttemptedInterface.clear();
  m_addressSetupFailed = false;
  tick();
}

bool EthernetBeacon::addressReady() const
{
  if (m_addressProcess || m_addressSetupFailed)
    return false;
  const auto expectedIp = m_server ? serverIp() : NetworkMonitor::directClientIp();
  const auto nic = NetworkMonitor::ethernetNic(expectedIp);
  return nic && nic->ip == expectedIp && nic->prefixLength == 16 &&
         nic->interfaceName == NetworkMonitor::ethernetInterfaceName();
}

bool EthernetBeacon::serverAddressReady() const
{
  return m_server && addressReady();
}

void EthernetBeacon::tick()
{
  if (!m_running) {
    return;
  }

  const auto interfaceName = NetworkMonitor::ethernetInterfaceName();
  const auto expectedIp = m_server ? serverIp() : NetworkMonitor::directClientIp();
  const auto nic = NetworkMonitor::ethernetNic(expectedIp);
  const bool addressMatches = nic && nic->ip == expectedIp && nic->prefixLength == 16 &&
                              nic->interfaceName == interfaceName;
  if (!addressMatches) {
    if (!m_localIp.isEmpty()) {
      m_localIp.clear();
      Q_EMIT localNicChanged(QString());
    }
    if (m_send) {
      m_send->close();
      m_send->deleteLater();
      m_send = nullptr;
    }
  }
  // The native helper waits for duplicate-address detection. An address can
  // appear in Qt's interface list before it is usable. Linux also waits for
  // its configuration process to finish before advertising the address.
  if (m_addressProcess || m_addressSetupFailed)
    return;
  if (interfaceName.isEmpty())
    m_setupAttemptedInterface.clear();
  if (!interfaceName.isEmpty() && !addressMatches) {
    configureAddress(interfaceName);
  }
  // Never advertise or bind to a dynamic address.
  if (!addressMatches) {
    return;
  }
  m_setupAttemptedInterface.clear();

  if (m_localIp != nic->ip) {
    m_localIp = nic->ip;
    m_prefixLength = nic->prefixLength;
    if (m_send) {
      m_send->close();
      m_send->deleteLater();
      m_send = nullptr;
    }
    Q_EMIT localNicChanged(m_localIp);
  }

  if (m_server) {
    if (!m_send) {
      m_send = new QUdpSocket(this);
      if (!m_send->bind(QHostAddress(m_localIp), 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        m_send->deleteLater();
        m_send = nullptr;
        return;
      }
    }
    const auto port = Settings::value(Settings::Core::Port).toInt();
    const auto payload = QStringLiteral("DC1 %1 %2").arg(m_localIp).arg(port).toUtf8();
    m_send->writeDatagram(payload, nic->broadcast, kDiscoveryPort);
    if (nic->broadcast != QHostAddress::Broadcast) {
      m_send->writeDatagram(payload, QHostAddress::Broadcast, kDiscoveryPort);
    }
    return;
  }

  if (!m_listen) {
    m_listen = new QUdpSocket(this);
    connect(m_listen, &QUdpSocket::readyRead, this, &EthernetBeacon::readDatagrams);
    if (!m_listen->bind(
            QHostAddress::AnyIPv4, kDiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint
        )) {
      m_listen->deleteLater();
      m_listen = nullptr;
    }
  }
}

void EthernetBeacon::configureAddress(const QString &interfaceName)
{
  if (m_addressProcess || m_setupAttemptedInterface == interfaceName)
    return;
  m_setupAttemptedInterface = interfaceName;
  m_addressSetupFailed = false;
  const auto expectedIp = m_server ? serverIp() : NetworkMonitor::directClientIp();
  QString program;
  QStringList args;

#ifdef Q_OS_LINUX
  QString ip;
  for (const auto &path : {QStringLiteral("/usr/sbin/ip"), QStringLiteral("/usr/bin/ip"),
                           QStringLiteral("/sbin/ip"), QStringLiteral("/bin/ip")}) {
    if (QFileInfo(path).isExecutable()) {
      ip = path;
      break;
    }
  }
  if (ip.isEmpty()) {
    Q_EMIT addressSetupFailed(tr("The Linux ip command was not found."));
    return;
  }
  program = ip;
  args = {QStringLiteral("address"), QStringLiteral("add"), expectedIp + QStringLiteral("/16"),
          QStringLiteral("dev"), interfaceName};
  if (geteuid() != 0) {
    QString pkexec;
    for (const auto &path : {QStringLiteral("/usr/bin/pkexec"), QStringLiteral("/bin/pkexec")}) {
      if (QFileInfo(path).isExecutable()) {
        pkexec = path;
        break;
      }
    }
    if (pkexec.isEmpty()) {
      Q_EMIT addressSetupFailed(tr("pkexec is required to configure the Ethernet address."));
      return;
    }
    program = pkexec;
    args.prepend(ip);
  }
#elif defined(Q_OS_WIN)
  const int interfaceIndex = QNetworkInterface::interfaceFromName(interfaceName).index();
  if (interfaceIndex <= 0) {
    Q_EMIT addressSetupFailed(tr("Could not identify the Windows Ethernet adapter."));
    return;
  }
  wchar_t systemDirectory[MAX_PATH]{};
  const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    Q_EMIT addressSetupFailed(tr("Could not find the Windows system directory."));
    return;
  }
  program = QString::fromWCharArray(systemDirectory) + QStringLiteral("\\WindowsPowerShell\\v1.0\\powershell.exe");
  if (!QFileInfo(program).isExecutable()) {
    Q_EMIT addressSetupFailed(tr("Windows PowerShell is required to authorize the Ethernet address."));
    return;
  }
  QString appPath = QCoreApplication::applicationFilePath();
  if (!QFileInfo(appPath).isExecutable()) {
    Q_EMIT addressSetupFailed(tr("Could not find the DeskConnect executable for the elevated address setup."));
    return;
  }
  // The path is quoted as a PowerShell string, and the interface index is
  // numeric. No adapter name or user-provided text is interpreted as code.
  appPath.replace(QLatin1Char('\''), QStringLiteral("''"));
  const auto script = QStringLiteral(
                          "try { $p = Start-Process -FilePath '%1' "
                          "-ArgumentList '--deskconnect-add-direct-ethernet-address %2 %3' "
                          "-Verb RunAs -Wait -PassThru -WindowStyle Hidden -ErrorAction Stop; "
                          "exit $p.ExitCode } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }"
                      )
                          .arg(appPath, QString::number(interfaceIndex), m_server ? QStringLiteral("server")
                                                                             : QStringLiteral("client"));
  const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()), script.size() * sizeof(char16_t));
  const auto encoded = utf16.toBase64();
  args = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
          QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-EncodedCommand"),
          QString::fromLatin1(encoded.constData())};
#else
  Q_EMIT addressSetupFailed(tr("Automatic Ethernet address setup is unavailable on this operating system."));
  return;
#endif

  auto *process = new QProcess(this);
  m_addressProcess = process;
  connect(process, &QProcess::finished, process, &QObject::deleteLater);
  connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
    if (m_addressProcess != process)
      return;
    m_addressProcess = nullptr;
    const auto error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    if (!m_running)
      return;
    bool alreadyConfigured = false;
#ifndef Q_OS_WIN
    const auto nic = NetworkMonitor::ethernetNic(m_server ? serverIp() : NetworkMonitor::directClientIp());
    alreadyConfigured = nic && nic->ip == (m_server ? serverIp() : NetworkMonitor::directClientIp()) &&
                        nic->prefixLength == 16 &&
                        nic->interfaceName == m_setupAttemptedInterface;
#endif
    if ((status == QProcess::NormalExit && exitCode == 0) || alreadyConfigured) {
      QTimer::singleShot(100, this, &EthernetBeacon::tick);
    } else {
#ifdef Q_OS_WIN
      m_addressSetupFailed = true;
#endif
      Q_EMIT addressSetupFailed(
          error.isEmpty() ? tr("Could not configure the Ethernet address (code %1).").arg(exitCode) : error
      );
    }
  });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || m_addressProcess != process)
      return;
    m_addressProcess = nullptr;
    process->deleteLater();
    if (m_running) {
#ifdef Q_OS_WIN
      m_addressSetupFailed = true;
#endif
      Q_EMIT addressSetupFailed(tr("Could not start the Ethernet address setup command."));
    }
  });
  process->start(program, args);
}

void EthernetBeacon::readDatagrams()
{
  if (!m_listen) {
    return;
  }
  while (m_listen->hasPendingDatagrams()) {
    QByteArray buffer;
    buffer.resize(static_cast<int>(m_listen->pendingDatagramSize()));
    QHostAddress sender;
    m_listen->readDatagram(buffer.data(), buffer.size(), &sender);
    if (!buffer.startsWith("DC1 ")) {
      continue;
    }
    const auto parts = QString::fromUtf8(buffer).trimmed().split(QLatin1Char(' '));
    if (parts.size() != 3) {
      continue;
    }
    const QHostAddress announced(parts.at(1));
    bool portOk = false;
    const int port = parts.at(2).toInt(&portOk);
    if (!portOk || port < 1 || port > 65535 || announced.protocol() != QHostAddress::IPv4Protocol) {
      continue;
    }
    QHostAddress senderV4 = sender;
    if (sender.protocol() == QHostAddress::IPv6Protocol) {
      senderV4 = QHostAddress(sender.toIPv4Address());
    }
    if (senderV4.protocol() != QHostAddress::IPv4Protocol || senderV4.toIPv4Address() != announced.toIPv4Address()) {
      continue;
    }
    if (announced.toString() == m_localIp) {
      continue;
    }
    if (!NetworkMonitor::sameEthernetLink(m_localIp, announced.toString(), m_prefixLength)) {
      continue;
    }
    Q_EMIT peerFound(announced.toString());
  }
}

} // namespace deskflow::gui
