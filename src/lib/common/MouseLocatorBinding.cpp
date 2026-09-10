/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MouseLocatorBinding.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace {

int qtButtonToId(int qtButton)
{
  switch (qtButton) {
  case Qt::LeftButton:
    return 1;
  case Qt::MiddleButton:
    return 2;
  case Qt::RightButton:
    return 3;
  case Qt::BackButton:
    return 4;
  case Qt::ForwardButton:
    return 5;
  default:
    // Unknown extra button; map to Extra0.
    return 4;
  }
}

int buttonIdToQt(int buttonId)
{
  switch (buttonId) {
  case 1:
    return Qt::LeftButton;
  case 2:
    return Qt::MiddleButton;
  case 3:
    return Qt::RightButton;
  case 4:
    return Qt::BackButton;
  case 5:
    return Qt::ForwardButton;
  default:
    return Qt::MiddleButton;
  }
}

int nameToQtKey(const QString &name)
{
  if (name.compare(QStringLiteral("Space"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Space;
  if (name.compare(QStringLiteral("Escape"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Escape;
  if (name.compare(QStringLiteral("Tab"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Tab;
  if (name.compare(QStringLiteral("LeftTab"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Backtab;
  if (name.compare(QStringLiteral("BackSpace"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Backspace;
  if (name.compare(QStringLiteral("Return"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Return;
  if (name.compare(QStringLiteral("KP_Enter"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Enter;
  if (name.compare(QStringLiteral("Insert"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Insert;
  if (name.compare(QStringLiteral("Delete"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Delete;
  if (name.compare(QStringLiteral("Home"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Home;
  if (name.compare(QStringLiteral("End"), Qt::CaseInsensitive) == 0)
    return Qt::Key_End;
  if (name.compare(QStringLiteral("Left"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Left;
  if (name.compare(QStringLiteral("Up"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Up;
  if (name.compare(QStringLiteral("Right"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Right;
  if (name.compare(QStringLiteral("Down"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Down;
  if (name.compare(QStringLiteral("PageUp"), Qt::CaseInsensitive) == 0)
    return Qt::Key_PageUp;
  if (name.compare(QStringLiteral("PageDown"), Qt::CaseInsensitive) == 0)
    return Qt::Key_PageDown;
  if (name.compare(QStringLiteral("Pause"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Pause;
  if (name.compare(QStringLiteral("Print"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Print;
  if (name.compare(QStringLiteral("Menu"), Qt::CaseInsensitive) == 0)
    return Qt::Key_Menu;

  static const QRegularExpression fKey(QStringLiteral("^F(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
  const auto fMatch = fKey.match(name);
  if (fMatch.hasMatch()) {
    const int n = fMatch.captured(1).toInt();
    if (n >= 1 && n <= 35)
      return Qt::Key_F1 + n - 1;
  }

  if (name.startsWith(QStringLiteral("\\u"), Qt::CaseInsensitive) && name.size() == 6) {
    bool ok = false;
    const int code = name.mid(2).toInt(&ok, 16);
    if (ok)
      return code;
  }

  if (name.size() == 1) {
    const QChar c = name.at(0).toUpper();
    if (c.unicode() >= 33 && c.unicode() < 127)
      return c.unicode();
  }

  return 0;
}

bool appendModifierName(KeySequence &seq, const QString &name)
{
  if (name.compare(QStringLiteral("Shift"), Qt::CaseInsensitive) == 0)
    return seq.appendKey(Qt::Key_Shift, Qt::ShiftModifier);
  if (name.compare(QStringLiteral("Control"), Qt::CaseInsensitive) == 0)
    return seq.appendKey(Qt::Key_Control, Qt::ControlModifier);
  if (name.compare(QStringLiteral("Alt"), Qt::CaseInsensitive) == 0)
    return seq.appendKey(Qt::Key_Alt, Qt::AltModifier);
  if (name.compare(QStringLiteral("Meta"), Qt::CaseInsensitive) == 0 ||
      name.compare(QStringLiteral("Super"), Qt::CaseInsensitive) == 0)
    return seq.appendKey(Qt::Key_Meta, Qt::MetaModifier);
  return false;
}

QString unwrap(const QString &binding, const QString &prefix)
{
  if (!binding.startsWith(prefix) || !binding.endsWith(QLatin1Char(')')))
    return {};
  return binding.mid(prefix.size(), binding.size() - prefix.size() - 1);
}

} // namespace

namespace MouseLocatorBinding {

QString fromSequence(const KeySequence &seq)
{
  if (!seq.valid() || seq.sequence().isEmpty())
    return QString::fromUtf8(kDefault);

  if (seq.isMouseButton()) {
    const int buttonId = qtButtonToId(seq.sequence().last());
    return QStringLiteral("mousebutton(%1)").arg(buttonId);
  }

  return QStringLiteral("keystroke(%1)").arg(seq.toString());
}

KeySequence toSequence(const QString &binding)
{
  KeySequence seq;
  const QString trimmed = binding.trimmed();
  if (trimmed.isEmpty()) {
    seq.appendMouseButton(Qt::MiddleButton);
    return seq;
  }

  if (const QString inner = unwrap(trimmed, QStringLiteral("mousebutton(")); !inner.isEmpty()) {
    // Optional modifiers+buttonId; UI currently stores button only.
    const int plus = inner.lastIndexOf(QLatin1Char('+'));
    const QString buttonPart = (plus >= 0) ? inner.mid(plus + 1) : inner;
    bool ok = false;
    const int buttonId = buttonPart.trimmed().toInt(&ok);
    if (ok && buttonId > 0) {
      if (plus >= 0) {
        const QStringList mods = inner.left(plus).split(QLatin1Char('+'), Qt::SkipEmptyParts);
        for (const QString &mod : mods)
          appendModifierName(seq, mod.trimmed());
      }
      seq.appendMouseButton(buttonIdToQt(buttonId));
      return seq;
    }
  }

  if (const QString inner = unwrap(trimmed, QStringLiteral("keystroke(")); !inner.isEmpty()) {
    const QStringList parts = inner.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
      const QString part = parts.at(i).trimmed();
      if (appendModifierName(seq, part))
        continue;
      const int key = nameToQtKey(part);
      if (key != 0)
        seq.appendKey(key, 0);
    }
    if (seq.valid())
      return seq;
  }

  seq = KeySequence();
  seq.appendMouseButton(Qt::MiddleButton);
  return seq;
}

QString toDisplayString(const KeySequence &seq)
{
  if (!seq.valid() || seq.sequence().isEmpty()) {
    return QCoreApplication::translate("MouseLocatorBinding", "Not set");
  }

  QStringList parts;
  for (int key : seq.sequence()) {
    if (key < Qt::Key_Space) {
      switch (key) {
      case Qt::LeftButton:
        parts << QCoreApplication::translate("MouseLocatorBinding", "Left mouse button");
        break;
      case Qt::MiddleButton:
        parts << QCoreApplication::translate("MouseLocatorBinding", "Middle mouse button");
        break;
      case Qt::RightButton:
        parts << QCoreApplication::translate("MouseLocatorBinding", "Right mouse button");
        break;
      case Qt::BackButton:
        parts << QCoreApplication::translate("MouseLocatorBinding", "Mouse back button");
        break;
      case Qt::ForwardButton:
        parts << QCoreApplication::translate("MouseLocatorBinding", "Mouse forward button");
        break;
      default:
        parts << QCoreApplication::translate("MouseLocatorBinding", "Mouse side button");
        break;
      }
      continue;
    }

    if (key & Qt::ShiftModifier) {
      parts << QCoreApplication::translate("MouseLocatorBinding", "Shift");
      continue;
    }
    if (key & Qt::ControlModifier) {
      parts << QCoreApplication::translate("MouseLocatorBinding", "Ctrl");
      continue;
    }
    if (key & Qt::AltModifier) {
      parts << QCoreApplication::translate("MouseLocatorBinding", "Alt");
      continue;
    }
    if (key & Qt::MetaModifier) {
      parts << QCoreApplication::translate("MouseLocatorBinding", "Win");
      continue;
    }

    key &= ~Qt::KeypadModifier;

    switch (key) {
    case Qt::Key_Space:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Space");
      break;
    case Qt::Key_Escape:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Esc");
      break;
    case Qt::Key_Tab:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Tab");
      break;
    case Qt::Key_Backtab:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Shift+Tab");
      break;
    case Qt::Key_Backspace:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Backspace");
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Enter");
      break;
    case Qt::Key_Insert:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Insert");
      break;
    case Qt::Key_Delete:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Delete");
      break;
    case Qt::Key_Home:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Home");
      break;
    case Qt::Key_End:
      parts << QCoreApplication::translate("MouseLocatorBinding", "End");
      break;
    case Qt::Key_Left:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Left Arrow");
      break;
    case Qt::Key_Up:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Up Arrow");
      break;
    case Qt::Key_Right:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Right Arrow");
      break;
    case Qt::Key_Down:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Down Arrow");
      break;
    case Qt::Key_PageUp:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Page Up");
      break;
    case Qt::Key_PageDown:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Page Down");
      break;
    case Qt::Key_CapsLock:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Caps Lock");
      break;
    case Qt::Key_NumLock:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Num Lock");
      break;
    case Qt::Key_ScrollLock:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Scroll Lock");
      break;
    case Qt::Key_Pause:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Pause");
      break;
    case Qt::Key_Print:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Print Screen");
      break;
    case Qt::Key_Menu:
      parts << QCoreApplication::translate("MouseLocatorBinding", "Menu");
      break;
    default:
      if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        parts << QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
      } else if (key < 0x80) {
        parts << QChar(key & 0x7f).toUpper();
      } else if (key < 0x10000) {
        parts << QChar(key).toUpper();
      } else {
        parts << QStringLiteral("\\u%1").arg(key, 4, 16, QLatin1Char('0'));
      }
      break;
    }
  }

  return parts.join(QLatin1Char('+'));
}

} // namespace MouseLocatorBinding
