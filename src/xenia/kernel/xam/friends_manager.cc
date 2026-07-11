/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/friends_manager.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/friends_util.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/user_profile.h"

namespace xe {
namespace kernel {
namespace xam {

FriendsManager::FriendsManager(KernelState* kernel_state,
                               ProfileManager* profile_manager)
    : kernel_state_(kernel_state),
      profile_manager_(profile_manager),
      mute_list_manager_(kernel_state_, profile_manager_) {}

void FriendsManager::AddFriends(const uint64_t xuid,
                                const std::set<uint64_t>& xuids) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return;
  }

  for (const auto& friend_xuid : xuids) {
    AddFriend(xuid, friend_xuid, false);
  }
}

bool FriendsManager::AddFriend(const uint64_t xuid, const uint64_t friend_xuid,
                               bool notify) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(user->friends_mutex_);
    if (user->friends_.size() >= X_ONLINE_MAX_FRIENDS) {
      return false;
    }
  }

  if (!IsOnlineXUID(friend_xuid)) {
    return false;
  }

  if (user->GetOnlineXUID() == friend_xuid) {
    return false;
  }

  if (IsFriend(xuid, friend_xuid)) {
    return false;
  }

  X_ONLINE_FRIEND peer = {.xuid = friend_xuid};

  const std::string default_gamertag =
      fmt::format("Friend {}", user->friends_.size() + 1);

  xe::string_util::copy_truncating(peer.Gamertag, default_gamertag.c_str(),
                                   xe::countof(peer.Gamertag));

  {
    std::lock_guard<std::mutex> lock(user->friends_mutex_);
    user->friends_.push_back(peer);
  }

  // Check if we're adding or loading existing friend.
  // Skip saving dummy friends.
  if (!ParseFriendsXUIDs().contains(friend_xuid) &&
      !user->dummy_friend_xuids_.contains(friend_xuid)) {
    AddFriendToConfig(friend_xuid);
  }

  if (notify) {
    kernel_state_->BroadcastNotification(
        kXNotificationFriendsFriendAdded,
        profile_manager_->GetUserIndexAssignedToProfile(user->xuid()));
  }

  return true;
}

bool FriendsManager::AddFriend(const uint64_t xuid,
                               const X_ONLINE_FRIEND& peer) const {
  // Technically we should broadcast added friend be after update.
  return AddFriend(xuid, peer.xuid) && UpdateFriend(xuid, peer);
}

bool FriendsManager::UpdateFriend(const uint64_t xuid,
                                  const X_ONLINE_FRIEND& update_friend) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  auto it = FindFriend(user->friends_, update_friend.xuid);

  if (it == user->friends_.end()) {
    return false;
  }

  *it = update_friend;

  return true;
}

bool FriendsManager::RemoveFriend(const uint64_t xuid,
                                  const uint64_t friend_xuid,
                                  bool notify) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  const auto it = FindFriend(user->friends_, friend_xuid);

  if (it == user->friends_.end()) {
    return false;
  }

  user->friends_.erase(it);

  // Skip erasing from user->dummy_friend_xuids_ so dummy friend cannot be added
  // to config.

  RemoveFriendFromConfig(friend_xuid);

  if (notify) {
    kernel_state_->BroadcastNotification(
        kXNotificationFriendsFriendRemoved,
        profile_manager_->GetUserIndexAssignedToProfile(user->xuid()));
  }

  return true;
}

bool FriendsManager::IsFriend(const uint64_t xuid,
                              const uint64_t friend_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  return FindFriend(user->friends_, friend_xuid) != user->friends_.end();
}

void FriendsManager::ClearFriends(const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return;
  }

  for (const auto& friend_xuid : GetFriendsXUIDs(xuid)) {
    RemoveFriend(xuid, friend_xuid, false);
  }

  kernel_state_->BroadcastNotification(
      kXNotificationFriendsFriendRemoved,
      profile_manager_->GetUserIndexAssignedToProfile(user->xuid()));
}

std::optional<X_ONLINE_FRIEND> FriendsManager::GetFriendFromIndex(
    const uint64_t xuid, const uint32_t index) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  if (index >= X_ONLINE_MAX_FRIENDS || index >= user->friends_.size()) {
    return std::nullopt;
  }

  return user->friends_[index];
}

std::optional<X_ONLINE_FRIEND> FriendsManager::GetFriend(
    const uint64_t xuid, const uint64_t friend_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  const auto it = FindFriend(user->friends_, friend_xuid);

  if (it == user->friends_.end()) {
    return std::nullopt;
  }

  return *it;
}

std::optional<std::vector<X_ONLINE_FRIEND>> FriendsManager::GetFriends(
    const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  // Copy vector so it's thread safe, although it's less efficient.
  return user->friends_;
}

std::set<uint64_t> FriendsManager::GetFriendsXUIDs(const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {};
  }

  std::set<uint64_t> xuids;

  for (const auto& peer : user->friends_) {
    std::lock_guard<std::mutex> lock(user->friends_mutex_);
    xuids.insert(peer.xuid);
  }

  return xuids;
}

size_t FriendsManager::GetFriendsCount(const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(user->friends_mutex_);

  return user->friends_.size();
}

