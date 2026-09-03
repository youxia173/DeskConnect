/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/XWindowsClipboard.h"

//! Convert text/uri-list to/from IClipboard::Format::Files
class XWindowsClipboardURIListConverter : public IXWindowsClipboardConverter
{
public:
  explicit XWindowsClipboardURIListConverter(Display *display, const char *name = "text/uri-list");
  ~XWindowsClipboardURIListConverter() override = default;

  IClipboard::Format getFormat() const override;
  Atom getAtom() const override;
  int getDataSize() const override;
  std::string fromIClipboard(const std::string &) const override;
  std::string toIClipboard(const std::string &) const override;

private:
  Atom m_atom;
};
