/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert CF_HDROP file lists to/from IClipboard::Format::Files
class MSWindowsClipboardFileConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardFileConverter() = default;
  ~MSWindowsClipboardFileConverter() override = default;

  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE data) const override;
};
