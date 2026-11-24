// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "BluetoothTransport.h"
#include "BluetoothFrame.h"

#ifdef DESKCONNECT_BLUETOOTH
#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>
#include <QBluetoothServer>
#include <QBluetoothServiceInfo>
#include <QBluetoothSocket>
#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#endif

namespace deskflow::gui::bluetooth {
#ifdef DESKCONNECT_BLUETOOTH
namespace {
const QBluetoothUuid ServiceUuid(QStringLiteral("ef937c10-317f-4c9b-90a1-d7ec206ca001"));
constexpr qint64 Window = 16384;
constexpr qint64 WatchdogMs = 3000;
constexpr qint64 ConnectTimeoutMs = 10000;
} // namespace

class Transport::Impl : public QObject
{
public:
  explicit Impl(Transport *transport) : QObject(transport), owner(transport)
  {
    timer.setInterval(250);
    connect(&timer, &QTimer::timeout, this, [this] { tick(); });
    connect(&listener, &QTcpServer::newConnection, this, [this] {
      while (listener.hasPendingConnections()) {
        auto *socket = listener.nextPendingConnection();
        if (tcp || !running) {
          socket->abort();
          socket->deleteLater();
          continue;
        }
        attachTcp(socket);
        attachBluetooth(new QBluetoothSocket(QBluetoothServiceInfo::Protocol::RfcommProtocol, this));
        connectionAge.start();
        Q_EMIT owner->status(tr("正在连接蓝牙设备 %1…").arg(peer.toString()));
        bt->connectToService(peer, ServiceUuid);
      }
    });
  }

  ~Impl() override
  {
    stop();
  }

  bool start(bool serverMode, const QString &address, QString &error)
  {
    stop();
    QBluetoothLocalDevice adapter;
    peer = QBluetoothAddress(address);
    if (!adapter.isValid() || adapter.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
      error = tr("没有可用的蓝牙适配器，请先打开系统蓝牙。");
      return false;
    }
    if (peer.isNull() || peer == adapter.address()) {
      error = tr("请填写另一台电脑的蓝牙地址。");
      return false;
    }
    if (adapter.pairingStatus(peer) == QBluetoothLocalDevice::Unpaired) {
      error = tr("请先在操作系统的蓝牙设置中将两台电脑配对，再启动蓝牙直连。");
      return false;
    }
    server = serverMode;
    // The server core owns its port after this reservation is released. A
    // concurrent bind fails closed in the core; never fall back to a LAN bind.
    if (!listener.listen(QHostAddress::LocalHost, 0)) {
      error = listener.errorString();
      return false;
    }
    localPort = listener.serverPort();
    if (server) {
      listener.close();
      rfcomm = new QBluetoothServer(QBluetoothServiceInfo::Protocol::RfcommProtocol, this);
      rfcomm->setSecurityFlags(QBluetooth::Security::Authentication | QBluetooth::Security::Encryption);
      connect(rfcomm, &QBluetoothServer::errorOccurred, this, [this](auto error) {
        drop(tr("蓝牙服务错误 %1").arg(int(error)));
        rfcomm->close();
      });
      connect(rfcomm, &QBluetoothServer::newConnection, this, [this] {
        while (rfcomm->hasPendingConnections()) {
          auto *socket = rfcomm->nextPendingConnection();
          if (!running || bt || socket->peerAddress() != peer) {
            socket->abort();
            socket->deleteLater();
            continue;
          }
          attachBluetooth(socket);
          attachTcp(new QTcpSocket(this));
          connectionAge.start();
          tcp->connectToHost(QHostAddress::LocalHost, localPort);
          linked();
        }
      });
      service = rfcomm->listen(ServiceUuid, QStringLiteral("DeskConnect Bluetooth v1"));
      if (!service.isRegistered()) {
        error = tr("无法注册蓝牙服务（错误 %1）。请检查系统蓝牙服务及适配器。").arg(int(rfcomm->error()));
        stop();
        return false;
      }
    }
    running = true;
    timer.start();
    Q_EMIT owner->status(
        server ? tr("蓝牙服务已启动，等待已配对设备 %1").arg(peer.toString())
               : tr("蓝牙通道已准备，目标 %1").arg(peer.toString())
    );
    return true;
  }

