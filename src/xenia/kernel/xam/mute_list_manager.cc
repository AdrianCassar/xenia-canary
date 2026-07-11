/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/mute_list_manager.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/friends_manager.h"
#include "xenia/kernel/xam/profile_manager.h"

namespace xe {
namespace kernel {
namespace xam {

MuteListManager::MuteListManager(KernelState* kernel_state,
                                 ProfileManager* profile_manager)
    : kernel_state_(kernel_state), profile_manager_(profile_manager) {}

bool MuteListManager::AddMuteListUser(const uint64_t xuid,
                                      const uint64_t remote_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(user->mute_list_mutex_);

    if (user->mute_list_.size() >= X_ONLINE_MAX_MUSTLIST) {
      return false;
    }
  }

  bool query_user = !QueryMuteListUser(xuid, remote_xuid);

  if (query_user) {
    std::lock_guard<std::mutex> lock(user->mute_list_mutex_);

    user->mute_list_.insert(remote_xuid);
    kernel_state_->BroadcastNotification(kXNotificationSystemMuteListChanged,
                                         0);
  }

  return query_user;
}

bool MuteListManager::RemoveMuteListUser(const uint64_t xuid,
                                         const uint64_t remote_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  std::lock_guard<std::mutex> lock(user->mute_list_mutex_);

  bool removed = user->mute_list_.erase(remote_xuid);

  if (removed) {
    kernel_state_->BroadcastNotification(kXNotificationSystemMuteListChanged,
                                         0);
  }

  return removed;
}

bool MuteListManager::QueryMuteListUser(const uint64_t xuid,
                                        const uint64_t remote_talker) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  return FindMuteListUser(xuid, remote_talker);
}

void MuteListManager::ResetMuteList(const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return;
  }

  std::lock_guard<std::mutex> lock(user->mute_list_mutex_);

  user->mute_list_.clear();
}

void MuteListManager::RebuildMuteList(const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return;
  }

  // TODO(Adrian):
  // Rebuild the mute list from the backend.
}

bool MuteListManager::FindMuteListUser(uint64_t xuid,
                                       const uint64_t remote_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  std::lock_guard<std::mutex> lock(user->mute_list_mutex_);

  return user->mute_list_.contains(remote_xuid);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
