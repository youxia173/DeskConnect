/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/XWindowsClipboardURIListConverter.h"

#include "filetransfer/FileTransfer.h"
#include <cstring>

XWindowsClipboardURIListConverter::XWindowsClipboardURIListConverter(Display *display, const char *name)
    : m_atom(XInternAtom(display, name, False)), m_gnome(std::strcmp(name, "x-special/gnome-copied-files") == 0)
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
  const auto uris = deskflow::uriListFromClipboardData(data);
  return m_gnome ? "copy\n" + uris : uris;
}

std::string XWindowsClipboardURIListConverter::toIClipboard(const std::string &data) const
{
  if (m_gnome) {
    const auto newline = data.find('\n');
    return newline == std::string::npos ? std::string{} : deskflow::clipboardDataFromUriList(data.substr(newline + 1));
  }
  return deskflow::clipboardDataFromUriList(data);
}
