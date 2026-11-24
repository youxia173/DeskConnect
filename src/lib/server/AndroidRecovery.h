// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// Owned by Server, never by the GUI or a login task. No ADB/subprocess work
// runs on the keyboard/mouse event loop. Registrations last for this server run.
class AndroidRecovery
{
public:
  ~AndroidRecovery();
  void update(const std::string &screen, const std::string &id, bool enabled);
  void forget(const std::string &screen);

private:
  void run();
  std::mutex m_mutex;
  std::condition_variable m_changed;
  std::map<std::string, std::string> m_devices;
  std::thread m_worker;
  bool m_stopping = false;
  bool m_dirty = false;
};
