/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Qt headers must come before X11 (Status/Bool macros).
#include "base/Log.h"
#include "platform/ClipboardImage.h"

#include "platform/XWindowsClipboardPNGConverter.h"

XWindowsClipboardPNGConverter::XWindowsClipboardPNGConverter(Display *display, const char *name)
    : m_atom(XInternAtom(display, name, False))
{
}

IClipboard::Format XWindowsClipboardPNGConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

Atom XWindowsClipboardPNGConverter::getAtom() const
{
  return m_atom;
}

int XWindowsClipboardPNGConverter::getDataSize() const
{
  return 8;
}

std::string XWindowsClipboardPNGConverter::fromIClipboard(const std::string &dib) const
{
  return deskflow::ClipboardImage::dibToPng(dib);
}

std::string XWindowsClipboardPNGConverter::toIClipboard(const std::string &png) const
{
  const auto dib = deskflow::ClipboardImage::pngToDib(png);
  if (!dib.empty()) {
    LOG_DEBUG("converted clipboard image/png (%zu bytes) to DIB (%zu bytes)", png.size(), dib.size());
  }
  return dib;
}
