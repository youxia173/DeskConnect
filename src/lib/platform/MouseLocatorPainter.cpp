/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MouseLocatorPainter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float easeOutCubic(float t)
{
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

float easeInOut(float t)
{
  return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

void blendPremul(uint32_t *pixel, uint8_t r, uint8_t g, uint8_t b, float cover)
{
  if (cover <= 0.0f) {
    return;
  }
  if (cover > 1.0f) {
    cover = 1.0f;
  }
  const uint8_t a = static_cast<uint8_t>(255.0f * cover + 0.5f);
  if (a == 0) {
    return;
  }
  const uint32_t sb = (b * a) / 255u;
  const uint32_t sg = (g * a) / 255u;
  const uint32_t sr = (r * a) / 255u;
  const uint32_t src = (static_cast<uint32_t>(a) << 24) | (sr << 16) | (sg << 8) | sb;

  const uint32_t dst = *pixel;
  if (dst == 0) {
    *pixel = src;
    return;
  }

  const uint32_t da = (dst >> 24) & 0xffu;
  const uint32_t inv = 255u - a;
  const uint32_t outA = a + (da * inv) / 255u;
  const uint32_t outB = sb + (((dst & 0xffu) * inv) / 255u);
  const uint32_t outG = sg + ((((dst >> 8) & 0xffu) * inv) / 255u);
  const uint32_t outR = sr + ((((dst >> 16) & 0xffu) * inv) / 255u);
  *pixel = (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

void drawRing(uint32_t *pixels, int size, float radius, float thickness, uint8_t r, uint8_t g, uint8_t b, float alpha)
{
  if (radius <= 0.5f || thickness <= 0.0f || alpha <= 0.0f) {
    return;
  }
  const float cx = (size - 1) * 0.5f;
  const float cy = (size - 1) * 0.5f;
  const float half = thickness * 0.5f;
  const float rOuter = radius + half;
  const float rInner = (std::max)(0.0f, radius - half);
  const int y0 = (std::max)(0, static_cast<int>(cy - rOuter - 2));
  const int y1 = (std::min)(size - 1, static_cast<int>(cy + rOuter + 2));
  const int x0 = (std::max)(0, static_cast<int>(cx - rOuter - 2));
  const int x1 = (std::min)(size - 1, static_cast<int>(cx + rOuter + 2));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      float cover = 0.0f;
      if (d <= rOuter + 1.0f && d >= rInner - 1.0f) {
        const float outer = 1.0f - std::clamp(d - rOuter, 0.0f, 1.0f);
        const float inner = 1.0f - std::clamp(rInner - d, 0.0f, 1.0f);
        cover = outer * inner;
      }
      if (cover > 0.0f) {
        blendPremul(&pixels[y * size + x], r, g, b, cover * alpha);
      }
    }
  }
}

void drawDisk(uint32_t *pixels, int size, float radius, uint8_t r, uint8_t g, uint8_t b, float alpha)
{
  if (radius <= 0.5f || alpha <= 0.0f) {
    return;
  }
  const float cx = (size - 1) * 0.5f;
  const float cy = (size - 1) * 0.5f;
  const int y0 = (std::max)(0, static_cast<int>(cy - radius - 2));
  const int y1 = (std::min)(size - 1, static_cast<int>(cy + radius + 2));
  const int x0 = (std::max)(0, static_cast<int>(cx - radius - 2));
  const int x1 = (std::min)(size - 1, static_cast<int>(cx + radius + 2));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float cover = 1.0f - std::clamp(d - radius, 0.0f, 1.0f);
      if (cover > 0.0f) {
        blendPremul(&pixels[y * size + x], r, g, b, cover * alpha);
      }
    }
  }
}

void drawSoftDisk(uint32_t *pixels, int size, float radius, uint8_t r, uint8_t g, uint8_t b, float alpha)
{
  if (radius <= 0.5f || alpha <= 0.0f) {
    return;
  }
  const float cx = (size - 1) * 0.5f;
  const float cy = (size - 1) * 0.5f;
  const int y0 = (std::max)(0, static_cast<int>(cy - radius - 2));
  const int y1 = (std::min)(size - 1, static_cast<int>(cy + radius + 2));
  const int x0 = (std::max)(0, static_cast<int>(cx - radius - 2));
  const int x1 = (std::min)(size - 1, static_cast<int>(cx + radius + 2));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d > radius) {
        continue;
      }
      const float falloff = 1.0f - (d / radius);
      const float cover = falloff * falloff;
      blendPremul(&pixels[y * size + x], r, g, b, cover * alpha);
    }
  }
}

