/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "HelpDialog.h"
#include <common/Constants.h>

#include <QEvent>
#include <QFile>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

HelpDialog::HelpDialog(QWidget *parent, const QUrl &source)
    : QDialog(parent),
      m_initialSource{source},
      m_btnBack{new QPushButton(this)},
      m_btnHome{new QPushButton(this)},
      m_browser{new QTextBrowser(this)},
      m_btnClose{new QPushButton(this)}
{
  setWindowIcon(QIcon::fromTheme(QStringLiteral("question")));

  m_btnBack->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoPrevious));
  m_btnBack->setMaximumWidth(m_btnBack->height());
  m_btnBack->setShortcut(QKeySequence::Back);
  m_btnBack->setEnabled(false);
  m_btnHome->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoHome));
  m_btnHome->setMaximumWidth(m_btnHome->height());
  m_btnHome->setEnabled(false);
  m_btnClose->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::WindowClose));
  m_btnClose->setDefault(true);
  m_btnClose->setFocus();

  updateText();

  m_browser->setMinimumSize(650, 500);
  m_browser->setOpenExternalLinks(false);
  m_browser->setOpenLinks(false);

  if (source.isLocalFile() && QFile::exists(source.toLocalFile())) {
    m_browser->setSource(source);
  } else {
    m_browser->setMarkdown(tr(
        "# DeskConnect Help\n\n"
        "Share one keyboard and mouse across multiple computers on your network.\n\n"
        "## Quick start\n\n"
        "1. On the computer with the keyboard and mouse, set mode to **Server** and click **Start**.\n"
        "2. On other computers, set mode to **Client**, enter the server hostname or IP, then click **Start**.\n"
        "3. Move the mouse past the screen edge to switch computers.\n\n"
        "## Tips\n\n"
        "- Use **Send files…** from the tray or File menu to transfer files.\n"
        "- Clipboard text and images sync when you switch screens (size limit is in settings).\n"
        "- Project page: https://github.com/youxia173/DeskConnect\n"
    ));
  }

  auto topLayout = new QHBoxLayout;
  topLayout->addWidget(m_btnBack);
  topLayout->addWidget(m_btnHome);
  topLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
  auto layout = new QVBoxLayout;
  layout->addLayout(topLayout);
  layout->addWidget(m_browser);
  layout->addWidget(m_btnClose);
  setLayout(layout);
  adjustSize();

  connect(m_browser, &QTextBrowser::anchorClicked, this, &HelpDialog::linkClicked);
  connect(m_browser, &QTextBrowser::historyChanged, this, &HelpDialog::historyChanged);
  connect(m_btnBack, &QPushButton::clicked, m_browser, &QTextBrowser::backward);
  connect(m_btnHome, &QPushButton::clicked, m_browser, &QTextBrowser::home);
  connect(m_btnClose, &QPushButton::clicked, this, &QDialog::close);
}

void HelpDialog::changeEvent(QEvent *e)
{
  QDialog::changeEvent(e);
  if (e->type() == QEvent::LanguageChange) {
    updateText();
  }
}

void HelpDialog::updateText()
{
  setWindowTitle(tr("%1 Help").arg(kAppName));
  m_btnClose->setText(tr("Close"));
}

void HelpDialog::historyChanged()
{
  m_btnBack->setEnabled(m_browser->isBackwardAvailable());
  m_btnHome->setEnabled(m_browser->source() != m_initialSource);
}

void HelpDialog::linkClicked(const QUrl &url)
{
  // Stay inside the help dialog; do not open a system browser.
  if (url.isLocalFile() || url.isRelative()) {
    m_browser->setSource(url);
  }
}
