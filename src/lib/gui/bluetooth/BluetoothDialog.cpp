// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "BluetoothDialog.h"
#include "BluetoothTransport.h"
#include "common/Settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#ifdef DESKCONNECT_BLUETOOTH
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#endif

namespace deskflow::gui::bluetooth {
Dialog::Dialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("蓝牙直连（实验功能）"));
  resize(540, 460);
  auto *layout = new QVBoxLayout(this);
  auto *help = new QLabel(
      tr("两台电脑都需要运行支持蓝牙直连的 DeskConnect，并先在系统蓝牙设置中完成配对。\n"
         "两端填写对方的蓝牙地址，一端选服务端，另一端选客户端；服务端仍按原来的方式排列屏幕。\n"
         "首次连接需要核对并接受对方的证书指纹。连接后移动到屏幕边缘即可切换。\n"
         "此模式仅传键鼠，暂不共享剪切板或文件；需要桌面进程模式，不能同时启用网线直连。"),
      this
  );
  help->setWordWrap(true);
  layout->addWidget(help);
  m_enabled = new QCheckBox(tr("启用蓝牙直连"), this);
  m_enabled->setChecked(Settings::value(Settings::Bluetooth::Enabled).toBool());
  layout->addWidget(m_enabled);
  layout->addWidget(new QLabel(tr("对方电脑的蓝牙地址（不是 IP 地址）："), this));
  m_peer = new QLineEdit(Settings::value(Settings::Bluetooth::Peer).toString(), this);
  m_peer->setPlaceholderText(QStringLiteral("AA:BB:CC:DD:EE:FF"));
  layout->addWidget(m_peer);
  auto *status = new QLabel(this);
  status->setWordWrap(true);
  status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(status);
  auto *scan = new QPushButton(tr("查找附近电脑"), this);
  auto *devices = new QListWidget(this);
  layout->addWidget(scan);
  layout->addWidget(devices);
#ifdef DESKCONNECT_BLUETOOTH
  QBluetoothLocalDevice local;
  status->setText(
      local.isValid() ? tr("本机蓝牙：%1  %2").arg(local.name(), local.address().toString())
                      : tr("未检测到蓝牙适配器，请检查系统设置。")
  );
  auto *discovery = new QBluetoothDeviceDiscoveryAgent(this);
  connect(scan, &QPushButton::clicked, this, [=] {
    devices->clear();
    scan->setEnabled(false);
    status->setText(tr("正在扫描，请让对方电脑在系统蓝牙设置中保持可发现…"));
    discovery->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
  });
  connect(discovery, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, [=](const QBluetoothDeviceInfo &info) {
    const auto address = info.address().toString();
    for (int i = 0; i < devices->count(); ++i)
      if (devices->item(i)->data(Qt::UserRole).toString() == address)
        return;
    auto *item = new QListWidgetItem(info.name() + QStringLiteral("  ") + address, devices);
    item->setData(Qt::UserRole, address);
  });
  connect(discovery, &QBluetoothDeviceDiscoveryAgent::finished, this, [=] {
    scan->setEnabled(true);
    status->setText(tr("扫描完成。选择已配对的电脑，或手动输入对方显示的蓝牙地址。"));
  });
  connect(discovery, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [=](auto) {
    scan->setEnabled(true);
    status->setText(discovery->errorString());
  });
  connect(devices, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    m_peer->setText(item->data(Qt::UserRole).toString());
  });
#else
  scan->setEnabled(false);
  status->setText(tr("当前构建没有蓝牙模块，可关闭已保存的蓝牙配置以恢复网络连接。"));
#endif
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &Dialog::save);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void Dialog::save()
{
  if (m_enabled->isChecked()) {
    QString error;
    static const QRegularExpression mac(QStringLiteral("^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$"));
    if (!Transport::available())
      error = tr("当前构建没有蓝牙模块。");
    else if (!mac.match(m_peer->text().trimmed()).hasMatch() ||
             m_peer->text().trimmed() == QStringLiteral("00:00:00:00:00:00"))
      error = tr("请输入有效的对方蓝牙地址，例如 AA:BB:CC:DD:EE:FF。");
    else if (Settings::value(Settings::Core::DirectEthernet).toBool())
      error = tr("请先取消主窗口中的网线直连。");
    else if (Settings::value(Settings::Core::ProcessMode).value<Settings::ProcessMode>() != Settings::Desktop)
      error = tr("请先在偏好设置中选择桌面进程模式，再启用蓝牙直连。");
    else if (!Settings::value(Settings::Security::TlsEnabled).toBool() ||
             !Settings::value(Settings::Security::CheckPeers).toBool())
      error = tr("请先在偏好设置中启用 TLS 加密和证书指纹验证。");
    if (!error.isEmpty()) {
      QMessageBox::warning(this, windowTitle(), error);
      return;
    }
  }
  Settings::setValue(Settings::Bluetooth::Peer, m_peer->text().trimmed().toUpper());
  Settings::setValue(Settings::Bluetooth::Enabled, m_enabled->isChecked());
  Settings::save();
  accept();
}
} // namespace deskflow::gui::bluetooth