  void stop()
  {
    running = false;
    timer.stop();
    listener.close();
    drop({});
    service.unregisterService();
    if (rfcomm) {
      rfcomm->disconnect(this);
      rfcomm->close();
      delete rfcomm;
      rfcomm = nullptr;
    }
    localPort = 0;
  }

  void drop(const QString &reason)
  {
    // Disconnect callbacks before abort: error/disconnected may be synchronous.
    if (tcp) {
      auto *socket = tcp;
      tcp = nullptr;
      socket->disconnect(this);
      socket->abort();
      socket->deleteLater();
    }
    if (bt) {
      auto *socket = bt;
      bt = nullptr;
      socket->disconnect(this);
      socket->abort();
      socket->deleteLater();
    }
    frames = Frames();
    pendingPing.clear();
    pingAge.invalidate();
    connectionAge.invalidate();
    outputAge.invalidate();
    linkedOnce = false;
    if (!reason.isEmpty())
      Q_EMIT owner->status(tr("蓝牙连接已断开：%1；键鼠连接已释放，等待重新连接。").arg(reason));
  }

  void attachTcp(QTcpSocket *socket)
  {
    tcp = socket;
    socket->setParent(this);
    socket->setReadBufferSize(Window);
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::connected, this, [this] {
      tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
      linked();
      pump();
    });
    connect(socket, &QTcpSocket::readyRead, this, [this] { pump(); });
    connect(socket, &QTcpSocket::disconnected, this, [this] { drop(tr("本机核心连接关闭")); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this](auto) {
      const auto reason = tcp->errorString();
      drop(reason);
    });
  }

  void attachBluetooth(QBluetoothSocket *socket)
  {
    bt = socket;
    socket->setParent(this);
    socket->setPreferredSecurityFlags(QBluetooth::Security::Authentication | QBluetooth::Security::Encryption);
    connect(socket, &QBluetoothSocket::connected, this, [this] {
      linked();
      pump();
    });
    connect(socket, &QBluetoothSocket::readyRead, this, [this] { receive(); });
    connect(socket, &QBluetoothSocket::bytesWritten, this, [this](qint64) {
      outputAge.restart();
      pump();
    });
    connect(socket, &QBluetoothSocket::disconnected, this, [this] { drop(tr("设备离线或蓝牙关闭")); });
    connect(socket, &QBluetoothSocket::errorOccurred, this, [this](auto) {
      const auto reason = bt->errorString();
      drop(reason);
    });
  }

  void linked()
  {
    if (!bt || !tcp || bt->state() != QBluetoothSocket::SocketState::ConnectedState ||
        tcp->state() != QAbstractSocket::ConnectedState || linkedOnce)
      return;
    linkedOnce = true;
    outputAge.start();
    Q_EMIT owner->status(tr("蓝牙通道已连接，正在进行 DeskConnect TLS 身份验证。"));
    receive();
  }

  bool send(Frames::Type type, const QByteArray &payload)
  {
    if (!bt || bt->state() != QBluetoothSocket::SocketState::ConnectedState)
      return false;
    const auto packet = Frames::encode(type, payload);
    if (bt->bytesToWrite() == 0)
      outputAge.restart();
    if (packet.isEmpty() || bt->bytesToWrite() + packet.size() > Frames::MaxBuffered ||
        bt->write(packet) != packet.size()) {
      drop(tr("蓝牙发送队列异常"));
      return false;
    }
    return true;
  }

  void pump()
  {
    // Backpressure bounds both Qt buffers. Never accumulate a replay of stale
    // mouse events after an outage; the watchdog tears down the entire session.
    while (tcp && bt && linkedOnce && tcp->bytesAvailable() > 0 && bt->bytesToWrite() < Window) {
      const auto bytes = tcp->read(Frames::MaxPayload);
      if (bytes.isEmpty() || !send(Frames::Data, bytes))
        break;
    }
  }

