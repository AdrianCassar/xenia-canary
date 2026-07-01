/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_FRIENDS_MANAGER_H_
#define XENIA_KERNEL_XAM_FRIENDS_MANAGER_H_

#include <optional>
#include <set>

#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {
class ProfileManager;
}  // namespace xam
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

class FriendsManager {
 public:
  FriendsManager(KernelState* kernel_state, ProfileManager* profile_manager);

  ~FriendsManager() = default;

  void AddFriends(const uint64_t xuid, const std::set<uint64_t>& xuids) const;

  bool AddFriend(const uint64_t xuid, const uint64_t friend_xuid) const;
  bool AddFriend(const uint64_t xuid, const X_ONLINE_FRIEND& peer) const;

  bool UpdateFriend(const uint64_t xuid,
                    const X_ONLINE_FRIEND& update_peer) const;

  bool RemoveFriend(const uint64_t xuid, const uint64_t friend_xuid) const;

  bool IsFriend(const uint64_t xuid, const uint64_t friend_xuid) const;

  void ClearFriends(const uint64_t xuid) const;

  std::optional<X_ONLINE_FRIEND> GetFriendFromIndex(const uint64_t xuid,
                                                    const uint32_t index) const;
  std::optional<X_ONLINE_FRIEND> GetFriend(const uint64_t xuid,
                                           const uint64_t friend_xuid) const;

  std::optional<std::reference_wrapper<const std::vector<X_ONLINE_FRIEND>>>
  GetFriends(const uint64_t xuid) const;

  std::set<uint64_t> GetFriendsXUIDs(const uint64_t xuid) const;

  size_t GetFriendsCount(const uint64_t xuid) const;

  std::optional<X_ONLINE_PRESENCE> GetFriendPresence(
      const uint64_t xuid, const uint64_t friend_xuid) const;

  void AddDummyFriends(const uint64_t xuid, const uint32_t friends_count) const;

 private:
  std::vector<X_ONLINE_FRIEND>::iterator FindFriend(
      std::vector<X_ONLINE_FRIEND>& friends, const uint64_t friend_xuid) const;

  X_ONLINE_FRIEND GenerateDummyFriend() const;

  KernelState* kernel_state_;
  ProfileManager* profile_manager_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_FRIENDS_MANAGER_H_
