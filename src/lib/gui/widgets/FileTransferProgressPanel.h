/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QWidget>

//! Inline file-transfer status strip for the main window (not a popup).
class FileTransferProgressPanel : public QWidget
{
  Q_OBJECT

public:
  explicit FileTransferProgressPanel(QWidget *parent = nullptr);

  //! Show host-only controls (full-speed + cancel) while a transfer is active.
  void setHostControlsVisible(bool visible);
  //! Enable/disable the one-shot full-speed button (e.g. already full speed).
  void setFullSpeedEnabled(bool enabled);

public Q_SLOTS:
  void updateProgress(
      bool sending, const QString &fileName, int fileIndex, int fileCount, qint64 bytesDone, qint64 bytesTotal,
      qint64 bytesPerSec, int etaSeconds
  );
  void transferFinished(bool success, const QString &message);

Q_SIGNALS:
  void fullSpeedRequested();
  void cancelRequested();

protected:
  void changeEvent(QEvent *event) override;

private:
  void retranslate();
  static QString formatBytes(qint64 bytes);
  static QString formatEta(int seconds);

  QLabel *m_titleLabel = nullptr;
  QLabel *m_fileLabel = nullptr;
  QLabel *m_sizeLabel = nullptr;
  QLabel *m_speedLabel = nullptr;
  QLabel *m_etaLabel = nullptr;
  QProgressBar *m_progress = nullptr;
  QPushButton *m_fullSpeedButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  bool m_sending = true;
  bool m_hostControls = false;
};
