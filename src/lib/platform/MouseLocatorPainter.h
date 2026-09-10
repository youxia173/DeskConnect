/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>

//! Shared ARGB (premultiplied) painter for mouse-locator overlays (Win + X11).
namespace MouseLocatorPainter {

enum class Style
{
  ShrinkRing = 0,
  Ripple,
  Crosshair,
  Spotlight,
  Pulse,
  Dot,
  Count
};

Style parseStyle(const std::string &id);
const char *styleId(Style style);
//! Default animation length for the style.
int durationMs(Style style);

void clear(uint32_t *pixels, int size);
//! Paint one animation frame. \p t is in [0, 1].
void paint(uint32_t *pixels, int size, Style style, float t);

} // namespace MouseLocatorPainter
