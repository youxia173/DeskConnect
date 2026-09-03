/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert Windows clipboard PNG <-> IClipboard::Bitmap (DIB)
class MSWindowsClipboardPNGConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardPNGConverter();
  ~MSWindowsClipboardPNGConverter() override = default;

  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &data) const override;
  std::string toIClipboard(HANDLE data) const override;

private:
  UINT m_format;
};
