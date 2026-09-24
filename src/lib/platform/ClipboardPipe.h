/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QByteArray>
#include <QElapsedTimer>

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace deskflow {

// Portal callbacks run on the same GLib loop as input-capture control. poll()
// alone cannot bound a blocking read/write: use nonblocking syscalls as well.
class ClipboardPipe
{
public:
  // These helpers borrow fd; the caller remains responsible for closing it.
  static bool read(int fd, qint64 maxBytes, QByteArray &result, int idleMs = 200, int totalMs = 2000)
  {
    result.clear();
    Nonblocking nonblocking(fd);
    if (!nonblocking.valid() || maxBytes < 0)
      return false;
    QElapsedTimer elapsed;
    elapsed.start();
    char buffer[64 * 1024];
    for (;;) {
      if (!wait(fd, POLLIN, elapsed, idleMs, totalMs))
        break;
      const auto n = ::read(fd, buffer, sizeof(buffer));
      if (n == 0)
        return true;
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
          continue;
        break;
      }
      if (n > maxBytes - result.size())
        break;
      result.append(buffer, n);
    }
    // Never publish partial text, images or file lists after a timeout/limit.
    result.clear();
    return false;
  }

  static bool write(int fd, const QByteArray &data, int idleMs = 200, int totalMs = 2000)
  {
    Nonblocking nonblocking(fd);
    if (!nonblocking.valid())
      return false;
    QElapsedTimer elapsed;
    elapsed.start();
    qsizetype written = 0;
    while (written < data.size()) {
      if (!wait(fd, POLLOUT, elapsed, idleMs, totalMs))
        return false;
      const auto n = ::write(fd, data.constData() + written, std::min<qsizetype>(64 * 1024, data.size() - written));
      if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        continue;
      if (n <= 0)
        return false;
      written += n;
    }
    return true;
  }

private:
  class Nonblocking
  {
  public:
    explicit Nonblocking(int fd) : m_fd(fd), m_flags(fcntl(fd, F_GETFL))
    {
      m_valid = m_flags >= 0 && fcntl(fd, F_SETFL, m_flags | O_NONBLOCK) == 0;
    }
    ~Nonblocking()
    {
      if (m_valid)
        fcntl(m_fd, F_SETFL, m_flags);
    }
    bool valid() const
    {
      return m_valid;
    }

  private:
    int m_fd;
    int m_flags;
    bool m_valid;
  };

  static bool wait(int fd, short events, const QElapsedTimer &elapsed, int idleMs, int totalMs)
  {
    const auto remaining = totalMs - elapsed.elapsed();
    if (remaining <= 0)
      return false;
    pollfd pfd{fd, events, 0};
    const int ready = ::poll(&pfd, 1, static_cast<int>(std::min<qint64>(idleMs, remaining)));
    if (ready < 0 && errno == EINTR)
      return elapsed.elapsed() < totalMs;
    if (ready <= 0 || (pfd.revents & (POLLERR | POLLNVAL)))
      return false;
    // Read HUP must drain pending bytes, then observe EOF. Write HUP fails.
    return (pfd.revents & events) || (events == POLLIN && (pfd.revents & POLLHUP));
  }
};
} // namespace deskflow
