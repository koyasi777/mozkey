// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause

#include "unix/ibus/delayed_command_scheduler.h"

#include <cstdint>
#include <utility>

namespace mozc::ibus {
namespace {

// Bound both timers and retained callback commands during fast typing.
constexpr size_t kMaxPendingCommands = 512;

}  // namespace

DelayedCommandScheduler::DelayedCommandScheduler(CommandHandler handler)
    : handler_(std::move(handler)) {
  g_weak_ref_init(&active_engine_, nullptr);
}

DelayedCommandScheduler::~DelayedCommandScheduler() {
  Cancel();
  g_weak_ref_clear(&active_engine_);
}

void DelayedCommandScheduler::Activate(GObject* engine) {
  GObject* active = G_OBJECT(g_weak_ref_get(&active_engine_));
  const bool changed = active != engine;
  if (active != nullptr) {
    g_object_unref(active);
  }
  if (changed) {
    Cancel();
    g_weak_ref_set(&active_engine_, engine);
  }
}

void DelayedCommandScheduler::Deactivate(GObject* engine) {
  GObject* active = G_OBJECT(g_weak_ref_get(&active_engine_));
  const bool matches = active != nullptr && active == engine;
  if (active != nullptr) {
    g_object_unref(active);
  }
  if (matches) {
    Cancel();
    g_weak_ref_set(&active_engine_, nullptr);
  }
}

void DelayedCommandScheduler::Cancel() {
  ++generation_;
  // g_source_remove invokes DestroyPending on each removed source.
  for (const auto& [id, pending] : pending_) {
    g_source_remove(id);
  }
  pending_.clear();
}

bool DelayedCommandScheduler::Schedule(
    GObject* engine, const commands::SessionCommand& command,
    uint32_t delay_millisec) {
  if (engine == nullptr || delay_millisec == 0) {
    return false;
  }
  GObject* active = G_OBJECT(g_weak_ref_get(&active_engine_));
  const bool matches = active == engine;
  if (active != nullptr) {
    g_object_unref(active);
  }
  if (!matches) {
    return false;
  }

  // GLib timer IDs increase during ordinary operation. Evict the earliest
  // registered source when the client falls behind, as in the Windows TIP.
  while (pending_.size() >= kMaxPendingCommands) {
    const guint oldest_id = pending_.begin()->first;
    pending_.erase(oldest_id);
    g_source_remove(oldest_id);
  }

  auto* pending = new PendingCallback;
  pending->owner = this;
  pending->command = command;
  pending->generation = generation_;
  g_weak_ref_init(&pending->engine, engine);
  const guint source_id = g_timeout_add_full(
      G_PRIORITY_DEFAULT, delay_millisec, &OnTimeout, pending, &DestroyPending);
  if (source_id == 0) {
    DestroyPending(pending);
    return false;
  }
  pending->source_id = source_id;
  pending_[source_id] = pending;
  return true;
}

gboolean DelayedCommandScheduler::OnTimeout(gpointer data) {
  auto* pending = static_cast<PendingCallback*>(data);
  DelayedCommandScheduler* owner = pending->owner;
  // Remove this timer before invoking the handler: a server reply may schedule
  // another delayed callback, or a focus change may cancel every other timer.
  if (owner->pending_.erase(pending->source_id) == 0 ||
      pending->generation != owner->generation_) {
    return G_SOURCE_REMOVE;
  }

  GObject* target = G_OBJECT(g_weak_ref_get(&pending->engine));
  GObject* active = G_OBJECT(g_weak_ref_get(&owner->active_engine_));
  if (target != nullptr && target == active) {
    owner->handler_(target, pending->command);
  }
  if (active != nullptr) {
    g_object_unref(active);
  }
  if (target != nullptr) {
    g_object_unref(target);
  }
  return G_SOURCE_REMOVE;
}

void DelayedCommandScheduler::DestroyPending(gpointer data) {
  auto* pending = static_cast<PendingCallback*>(data);
  g_weak_ref_clear(&pending->engine);
  delete pending;
}

}  // namespace mozc::ibus
