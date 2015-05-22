// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_COMMAND_QUEUE_H_
#define BUFFET_COMMANDS_COMMAND_QUEUE_H_

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>

#include "buffet/commands/command_instance.h"

namespace buffet {

class CommandQueue final {
 public:
  using Callback = base::Callback<void(CommandInstance*)>;
  CommandQueue() = default;

  // Adds notifications callback for a new command is added to the queue.
  void AddOnCommandAddedCallback(const Callback& callback);

  // Adds notifications callback for a command is removed from the queue.
  void AddOnCommandRemovedCallback(const Callback& callback);

  // Checks if the command queue is empty.
  bool IsEmpty() const { return map_.empty(); }

  // Returns the number of commands in the queue.
  size_t GetCount() const { return map_.size(); }

  // Adds a new command to the queue. Each command in the queue has a unique
  // ID that identifies that command instance in this queue.
  // One shouldn't attempt to add a command with the same ID.
  void Add(std::unique_ptr<CommandInstance> instance);

  // Selects command identified by |id| ready for removal. Command will actually
  // be removed after some time.
  void DelayedRemove(const std::string& id);

  // Finds a command instance in the queue by the instance |id|. Returns
  // nullptr if the command with the given |id| is not found. The returned
  // pointer should not be persisted for a long period of time.
  CommandInstance* Find(const std::string& id) const;

 private:
  friend class CommandQueueTest;
  friend class DBusCommandDispacherTest;

  // Removes a command identified by |id| from the queue.
  bool Remove(const std::string& id);

  // Removes old commands selected with DelayedRemove.
  void Cleanup();

  // Overrides CommandQueue::Now() for tests.
  void SetNowForTest(base::Time now);

  // Returns current time.
  base::Time Now() const;

  // Overridden value to be returned from Now().
  base::Time test_now_;

  // ID-to-CommandInstance map.
  std::map<std::string, std::unique_ptr<CommandInstance>> map_;

  // Queue of commands to be removed.
  std::queue<std::pair<base::Time, std::string>> remove_queue_;

  using CallbackList = std::vector<Callback>;
  CallbackList on_command_added_;
  CallbackList on_command_removed_;

  DISALLOW_COPY_AND_ASSIGN(CommandQueue);
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_COMMAND_QUEUE_H_
