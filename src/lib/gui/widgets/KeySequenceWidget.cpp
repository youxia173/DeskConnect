/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "KeySequenceWidget.h"

#include "common/MouseLocatorBinding.h"

#include <QApplication>
#include <QMouseEvent>

KeySequenceWidget::KeySequenceWidget(QWidget *parent, const KeySequence &seq)
    : QPushButton(parent),
      m_KeySequence(seq),
      m_BackupSequence(seq)
{
  // ClickFocus so we can take keyboard focus while rebinding without joining tab order.
  setFocusPolicy(Qt::ClickFocus);
  updateOutput();
}

void KeySequenceWidget::setKeySequence(const KeySequence &seq)
{
  m_KeySequence = seq;
  m_BackupSequence = seq;

  setStatus(Stopped);
  updateOutput();
}

void KeySequenceWidget::mousePressEvent(QMouseEvent *event)
{
  event->accept();

  if (status() == Stopped) {
    startRecording();
    return;
  }

  if (m_RejectLeftButton && event->button() == Qt::LeftButton) {
    // Left click while recording cancels instead of binding.
    cancelRecording();
    return;
  }

  if (m_KeySequence.appendMouseButton(event->button()))
    stopRecording();

  updateOutput();
}

void KeySequenceWidget::startRecording()
{
  m_KeySequence = KeySequence();
  setDown(true);
  setFocus(Qt::MouseFocusReason);
  grabKeyboard();
  grabMouse();
  if (!m_FilterInstalled) {
    qApp->installEventFilter(this);
    m_FilterInstalled = true;
  }
  setStatus(Recording);
  if (!m_RecordingText.isEmpty())
    setText(m_RecordingText);
}

void KeySequenceWidget::finishGrab()
{
  if (m_FilterInstalled) {
    qApp->removeEventFilter(this);
    m_FilterInstalled = false;
  }
  releaseKeyboard();
  releaseMouse();
  setDown(false);
}

void KeySequenceWidget::stopRecording()
{
  if (!keySequence().valid()) {
    m_KeySequence = backupSequence();
  }

  finishGrab();
  setStatus(Stopped);
  updateOutput();
  Q_EMIT keySequenceChanged();
}

void KeySequenceWidget::cancelRecording()
{
  m_KeySequence = backupSequence();
  finishGrab();
  setStatus(Stopped);
  updateOutput();
}

bool KeySequenceWidget::eventFilter(QObject *watched, QEvent *event)
{
  Q_UNUSED(watched);

  if (status() != Recording)
    return false;

  switch (event->type()) {
  case QEvent::KeyPress:
    keyPressEvent(static_cast<QKeyEvent *>(event));
    return true;

  case QEvent::KeyRelease:
  case QEvent::ShortcutOverride:
    event->accept();
    return true;

  case QEvent::MouseButtonPress:
    // Route global mouse presses through our handler (side buttons etc.).
    mousePressEvent(static_cast<QMouseEvent *>(event));
    return true;

  case QEvent::MouseButtonRelease:
  case QEvent::MouseButtonDblClick:
    event->accept();
    return true;

  default:
    break;
  }

  return false;
}

bool KeySequenceWidget::event(QEvent *event)
{
  if (status() == Recording) {
    switch (event->type()) {
    case QEvent::KeyPress:
      keyPressEvent(static_cast<QKeyEvent *>(event));
      return true;

    case QEvent::MouseButtonRelease:
    case QEvent::ShortcutOverride:
      event->accept();
      return true;

    case QEvent::FocusOut:
      // Keep recording; Esc / left-click cancel. Avoid losing capture when
      // sibling widgets briefly take focus (e.g. line edits in tab order).
      event->accept();
      return true;

    default:
      break;
    }
  }

  return QPushButton::event(event);
}

void KeySequenceWidget::keyPressEvent(QKeyEvent *event)
{
  event->accept();

  if (status() == Stopped)
    return;

  if (m_EscapeCancels && event->key() == Qt::Key_Escape) {
    cancelRecording();
    return;
  }

  // Do not treat Tab as focus navigation while capturing.
  if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
    if (m_KeySequence.appendKey(event->key(), event->modifiers()))
      stopRecording();
    else
      updateOutput();
    return;
  }

  if (m_KeySequence.appendKey(event->key(), event->modifiers()))
    stopRecording();

  updateOutput();
}

void KeySequenceWidget::updateOutput()
{
  if (status() == Recording && !m_KeySequence.valid() && !m_RecordingText.isEmpty()) {
    setText(m_RecordingText);
    return;
  }

  if (m_LocalizedDisplay) {
    setText(MouseLocatorBinding::toDisplayString(m_KeySequence));
    return;
  }

  if (m_DeskflowMouseIds) {
    setText(MouseLocatorBinding::fromSequence(m_KeySequence));
    return;
  }

  QString s;

  if (m_KeySequence.isMouseButton())
    s = mousePrefix() + m_KeySequence.toString() + mousePostfix();
  else
    s = keyPrefix() + m_KeySequence.toString() + keyPostfix();

  setText(s);
}