std::vector<X_ONLINE_FRIEND>::iterator FriendsManager::FindFriend(
    std::vector<X_ONLINE_FRIEND>& friends, const uint64_t friend_xuid) const {
  return std::find_if(friends.begin(), friends.end(),
                      [&friend_xuid](const X_ONLINE_FRIEND& peer) {
                        return peer.xuid == friend_xuid;
                      });
}

bool FriendsManager::IsPresenceOutOfSync(
    uint64_t xuid, std::vector<FriendPresenceObjectJSON> friends) const {
  if (friends.empty()) {
    return false;
  }

  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  bool sync_state = false;

  for (const auto& player : friends) {
    const uint64_t friend_xuid = player.XUID();

    if (!IsFriend(xuid, friend_xuid)) {
      XELOGI("Requested unknown peer presence: {} - {:016X}", player.Gamertag(),
             friend_xuid);
      continue;
    }

    if (sync_state) {
      break;
    }

    const auto friend_ = GetFriend(xuid, friend_xuid);

    if (friend_.has_value()) {
      const X_ONLINE_FRIEND peer = friend_.value();
      const X_ONLINE_FRIEND updated_peer_presence = player.GetFriendPresence();

      sync_state =
          std::memcmp(&peer, &updated_peer_presence, sizeof(X_ONLINE_FRIEND));
    }
  }

  return sync_state;
}

// Convert X_ONLINE_FRIEND to X_ONLINE_PRESENCE
std::optional<X_ONLINE_PRESENCE> FriendsManager::GetFriendPresence(
    const uint64_t xuid, const uint64_t friend_xuid) const {
  const auto peer = GetFriend(xuid, friend_xuid);

  if (!peer.has_value()) {
    return std::nullopt;
  }

  X_ONLINE_PRESENCE presence = {};

  presence.title_id = peer->title_id;
  presence.state = peer->state;
  presence.xuid = peer->xuid;
  presence.session_id = peer->session_id;

  const std::u16string rich_presence(
      reinterpret_cast<const char16_t*>(peer->wszRichPresence));
  char16_t* rich_presence_ptr =
      reinterpret_cast<char16_t*>(presence.wszRichPresence);

  presence.cchRichPresence = rich_presence.size();

  xe::string_util::copy_truncating(rich_presence_ptr, rich_presence,
                                   X_MAX_RICHPRESENCE_SIZE);

  return presence;
}

X_ONLINE_FRIEND FriendsManager::GenerateDummyFriend() const {
  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<int> dist(0x00, 0xFF);

  X_ONLINE_FRIEND dummy_friend = {};

  // Friend is playing same title
  dummy_friend.title_id = kernel_state_->title_id();

  const uint32_t player_state = X_ONLINE_FRIENDSTATE_FLAG_ONLINE |
                                X_ONLINE_FRIENDSTATE_FLAG_JOINABLE |
                                X_ONLINE_FRIENDSTATE_FLAG_PLAYING;

  const uint32_t user_state = X_ONLINE_FRIENDSTATE_ENUM_ONLINE;

  dummy_friend.xuid = profile_manager_->GenerateXuidOnline();
  dummy_friend.session_id = XNKID();
  dummy_friend.state = player_state | user_state;

  xe::be<uint64_t> session_id = 0xAE00FFFFFFFFFFFF;
  std::memcpy(dummy_friend.session_id.ab, &session_id, sizeof(XNKID));

  // uint64_t xnkidInvite = 0xAE00FFFFFFFFFFFF;
  // std::memcpy(dummy_friend.xnkidInvite.ab, &xnkidInvite, sizeof(XNKID));

  std::string gamertag = fmt::format("Player {}", dist(gen));
  std::u16string rich_presence = u"Playing on Xenia";

  xe::string_util::copy_truncating(dummy_friend.Gamertag, gamertag.c_str(),
                                   xe::countof(dummy_friend.Gamertag));

  dummy_friend.cchRichPresence = rich_presence.size();

  char16_t* rich_presence_ptr =
      reinterpret_cast<char16_t*>(dummy_friend.wszRichPresence);
  xe::string_util::copy_and_swap_truncating(rich_presence_ptr, rich_presence,
                                            X_MAX_RICHPRESENCE_SIZE);

  return dummy_friend;
}

void FriendsManager::AddDummyFriends(const uint64_t xuid,
                                     const uint32_t friends_count) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(user->friends_mutex_);
    if (user->friends_.size() >= X_ONLINE_MAX_FRIENDS) {
      return;
    }
  }

  for (uint32_t i = 0; i < friends_count; i++) {
    const auto dummy = GenerateDummyFriend();
    user->dummy_friend_xuids_.insert(dummy.xuid.get());

    AddFriend(xuid, dummy);
  }
}

bool FriendsManager::AddMuteListUser(const uint64_t xuid,
                                     const uint64_t remote_xuid) const {
  return mute_list_manager_.AddMuteListUser(xuid, remote_xuid);
}

bool FriendsManager::RemoveMuteListUser(const uint64_t xuid,
                                        const uint64_t remote_xuid) const {
  return mute_list_manager_.RemoveMuteListUser(xuid, remote_xuid);
}

bool FriendsManager::QueryMuteListUser(const uint64_t xuid,
                                       const uint64_t remote_talker) const {
  return mute_list_manager_.QueryMuteListUser(xuid, remote_talker);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
