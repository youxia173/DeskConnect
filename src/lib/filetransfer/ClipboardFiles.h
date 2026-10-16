/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class IEventQueue;
class IPlatformScreen;
class EventQueueTimer;

namespace deskflow {

// DFOF is a versioned control channel. No file contents or recursive directory
// scan is needed until the destination asks for metadata. Only a separate
// content request starts a transfer for the current offer token.
class ClipboardFiles
{
public:
  using SendControl = std::function<void(const std::string &)>;
  using SendFiles = std::function<bool(const std::vector<std::string> &)>;
  ClipboardFiles(IEventQueue *events, IPlatformScreen *screen, SendControl control, SendFiles files,
                 std::function<void()> cancelSend = {}, std::function<void()> cancelReceive = {});
  ~ClipboardFiles();
  void announce(const std::string &paths, bool force = false);
  void handle(const std::string &message);
  void received(const std::vector<std::string> &paths);
  void failed();
  void cancel();
  void sendFinished(bool success);
  void progress();
  bool waiting() const;

private:
  struct Pull;
  struct Listing;
  void pollListing();
  void clearListing();
  std::shared_ptr<Listing> m_listing;
  EventQueueTimer *m_listingTimer = nullptr;
  static std::vector<std::string> wait(IEventQueue *events, const std::shared_ptr<Pull> &pull);
  void control(const char *operation, const std::string &id);
  IEventQueue *m_events;
  IPlatformScreen *m_screen;
  SendControl m_control;
  SendFiles m_files;
  std::function<void()> m_cancelSend;
  std::function<void()> m_cancelReceive;
  std::string m_localPaths;
  std::string m_localId;
  std::string m_sendingId;
  std::shared_ptr<Pull> m_pull;
};
} // namespace deskflow