void drawLine(
    uint32_t *pixels, int size, float x0, float y0, float x1, float y1, float thickness, uint8_t r, uint8_t g, uint8_t b,
    float alpha
)
{
  if (alpha <= 0.0f || thickness <= 0.0f) {
    return;
  }
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.5f) {
    return;
  }
  const float nx = -dy / len;
  const float ny = dx / len;
  const float half = thickness * 0.5f;
  const float minX = (std::min)(x0, x1) - half - 2.0f;
  const float maxX = (std::max)(x0, x1) + half + 2.0f;
  const float minY = (std::min)(y0, y1) - half - 2.0f;
  const float maxY = (std::max)(y0, y1) + half + 2.0f;
  const int ix0 = (std::max)(0, static_cast<int>(minX));
  const int ix1 = (std::min)(size - 1, static_cast<int>(maxX));
  const int iy0 = (std::max)(0, static_cast<int>(minY));
  const int iy1 = (std::min)(size - 1, static_cast<int>(maxY));

  for (int y = iy0; y <= iy1; ++y) {
    for (int x = ix0; x <= ix1; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      const float tx = ((px - x0) * dx + (py - y0) * dy) / (len * len);
      if (tx < 0.0f || tx > 1.0f) {
        continue;
      }
      const float lx = x0 + tx * dx;
      const float ly = y0 + tx * dy;
      const float dist = std::abs((px - lx) * nx + (py - ly) * ny);
      const float cover = 1.0f - std::clamp(dist - half + 0.5f, 0.0f, 1.0f);
      if (cover > 0.0f) {
        blendPremul(&pixels[y * size + x], r, g, b, cover * alpha);
      }
    }
  }
}

void paintShrink(uint32_t *pixels, int size, float t)
{
  const float e = easeOutCubic(t);
  const float half = size * 0.5f;
  const float maxRadius = half - 8.0f;
  const float minRadius = (std::max)(10.0f, size * 0.04f);
  const float radius = maxRadius + (minRadius - maxRadius) * e;
  const float fade = 1.0f - e * 0.35f;
  const float thickness = (std::max)(4.0f, size * 0.035f) * (1.0f - 0.25f * e);
  drawDisk(pixels, size, radius, 255, 196, 0, 0.18f * fade);
  drawRing(pixels, size, radius, thickness, 255, 220, 40, 0.95f * fade);
  drawRing(pixels, size, radius - thickness * 0.85f, thickness * 0.55f, 255, 255, 255, 0.85f * fade);
}

void paintRipple(uint32_t *pixels, int size, float t)
{
  const float half = size * 0.5f;
  const float maxRadius = half - 6.0f;
  const float thickness = (std::max)(3.5f, size * 0.028f);
  for (int i = 0; i < 3; ++i) {
    const float phase = t - static_cast<float>(i) * 0.18f;
    if (phase < 0.0f || phase > 1.0f) {
      continue;
    }
    const float e = easeOutCubic(phase);
    const float radius = 12.0f + (maxRadius - 12.0f) * e;
    const float fade = (1.0f - e) * (1.0f - static_cast<float>(i) * 0.12f);
    drawRing(pixels, size, radius, thickness, 255, 220, 40, 0.9f * fade);
    drawRing(pixels, size, radius - thickness * 0.7f, thickness * 0.45f, 255, 255, 255, 0.7f * fade);
  }
  drawDisk(pixels, size, 8.0f + 4.0f * (1.0f - t), 255, 240, 80, 0.55f * (1.0f - t * 0.7f));
}

void paintCrosshair(uint32_t *pixels, int size, float t)
{
  const float e = easeOutCubic(t);
  const float cx = (size - 1) * 0.5f;
  const float cy = (size - 1) * 0.5f;
  const float half = size * 0.5f;
  const float arm = (half - 10.0f) * (1.0f - 0.55f * e);
  const float gap = 10.0f + 6.0f * e;
  const float thick = (std::max)(3.5f, size * 0.028f) * (1.0f - 0.2f * e);
  const float fade = 1.0f - e * 0.4f;

  drawLine(pixels, size, cx - arm, cy, cx - gap, cy, thick, 255, 220, 40, 0.95f * fade);
  drawLine(pixels, size, cx + gap, cy, cx + arm, cy, thick, 255, 220, 40, 0.95f * fade);
  drawLine(pixels, size, cx, cy - arm, cx, cy - gap, thick, 255, 220, 40, 0.95f * fade);
  drawLine(pixels, size, cx, cy + gap, cx, cy + arm, thick, 255, 220, 40, 0.95f * fade);

  drawLine(pixels, size, cx - arm, cy, cx - gap, cy, thick * 0.45f, 255, 255, 255, 0.8f * fade);
  drawLine(pixels, size, cx + gap, cy, cx + arm, cy, thick * 0.45f, 255, 255, 255, 0.8f * fade);
  drawLine(pixels, size, cx, cy - arm, cx, cy - gap, thick * 0.45f, 255, 255, 255, 0.8f * fade);
  drawLine(pixels, size, cx, cy + gap, cx, cy + arm, thick * 0.45f, 255, 255, 255, 0.8f * fade);

  const float ringR = 18.0f + 10.0f * (1.0f - e);
  drawRing(pixels, size, ringR, thick * 0.9f, 255, 220, 40, 0.9f * fade);
  drawDisk(pixels, size, 5.0f, 255, 255, 255, 0.9f * fade);
}

