// SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <QByteArray>
#include <QList>

namespace deskflow::gui::bluetooth {

// RFCOMM is a byte stream. Versioned frames carry opaque TLS bytes and a
// round-trip watchdog; no DeskConnect input packets are interpreted here.
class Frames
{
public:
  enum Type : quint8
  {
    Data = 1,
    Ping = 2,
    Pong = 3
  };
  struct Frame
  {
    Type type;
    QByteArray payload;
  };
  static constexpr qsizetype MaxPayload = 4096;
  static constexpr qsizetype MaxBuffered = 65536;
  static constexpr qsizetype HeaderSize = 7;

  static QByteArray encode(Type type, const QByteArray &payload)
  {
    if (!valid(type, payload.size()))
      return {};
    QByteArray out("DCB\1", 4);
    out.append(char(type));
    out.append(char(payload.size() >> 8));
    out.append(char(payload.size() & 255));
    return out + payload;
  }

  bool feed(const QByteArray &bytes, QList<Frame> &frames)
  {
    if (m_failed || bytes.size() > MaxBuffered - m_buffer.size())
      return fail();
    m_buffer += bytes;
    while (m_buffer.size() >= HeaderSize) {
      const auto type = Type(quint8(m_buffer[4]));
      const auto size = (quint8(m_buffer[5]) << 8) | quint8(m_buffer[6]);
      if (!m_buffer.startsWith(QByteArray("DCB\1", 4)) || !valid(type, size))
        return fail();
      if (m_buffer.size() < HeaderSize + size)
        break;
      frames.append({type, m_buffer.mid(HeaderSize, size)});
      m_buffer.remove(0, HeaderSize + size);
    }
    return true;
  }

private:
  static bool valid(Type type, qsizetype size)
  {
    return (type == Data && size > 0 && size <= MaxPayload) || ((type == Ping || type == Pong) && size == 4);
  }
  bool fail()
  {
    m_buffer.clear();
    m_failed = true;
    return false;
  }
  QByteArray m_buffer;
  bool m_failed = false;
};
} // namespace deskflow::gui::bluetooth
