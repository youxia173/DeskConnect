/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "widgets/FileTransferProgressPanel.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

FileTransferProgressPanel::FileTransferProgressPanel(QWidget *parent)
    : QWidget(parent)
{
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  hide();

  auto *frame = new QFrame(this);
  frame->setFrameShape(QFrame::StyledPanel);
  frame->setFrameShadow(QFrame::Sunken);

  m_titleLabel = new QLabel(frame);
  QFont titleFont = m_titleLabel->font();
  titleFont.setBold(true);
  m_titleLabel->setFont(titleFont);

  m_fileLabel = new QLabel(frame);
  m_fileLabel->setWordWrap(false);
  m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_sizeLabel = new QLabel(frame);
  m_speedLabel = new QLabel(frame);
  m_etaLabel = new QLabel(frame);

  m_progress = new QProgressBar(frame);
  m_progress->setRange(0, 1000);
  m_progress->setValue(0);
  m_progress->setTextVisible(true);
  m_progress->setMinimumHeight(16);
  m_progress->setMaximumHeight(18);

  m_fullSpeedButton = new QPushButton(frame);
  m_cancelButton = new QPushButton(frame);
  m_fullSpeedButton->setVisible(false);
  m_cancelButton->setVisible(false);
  m_fullSpeedButton->setAutoDefault(false);
  m_cancelButton->setAutoDefault(false);
  connect(m_fullSpeedButton, &QPushButton::clicked, this, &FileTransferProgressPanel::fullSpeedRequested);
  connect(m_cancelButton, &QPushButton::clicked, this, &FileTransferProgressPanel::cancelRequested);

  auto *topRow = new QHBoxLayout;
  topRow->setContentsMargins(0, 0, 0, 0);
  topRow->setSpacing(8);
  topRow->addWidget(m_titleLabel);
  topRow->addWidget(m_fileLabel, 1);
  topRow->addWidget(m_sizeLabel);

  auto *bottomRow = new QHBoxLayout;
  bottomRow->setContentsMargins(0, 0, 0, 0);
  bottomRow->setSpacing(8);
  bottomRow->addWidget(m_progress, 1);
  bottomRow->addWidget(m_speedLabel);
  bottomRow->addWidget(m_etaLabel);
  bottomRow->addWidget(m_fullSpeedButton);
  bottomRow->addWidget(m_cancelButton);

  auto *inner = new QVBoxLayout(frame);
  inner->setContentsMargins(8, 6, 8, 6);
  inner->setSpacing(4);
  inner->addLayout(topRow);
  inner->addLayout(bottomRow);

  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 2, 0, 2);
  outer->setSpacing(0);
  outer->addWidget(frame);

  retranslate();
}

void FileTransferProgressPanel::setHostControlsVisible(bool visible)
{
  m_hostControls = visible;
  m_fullSpeedButton->setVisible(visible && m_sending);
  m_cancelButton->setVisible(visible);
}

void FileTransferProgressPanel::setFullSpeedEnabled(bool enabled)
{
  m_fullSpeedButton->setEnabled(enabled);
}

void FileTransferProgressPanel::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
  }
  QWidget::changeEvent(event);
}

void FileTransferProgressPanel::retranslate()
{
  m_titleLabel->setText(m_sending ? tr("Sending") : tr("Receiving"));
  m_fullSpeedButton->setText(tr("Full speed"));
  m_fullSpeedButton->setToolTip(tr("Disable speed limit for this transfer only"));
  m_cancelButton->setText(tr("Cancel"));
  m_cancelButton->setToolTip(tr("Cancel this file transfer"));
}

QString FileTransferProgressPanel::formatBytes(qint64 bytes)
{
  const double kb = 1024.0;
  const double mb = kb * 1024.0;
  const double gb = mb * 1024.0;
  if (bytes >= static_cast<qint64>(gb)) {
    return tr("%1 GB").arg(bytes / gb, 0, 'f', 2);
  }
  if (bytes >= static_cast<qint64>(mb)) {
    return tr("%1 MB").arg(bytes / mb, 0, 'f', 2);
  }
  if (bytes >= static_cast<qint64>(kb)) {
    return tr("%1 KB").arg(bytes / kb, 0, 'f', 1);
  }
  return tr("%1 B").arg(bytes);
}

QString FileTransferProgressPanel::formatEta(int seconds)
{
  if (seconds < 0) {
    return tr("Estimating…");
  }
  if (seconds < 60) {
    return tr("%1 s left").arg(seconds);
  }
  const int minutes = seconds / 60;
  const int secs = seconds % 60;
  if (minutes < 60) {
    return tr("%1:%2 left").arg(minutes).arg(secs, 2, 10, QChar('0'));
  }
  const int hours = minutes / 60;
  const int mins = minutes % 60;
  return tr("%1:%2:%3 left").arg(hours).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}

void FileTransferProgressPanel::updateProgress(
    bool sending, const QString &fileName, int fileIndex, int fileCount, qint64 bytesDone, qint64 bytesTotal,
    qint64 bytesPerSec, int etaSeconds
)
{
  m_sending = sending;
  retranslate();
  setHostControlsVisible(m_hostControls);

  const int displayIndex = fileCount > 0 ? qMin(fileIndex + 1, fileCount) : 0;
  m_fileLabel->setText(tr("%1 (%2/%3)").arg(fileName).arg(displayIndex).arg(fileCount));
  m_fileLabel->setToolTip(fileName);

  if (bytesTotal > 0) {
    m_sizeLabel->setText(tr("%1 / %2").arg(formatBytes(bytesDone), formatBytes(bytesTotal)));
    const int value = static_cast<int>(qMin<qint64>(1000, (bytesDone * 1000) / bytesTotal));
    m_progress->setRange(0, 1000);
    m_progress->setValue(value);
    m_progress->setFormat(tr("%p%"));
  } else {
    m_sizeLabel->setText(formatBytes(bytesDone));
    m_progress->setRange(0, 0);
  }

  m_speedLabel->setText(tr("%1/s").arg(formatBytes(bytesPerSec)));
  m_etaLabel->setText(formatEta(etaSeconds));

  if (!isVisible()) {
    show();
    if (auto *win = window()) {
      const int need = win->sizeHint().height();
      if (win->height() < need) {
        win->resize(win->width(), need);
      }
    }
  }
}

void FileTransferProgressPanel::transferFinished(bool success, const QString &message)
{
  m_fullSpeedButton->setVisible(false);
  m_cancelButton->setVisible(false);
  m_progress->setRange(0, 1000);
  m_progress->setValue(success ? 1000 : m_progress->value());
  m_etaLabel->setText(success ? tr("Done") : tr("Failed"));
  if (!message.isEmpty()) {
    m_sizeLabel->setText(message);
    m_sizeLabel->setToolTip(message);
  }
  // Hide quickly on failure so the form is usable again; success can linger briefly.
  QTimer::singleShot(success ? 2000 : 800, this, &QWidget::hide);
}
