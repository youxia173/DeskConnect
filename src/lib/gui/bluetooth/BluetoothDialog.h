// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

namespace deskflow::gui::bluetooth {
class Dialog : public QDialog
{
  Q_OBJECT
public:
  explicit Dialog(QWidget *parent = nullptr);

private:
  void save();
  QCheckBox *m_enabled;
  QLineEdit *m_peer;
};
} // namespace deskflow::gui::bluetooth
