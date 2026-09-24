/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EventQueueTests.h"

#include "base/EventQueue.h"
#include "base/IEventQueueBuffer.h"

#include <QTest>

#include <memory>

void EventQueueTests::initTestCase()
{
  m_arch.init();
}

void EventQueueTests::dispatchEvent_noHandler_returnsFalse()
{
  EventQueue events;

  QVERIFY(!events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
}

void EventQueueTests::dispatchEvent_noTypeHandler_dispatchesUnknownHandler()
{
  EventQueue events;
  bool fallbackCalled = false;
  events.addHandler(EventTypes::Unknown, this, [&fallbackCalled](const Event &) { fallbackCalled = true; });

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(fallbackCalled);
}

void EventQueueTests::dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn()
{
  EventQueue events;
  auto handlerLifetime = std::make_shared<int>(1);
  std::weak_ptr<int> handlerLifetimeObserver = handlerLifetime;
  bool handlerAliveAfterRemoval = false;

  events.addHandler(
      EventTypes::ClientDisconnected, this,
      [this, &events, &handlerLifetimeObserver, &handlerAliveAfterRemoval, handlerLifetime](const Event &) {
        events.removeHandler(EventTypes::ClientDisconnected, this);
        handlerAliveAfterRemoval = handlerLifetime != nullptr && !handlerLifetimeObserver.expired();
      }
  );
  handlerLifetime.reset();

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(handlerAliveAfterRemoval);
  QVERIFY(handlerLifetimeObserver.expired());
}

void EventQueueTests::getEvent_busyBufferDoesNotStarveTimersOrInput()
{
  class BusyBuffer : public IEventQueueBuffer
  {
  public:
    void init() override
    {
    }
    void waitForEvent(double) override
    {
    }
    bool isEmpty() const override
    {
      return false;
    }
    bool addEvent(uint32_t) override
    {
      return true;
    }
    Type getEvent(Event &event, uint32_t &) override
    {
      event = Event(EventTypes::System);
      return Type::System;
    }
  };
  EventQueue events;
  events.adoptBuffer(new BusyBuffer);
  auto *timer = events.newTimer(0.001, nullptr);
  QTest::qSleep(10);
  Event event;
  const bool gotTimer = events.getEvent(event, 0);
  const auto firstType = event.getType();
  QTest::qSleep(10);
  const bool gotInput = events.getEvent(event, 0);
  const auto secondType = event.getType();
  events.deleteTimer(timer);
  QVERIFY(gotTimer);
  QCOMPARE(firstType, EventTypes::Timer);
  QVERIFY(gotInput);
  QCOMPARE(secondType, EventTypes::System);
}

QTEST_MAIN(EventQueueTests)
