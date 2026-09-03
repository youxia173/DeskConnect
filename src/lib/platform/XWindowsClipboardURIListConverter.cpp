/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/XWindowsClipboardURIListConverter.h"

#include "filetransfer/FileTransfer.h"

XWindowsClipboardURIListConverter::XWindowsClipboardURIListConverter(Display *display, const char *name)
    : m_atom(XInternAtom(display, name, False))
{
}

IClipboard::Format XWindowsClipboardURIListConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

Atom XWindowsClipboardURIListConverter::getAtom() const
{
  return m_atom;
}

int XWindowsClipboardURIListConverter::getDataSize() const
{
  return 8;
}

std::string XWindowsClipboardURIListConverter::fromIClipboard(const std::string &data) const
{
  return deskflow::uriListFromClipboardData(data);
}

std::string XWindowsClipboardURIListConverter::toIClipboard(const std::string &data) const
{
  return deskflow::clipboardDataFromUriList(data);
}
