// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef MOZC_UNIX_IBUS_DELAYED_COMMAND_SCHEDULER_H_
#define MOZC_UNIX_IBUS_DELAYED_COMMAND_SCHEDULER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

#include "protocol/commands.pb.h"
#include "unix/ibus/ibus_header.h"

namespace mozc::ibus {

// Executes server-requested delayed commands on the IBus main context.
// All methods, including the callback, must run on the GLib main thread.
// The target engine is weakly referenced; a timer cannot keep an IBusEngine
// alive or deliver a reply after a focus/engine transition.
class DelayedCommandScheduler {
 public:
  using CommandHandler =
      std::function<void(GObject*, const commands::SessionCommand&)>;

  explicit DelayedCommandScheduler(CommandHandler handler);
  DelayedCommandScheduler(const DelayedCommandScheduler&) = delete;
  DelayedCommandScheduler& operator=(const DelayedCommandScheduler&) = delete;
  ~DelayedCommandScheduler();

  // Activate an engine. Changing the engine invalidates all pending callbacks.
  void Activate(GObject* engine);
  // Invalidate pending callbacks only if this is the active engine.
  void Deactivate(GObject* engine);
  // Invalidate pending callbacks without changing the active engine.
  void Cancel();

  bool Schedule(GObject* engine, const commands::SessionCommand& command,
                uint32_t delay_millisec);

  size_t pending_count() const { return pending_.size(); }

 private:
  struct PendingCallback {
    DelayedCommandScheduler* owner;
    GWeakRef engine;
    commands::SessionCommand command;
    guint source_id = 0;
    uint64_t generation = 0;
  };

  static gboolean OnTimeout(gpointer data);
  static void DestroyPending(gpointer data);

  CommandHandler handler_;
  GWeakRef active_engine_;
  uint64_t generation_ = 0;
  std::map<guint, PendingCallback*> pending_;
};

}  // namespace mozc::ibus

#endif  // MOZC_UNIX_IBUS_DELAYED_COMMAND_SCHEDULER_H_
