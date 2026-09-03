/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QString>
#include <QWidget>

//! Inline file-transfer status strip for the main window (not a popup).
class FileTransferProgressPanel : public QWidget
{
  Q_OBJECT

public:
  explicit FileTransferProgressPanel(QWidget *parent = nullptr);

public Q_SLOTS:
  void updateProgress(
      bool sending, const QString &fileName, int fileIndex, int fileCount, qint64 bytesDone, qint64 bytesTotal,
      qint64 bytesPerSec, int etaSeconds
  );
  void transferFinished(bool success, const QString &message);

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
  bool m_sending = true;
};
