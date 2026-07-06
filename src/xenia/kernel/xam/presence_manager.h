/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_PRECENSE_MANAGER_H_
#define XENIA_KERNEL_XAM_PRECENSE_MANAGER_H_

#include <future>
#include <set>

#include "xenia/kernel/json/friend_presence_object_json.h"
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
class FriendsManager;
}  // namespace xam
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

struct PresenceSyncState {
  bool friends;
  bool subscribers;

  bool IsOutOfSync() const { return friends || subscribers; }
};

class PresenceSynced {
 public:
  PresenceSynced(PresenceSyncState sync_state,
                 std::vector<FriendPresenceObjectJSON> friends,
                 std::vector<FriendPresenceObjectJSON> subscribers)
      : sync_state_(sync_state),
        friends_(friends),
        subscribers_(subscribers) {};

  const PresenceSyncState GetSyncState() const { return sync_state_; }

  const std::vector<FriendPresenceObjectJSON>& GetFriendsView() const {
    return friends_;
  }

  const std::vector<FriendPresenceObjectJSON>& GetSubscribersView() const {
    return subscribers_;
  }

 private:
  PresenceSyncState sync_state_;
  std::vector<FriendPresenceObjectJSON> friends_;
  std::vector<FriendPresenceObjectJSON> subscribers_;
};

class PresenceManager {
 public:
  PresenceManager(KernelState* kernel_state, ProfileManager* profile_manager,
                  FriendsManager* friends_manager);

  ~PresenceManager() = default;

  bool IsInitialized() const;

  std::u16string GetPresenceString(const uint64_t xuid) const;

  void UpdateXboxLiveLocalUsersPresence(const std::set<uint64_t>& xuids) const;

  std::unique_ptr<FriendsPresenceObjectJSON> GetFriendsPresence(
      const uint64_t xuid, const std::set<uint64_t>& xuids) const;

  bool IsPresenceStringUpdateAvailable(const uint64_t xuid) const;

  bool UpdatePresence(const uint64_t xuid) const;

  bool UpdateLocalPresence(const uint64_t xuid) const;

  bool UpdateSubscription(const uint64_t xuid,
                          const X_ONLINE_PRESENCE& peer) const;

  std::optional<X_ONLINE_PRESENCE> GetSubscription(
      const uint64_t xuid, const uint64_t subscriber_xuid) const;

  void Initialize(const uint32_t max_subscriptions);

  bool Subscribe(const uint64_t xuid, const uint64_t subscriber_xuid) const;

  bool Unsubscribe(const uint64_t xuid, const uint64_t subscriber_xuid) const;

  bool IsSubscribed(const uint64_t xuid, const uint64_t subscriber_xuid) const;

  std::set<uint64_t> GetSubscribedXUIDs(const uint64_t xuid) const;

  uint32_t GetMaxPeerSubscriptions() const;

  uint32_t GetSubscribedPeersTotal() const;

  bool IsPresenceOutOfSync(
      uint64_t xuid, std::vector<FriendPresenceObjectJSON> subscribers) const;

  std::unique_ptr<FriendsPresenceObjectJSON> GetLivePresenceFriends(
      const uint64_t xuid) const;

  std::unique_ptr<FriendsPresenceObjectJSON> GetLivePresenceSubscribers(
      const uint64_t xuid) const;

  std::unique_ptr<FriendsPresenceObjectJSON> GetLivePresence(
      const uint64_t xuid) const;

  PresenceSynced GetPresenceSyncState(uint64_t xuid) const;

  std::future<void> SyncPresenceAsync(uint64_t xuid) const;

  // Sync XFriendsCreateEnumerator & XPresenceCreateEnumerator
  void SyncPresence(uint64_t xuid) const;

  std::vector<X_ONLINE_FRIEND> GetFriendsPresenceSorted(uint64_t xuid) const;

 private:
  bool BuildPresenceString(const uint64_t xuid, bool update,
                           std::u16string* presence_string = nullptr) const;

  bool initialized_ = false;
  uint32_t max_subscriptions_ = 0;

  const uint32_t kMaxUserSubscriptions =
      X_ONLINE_PEER_SUBSCRIPTIONS / XUserMaxUserCount;

  KernelState* kernel_state_;
  ProfileManager* profile_manager_;
  FriendsManager* friends_manager_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_PRECENSE_MANAGER_H_