void paintSpotlight(uint32_t *pixels, int size, float t)
{
  const float e = easeOutCubic(t);
  const float half = size * 0.5f;
  const float maxR = half - 4.0f;
  const float minR = size * 0.08f;
  const float radius = maxR + (minR - maxR) * e;
  const float fade = 1.0f - e * 0.55f;
  drawSoftDisk(pixels, size, radius, 255, 230, 60, 0.55f * fade);
  drawSoftDisk(pixels, size, radius * 0.45f, 255, 255, 220, 0.45f * fade);
  drawRing(pixels, size, radius * 0.92f, (std::max)(3.0f, size * 0.02f), 255, 240, 100, 0.5f * fade);
}

void paintPulse(uint32_t *pixels, int size, float t)
{
  // Two pulses then settle.
  const float wave = 0.5f + 0.5f * std::sin(t * 3.14159265f * 4.0f);
  const float envelope = 1.0f - easeInOut(t);
  const float half = size * 0.5f;
  const float base = half * 0.42f;
  const float radius = base * (0.72f + 0.40f * wave);
  const float thickness = (std::max)(4.0f, size * 0.032f);
  const float fade = envelope;

  const float warm = wave;
  const uint8_t r = 255;
  const uint8_t g = static_cast<uint8_t>(120 + 100 * (1.0f - warm));
  const uint8_t b = static_cast<uint8_t>(30 + 20 * (1.0f - warm));

  drawDisk(pixels, size, radius, r, g, b, 0.22f * fade);
  drawRing(pixels, size, radius, thickness, r, static_cast<uint8_t>(g + 40), b, 0.95f * fade);
  drawRing(pixels, size, radius - thickness * 0.8f, thickness * 0.5f, 255, 255, 255, 0.8f * fade);
}

void paintDot(uint32_t *pixels, int size, float t)
{
  const float blink = 0.5f + 0.5f * std::sin(t * 3.14159265f * 6.0f);
  const float envelope = 1.0f - easeOutCubic(t) * 0.85f;
  const float radius = (std::max)(10.0f, size * 0.055f) * (0.85f + 0.25f * blink);
  const float fade = envelope * (0.45f + 0.55f * blink);
  drawSoftDisk(pixels, size, radius * 2.2f, 255, 220, 40, 0.25f * fade);
  drawDisk(pixels, size, radius, 255, 230, 50, 0.95f * fade);
  drawDisk(pixels, size, radius * 0.45f, 255, 255, 255, 0.95f * fade);
  drawRing(pixels, size, radius * 1.15f, (std::max)(2.5f, size * 0.015f), 255, 255, 255, 0.7f * fade);
}

} // namespace

namespace MouseLocatorPainter {

Style parseStyle(const std::string &id)
{
  if (id == "ripple") {
    return Style::Ripple;
  }
  if (id == "crosshair") {
    return Style::Crosshair;
  }
  if (id == "spotlight") {
    return Style::Spotlight;
  }
  if (id == "pulse") {
    return Style::Pulse;
  }
  if (id == "dot") {
    return Style::Dot;
  }
  return Style::ShrinkRing;
}

const char *styleId(Style style)
{
  switch (style) {
  case Style::Ripple:
    return "ripple";
  case Style::Crosshair:
    return "crosshair";
  case Style::Spotlight:
    return "spotlight";
  case Style::Pulse:
    return "pulse";
  case Style::Dot:
    return "dot";
  case Style::ShrinkRing:
  default:
    return "shrink";
  }
}

int durationMs(Style style)
{
  switch (style) {
  case Style::Ripple:
    return 900;
  case Style::Crosshair:
    return 750;
  case Style::Spotlight:
    return 650;
  case Style::Pulse:
    return 1000;
  case Style::Dot:
    return 850;
  case Style::ShrinkRing:
  default:
    return 700;
  }
}

void clear(uint32_t *pixels, int size)
{
  std::memset(pixels, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * sizeof(uint32_t));
}

void paint(uint32_t *pixels, int size, Style style, float t)
{
  if (pixels == nullptr || size <= 0) {
    return;
  }
  clear(pixels, size);
  t = std::clamp(t, 0.0f, 1.0f);
  switch (style) {
  case Style::Ripple:
    paintRipple(pixels, size, t);
    break;
  case Style::Crosshair:
    paintCrosshair(pixels, size, t);
    break;
  case Style::Spotlight:
    paintSpotlight(pixels, size, t);
    break;
  case Style::Pulse:
    paintPulse(pixels, size, t);
    break;
  case Style::Dot:
    paintDot(pixels, size, t);
    break;
  case Style::ShrinkRing:
  default:
    paintShrink(pixels, size, t);
    break;
  }
}

} // namespace MouseLocatorPainter
