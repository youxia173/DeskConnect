/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/IClipboard.h"

#include "base/Log.h"

#include <assert.h>
#include <vector>

//
// IClipboard
//

void IClipboard::unmarshall(IClipboard *clipboard, const std::string_view &data, Time time)
{
  assert(clipboard != nullptr);

  const char *index = data.data();
  const char *const end = index + data.size();

  if (clipboard->open(time)) {
    // clear existing data
    clipboard->empty();

    if (end - index < 4) {
      LOG_ERR("clipboard unmarshall: truncated header");
      clipboard->close();
      return;
    }
    // read the number of formats
    const uint32_t numFormats = readUInt32(index);
    index += 4;

    // Apps like QQ put image/png plus HTML with local <img> paths. Prefer the
    // bitmap so paste targets get a real image instead of broken HTML.
    bool hasBitmap = false;
    {
      const char *scan = index;
      for (uint32_t i = 0; i < numFormats; ++i) {
        if (end - scan < 8) {
          break;
        }
        auto format = static_cast<IClipboard::Format>(readUInt32(scan));
        scan += 4;
        uint32_t size = readUInt32(scan);
        scan += 4;
        if (size > static_cast<uint32_t>(end - scan)) {
          break;
        }
        if (format == IClipboard::Format::Bitmap && size > 0) {
          hasBitmap = true;
          break;
        }
        scan += size;
      }
    }

    // read each format
    for (uint32_t i = 0; i < numFormats; ++i) {
      // need 8 bytes for format id + payload size
      if (end - index < 8) {
        LOG_ERR("clipboard unmarshall: truncated format header at %u/%u", i, numFormats);
        break;
      }
      // get the format id
      auto format = static_cast<IClipboard::Format>(readUInt32(index));
      index += 4;

      // get the size of the format data
      uint32_t size = readUInt32(index);
      index += 4;

      // peer-supplied size must not exceed remaining buffer
      if (size > static_cast<uint32_t>(end - index)) {
        LOG_ERR("clipboard unmarshall: payload size %u exceeds remaining %zd", size, end - index);
        break;
      }

      // save the data if it's a known format.  if either the client
      // or server supports more clipboard formats than the other
      // then one of them will get a format >= TotalFormats here.
      if (format < IClipboard::Format::TotalFormats) {
        if (hasBitmap && format == IClipboard::Format::HTML) {
          LOG_DEBUG("skipping HTML clipboard on unmarshall; bitmap present");
        } else {
          clipboard->add(format, std::string(index, size));
        }
      }
      index += size;
    }

    // done
    clipboard->close();
  }
}

std::string IClipboard::marshall(const IClipboard *clipboard)
{
  // return data format:
  // 4 bytes => number of formats included
  // 4 bytes => format enum
  // 4 bytes => clipboard data size n
  // n bytes => clipboard data
  // back to the second 4 bytes if there is another format

  assert(clipboard != nullptr);

  std::string data;
  static const auto totalClipboardFormats = static_cast<int>(Format::TotalFormats);
  std::vector<std::string> formatData;
  formatData.resize(totalClipboardFormats);
  // FIXME -- use current time
  if (clipboard->open(0)) {

    const bool hasBitmap =
        clipboard->has(Format::Bitmap) && !clipboard->get(Format::Bitmap).empty();

    // compute size of marshalled data
    uint32_t size = 4;
    uint32_t numFormats = 0;
    for (uint32_t format = 0; format != totalClipboardFormats; ++format) {
      auto eFormat = static_cast<IClipboard::Format>(format);
      // Files are transferred via DFTR/DDRG, not the clipboard channel.
      if (eFormat == IClipboard::Format::Files) {
        continue;
      }
      // Drop HTML when a bitmap is present (e.g. QQ chat image copy).
      if (hasBitmap && eFormat == IClipboard::Format::HTML) {
        LOG_DEBUG("skipping HTML clipboard on marshall; bitmap present");
        continue;
      }
      if (clipboard->has(eFormat) && !clipboard->get(eFormat).empty()) {
        ++numFormats;
        formatData[format] = clipboard->get(eFormat);
        size += 4 + 4 + (uint32_t)formatData[format].size();
      }
    }

    // allocate space
    data.reserve(size);

    // marshall the data
    writeUInt32(&data, numFormats);
    for (uint32_t format = 0; format != totalClipboardFormats; ++format) {
      auto eFormat = static_cast<IClipboard::Format>(format);
      if (eFormat == IClipboard::Format::Files) {
        continue;
      }
      if (hasBitmap && eFormat == IClipboard::Format::HTML) {
        continue;
      }
      if (clipboard->has(eFormat) && !formatData[format].empty()) {
        writeUInt32(&data, format);
        writeUInt32(&data, (uint32_t)formatData[format].size());
        data += formatData[format];
      }
    }
    clipboard->close();
  }

  return data;
}

bool IClipboard::copy(IClipboard *dst, const IClipboard *src)
{
  assert(dst != nullptr);
  assert(src != nullptr);

  return copy(dst, src, src->getTime());
}

bool IClipboard::copy(IClipboard *dst, const IClipboard *src, Time time)
{
  assert(dst != nullptr);
  assert(src != nullptr);

  bool success = false;
  if (src->open(time)) {
    if (dst->open(time)) {
      if (dst->empty()) {
        const bool hasBitmap = src->has(Format::Bitmap) && !src->get(Format::Bitmap).empty();
        for (int32_t format = 0; format != static_cast<int>(Format::TotalFormats); ++format) {
          auto eFormat = (IClipboard::Format)format;
          if (hasBitmap && eFormat == Format::HTML) {
            LOG_DEBUG("skipping HTML clipboard on copy; bitmap present");
            continue;
          }
          if (src->has(eFormat) && !src->get(eFormat).empty()) {
            dst->add(eFormat, src->get(eFormat));
          }
        }
        success = true;
      }
      dst->close();
    }
    src->close();
  }

  return success;
}

uint32_t IClipboard::readUInt32(const char *buf)
{
  const auto *ubuf = reinterpret_cast<const unsigned char *>(buf);
  return (static_cast<uint32_t>(ubuf[0]) << 24) | (static_cast<uint32_t>(ubuf[1]) << 16) |
         (static_cast<uint32_t>(ubuf[2]) << 8) | static_cast<uint32_t>(ubuf[3]);
}

void IClipboard::writeUInt32(std::string *buf, uint32_t v)
{
  *buf += static_cast<uint8_t>((v >> 24) & 0xff);
  *buf += static_cast<uint8_t>((v >> 16) & 0xff);
  *buf += static_cast<uint8_t>((v >> 8) & 0xff);
  *buf += static_cast<uint8_t>(v & 0xff);
}
