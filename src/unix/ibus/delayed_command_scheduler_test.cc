// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause

#include "unix/ibus/delayed_command_scheduler.h"

#include <cstdint>
#include <vector>

#include "protocol/commands.pb.h"
#include "testing/gunit.h"

namespace mozc::ibus {
namespace {

GObject* NewEngineForTest() { return G_OBJECT(g_object_new(G_TYPE_OBJECT, nullptr)); }

void DrainMainLoop() {
  while (g_main_context_pending(nullptr)) {
    g_main_context_iteration(nullptr, FALSE);
  }
}

// Avoid depending on wall-clock timing of the VM: only poll until one timer
// can fire, with a deadline so a broken scheduler terminates the test.
void RunUntil(size_t target, const std::vector<int>* received) {
  const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
  while (received->size() < target && g_get_monotonic_time() < deadline) {
    DrainMainLoop();
    g_usleep(1000);
  }
  DrainMainLoop();
}

commands::SessionCommand MakeCommand(uint32_t generation) {
  commands::SessionCommand command;
  command.set_type(commands::SessionCommand::APPLY_LIVE_CONVERSION);
  command.set_live_conversion_generation(generation);
  command.set_live_conversion_key("test-only-reading");
  return command;
}

TEST(DelayedCommandSchedulerTest, DeliversOriginalCommandAfterTimer) {
  std::vector<int> received;
  DelayedCommandScheduler scheduler(
      [&](GObject*, const commands::SessionCommand& command) {
        EXPECT_EQ(command.live_conversion_key(), "test-only-reading");
        received.push_back(command.live_conversion_generation());
      });
  GObject* engine = NewEngineForTest();
  scheduler.Activate(engine);
  EXPECT_TRUE(scheduler.Schedule(engine, MakeCommand(17), 1));
  RunUntil(1, &received);
  ASSERT_EQ(received.size(), 1);
  EXPECT_EQ(received.front(), 17);
  EXPECT_EQ(scheduler.pending_count(), 0);
  g_object_unref(engine);
}

TEST(DelayedCommandSchedulerTest, FocusChangeCancelsOldCallback) {
  std::vector<int> received;
  DelayedCommandScheduler scheduler(
      [&](GObject*, const commands::SessionCommand& command) {
        received.push_back(command.live_conversion_generation());
      });
  GObject* old_engine = NewEngineForTest();
  GObject* new_engine = NewEngineForTest();
  scheduler.Activate(old_engine);
  EXPECT_TRUE(scheduler.Schedule(old_engine, MakeCommand(1), 1));
  scheduler.Activate(new_engine);
  EXPECT_EQ(scheduler.pending_count(), 0);
  EXPECT_FALSE(scheduler.Schedule(old_engine, MakeCommand(2), 1));
  EXPECT_TRUE(scheduler.Schedule(new_engine, MakeCommand(3), 1));
  RunUntil(1, &received);
  ASSERT_EQ(received.size(), 1);
  EXPECT_EQ(received.front(), 3);
  g_object_unref(old_engine);
  g_object_unref(new_engine);
}

TEST(DelayedCommandSchedulerTest, DeactivationAndResetDiscardPending) {
  std::vector<int> received;
  DelayedCommandScheduler scheduler(
      [&](GObject*, const commands::SessionCommand& command) {
        received.push_back(command.live_conversion_generation());
      });
  GObject* engine = NewEngineForTest();
  scheduler.Activate(engine);
  EXPECT_TRUE(scheduler.Schedule(engine, MakeCommand(1), 500));
  scheduler.Cancel();
  EXPECT_EQ(scheduler.pending_count(), 0);
  EXPECT_TRUE(scheduler.Schedule(engine, MakeCommand(2), 1));
  scheduler.Deactivate(engine);
  EXPECT_EQ(scheduler.pending_count(), 0);
  EXPECT_FALSE(scheduler.Schedule(engine, MakeCommand(3), 1));
  scheduler.Activate(engine);
  EXPECT_TRUE(scheduler.Schedule(engine, MakeCommand(4), 1));
  RunUntil(1, &received);
  ASSERT_EQ(received.size(), 1);
  EXPECT_EQ(received.front(), 4);
  g_object_unref(engine);
}

TEST(DelayedCommandSchedulerTest, DestroyedEngineCannotReceiveCallback) {
  std::vector<int> received;
  DelayedCommandScheduler scheduler(
      [&](GObject*, const commands::SessionCommand& command) {
        received.push_back(command.live_conversion_generation());
      });
  GObject* engine = NewEngineForTest();
  scheduler.Activate(engine);
  EXPECT_TRUE(scheduler.Schedule(engine, MakeCommand(1), 1));
  g_object_unref(engine);
  RunUntil(1, &received);
  EXPECT_TRUE(received.empty());
  EXPECT_EQ(scheduler.pending_count(), 0);
}

TEST(DelayedCommandSchedulerTest, DelayedReplyMayScheduleAnotherCallback) {
  std::vector<int> received;
  GObject* engine = NewEngineForTest();
  DelayedCommandScheduler* scheduler_ptr = nullptr;
  DelayedCommandScheduler scheduler(
      [&](GObject* target, const commands::SessionCommand& command) {
        received.push_back(command.live_conversion_generation());
        if (command.live_conversion_generation() == 1) {
          EXPECT_TRUE(scheduler_ptr->Schedule(target, MakeCommand(2), 1));
        }
      });
  scheduler_ptr = &scheduler;
  scheduler.Activate(engine);
  EXPECT_TRUE(scheduler.Schedule(engine, MakeCommand(1), 1));
  RunUntil(2, &received);
  ASSERT_EQ(received.size(), 2);
  EXPECT_EQ(received[0], 1);
  EXPECT_EQ(received[1], 2);
  EXPECT_EQ(scheduler.pending_count(), 0);
  g_object_unref(engine);
}

}  // namespace
}  // namespace mozc::ibus