  void receive()
  {
    if (!bt)
      return;
    if (bt->bytesAvailable() > Frames::MaxBuffered) {
      drop(tr("蓝牙接收队列超过上限"));
      return;
    }
    if (!linkedOnce)
      return;
    while (bt && bt->bytesAvailable() > 0) {
      QList<Frames::Frame> incoming;
      if (!frames.feed(bt->read(Frames::MaxPayload), incoming)) {
        drop(tr("蓝牙协议版本不兼容或数据帧无效"));
        return;
      }
      for (const auto &frame : incoming) {
        if (!bt || !tcp)
          return;
        if (frame.type == Frames::Data) {
          if (tcp->bytesToWrite() + frame.payload.size() > Frames::MaxBuffered ||
              tcp->write(frame.payload) != frame.payload.size()) {
            drop(tr("本机核心未及时接收蓝牙数据"));
            return;
          }
        } else if (frame.type == Frames::Ping) {
          if (!send(Frames::Pong, frame.payload))
            return;
        } else if (!pendingPing.isEmpty() && frame.payload == pendingPing) {
          pendingPing.clear();
          pingAge.invalidate();
        }
      }
    }
  }

  void tick()
  {
    if (server && rfcomm && !rfcomm->isListening()) {
      // Adapter removal loses the SDP registration. Retry on the same paired
      // peer; do not enable discoverability or pair with arbitrary devices.
      if (++listenRetryTicks >= 12) {
        listenRetryTicks = 0;
        service.unregisterService();
        service = rfcomm->listen(ServiceUuid, QStringLiteral("DeskConnect Bluetooth v1"));
      }
    }
    if (!bt)
      return;
    if (!linkedOnce) {
      if (connectionAge.isValid() && connectionAge.elapsed() > ConnectTimeoutMs)
        drop(tr("连接超时"));
      return;
    }
    if ((pingAge.isValid() && pingAge.elapsed() > WatchdogMs) ||
        (bt->bytesToWrite() > 0 && outputAge.elapsed() > WatchdogMs)) {
      drop(tr("超过 3 秒未收到蓝牙响应"));
      return;
    }
    if (pendingPing.isEmpty()) {
      pendingPing.resize(4);
      qToBigEndian<quint32>(++sequence, pendingPing.data());
      pingAge.start();
      send(Frames::Ping, pendingPing);
    }
    pump();
  }

  Transport *owner;
  QTcpServer listener;
  QBluetoothServer *rfcomm = nullptr;
  QBluetoothServiceInfo service;
  QTcpSocket *tcp = nullptr;
  QBluetoothSocket *bt = nullptr;
  QBluetoothAddress peer;
  QTimer timer;
  QElapsedTimer connectionAge, pingAge, outputAge;
  QByteArray pendingPing;
  Frames frames;
  quint32 sequence = 0;
  quint16 localPort = 0;
  int listenRetryTicks = 0;
  bool running = false, server = false, linkedOnce = false;
};
#else
class Transport::Impl
{
};
#endif

Transport::Transport(QObject *parent) : QObject(parent)
{
#ifdef DESKCONNECT_BLUETOOTH
  m_impl = std::make_unique<Impl>(this);
#endif
}
Transport::~Transport() = default;
bool Transport::available()
{
#ifdef DESKCONNECT_BLUETOOTH
  return true;
#else
  return false;
#endif
}
bool Transport::start(bool server, const QString &peer, QString &error)
{
#ifdef DESKCONNECT_BLUETOOTH
  return m_impl->start(server, peer, error);
#else
  Q_UNUSED(server);
  Q_UNUSED(peer);
  error = tr("当前构建未启用蓝牙直连支持。");
  return false;
#endif
}
void Transport::stop()
{
#ifdef DESKCONNECT_BLUETOOTH
  m_impl->stop();
#endif
}
quint16 Transport::port() const
{
#ifdef DESKCONNECT_BLUETOOTH
  return m_impl->localPort;
#else
  return 0;
#endif
}
} // namespace deskflow::gui::bluetooth
