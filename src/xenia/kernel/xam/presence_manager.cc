/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <ranges>

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/util/presence_string_builder.h"
#include "xenia/kernel/xam/friends_manager.h"
#include "xenia/kernel/xam/presence_manager.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/user_profile.h"

namespace xe {
namespace kernel {
namespace xam {

// TODO(Adrian): How should we differentiate the usage between X_ONLINE_FRIEND
// and X_ONLINE_PRESENCE?

PresenceManager::PresenceManager(KernelState* kernel_state,
                                 ProfileManager* profile_manager,
                                 FriendsManager* friends_manager)
    : kernel_state_(kernel_state),
      profile_manager_(profile_manager),
      friends_manager_(friends_manager) {}

bool PresenceManager::IsInitialized() const { return initialized_; }

std::u16string PresenceManager::GetPresenceString(const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {};
  }

  return user->online_presence_desc_;
}

std::unique_ptr<FriendsPresenceObjectJSON> PresenceManager::GetFriendsPresence(
    const uint64_t xuid, const std::set<uint64_t>& xuids) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {};
  }

  for (const auto& friend_xuid : xuids) {
    const bool is_friend = friends_manager_->IsFriend(xuid, friend_xuid);
    const bool is_subscribed = IsSubscribed(xuid, friend_xuid);

    if (!is_friend && !is_subscribed) {
      XELOGI(
          "{}: Trying to retrieve presence for non-friend and unsubscribed "
          "peer",
          __func__);
    }
  }

  return kernel_state_->GetXboxLiveAPI()->GetFriendsPresence(xuids);
}

bool PresenceManager::IsPresenceStringUpdateAvailable(
    const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  const std::u16string current_presence = user->online_presence_desc_;
  std::u16string updated_presence = u"";

  if (!BuildPresenceString(xuid, false, &updated_presence)) {
    return false;
  }

  return current_presence != updated_presence;
}

void PresenceManager::UpdateXboxLiveLocalUsersPresence(
    const std::set<uint64_t>& xuids) const {
  kernel_state_->GetXboxLiveAPI()->SetPresence(xuids);
}

bool PresenceManager::UpdatePresence(const uint64_t xuid) const {
  bool updated = UpdateLocalPresence(xuid);

  if (updated) {
    UpdateXboxLiveLocalUsersPresence({xuid});
  }

  return updated;
}

bool PresenceManager::UpdateLocalPresence(const uint64_t xuid) const {
  bool updated = false;

  if (IsPresenceStringUpdateAvailable(xuid)) {
    updated = BuildPresenceString(xuid, true);
  }

  return updated;
}

bool PresenceManager::BuildPresenceString(
    const uint64_t xuid, bool update, std::u16string* presence_string) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  bool completed = false;

  const auto presence_context = std::find_if(
      user->properties_.cbegin(), user->properties_.cend(),
      [](const Property& property_data) {
        return property_data.GetPropertyId().value == XCONTEXT_PRESENCE;
      });

  if (presence_context == user->properties_.cend()) {
    return completed;
  }

  const auto gdb = kernel_state_->emulator()->game_info_database();

  if (!gdb->HasXLast()) {
    return completed;
  }

  const auto xlast = gdb->GetXLast();

  const std::u16string raw_presence =
      xlast->GetPresenceRawString(*presence_context);

  const auto presence_string_formatter =
      util::AttributeStringFormatter(raw_presence, xlast, xuid);

  completed = presence_string_formatter.IsComplete();

  const auto presence_parsed = presence_string_formatter.GetPresenceString();

  if (completed && update) {
    user->online_presence_desc_ = presence_parsed;
  }

  if (completed && presence_string) {
    *presence_string = presence_parsed;
  }

  return completed;
}

bool PresenceManager::UpdateSubscription(const uint64_t xuid,
                                         const X_ONLINE_PRESENCE& peer) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  if (!IsSubscribed(xuid, peer.xuid)) {
    return false;
  }

  user->subscriptions_[peer.xuid] = peer;

  return true;
}

std::optional<X_ONLINE_PRESENCE> PresenceManager::GetSubscription(
    const uint64_t xuid, const uint64_t subscriber_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return std::nullopt;
  }

  if (!IsSubscribed(xuid, subscriber_xuid)) {
    return std::nullopt;
  }

  return user->subscriptions_[subscriber_xuid];
}

void PresenceManager::Initialize(const uint32_t max_subscriptions) {
  // We're suppose to allocate memory for max subscriptions.
  // However we're simply using a map in profile to manage subscriptions.

  const uint32_t alloc_buffer_size =
      max_subscriptions * sizeof(X_ONLINE_PRESENCE);

  max_subscriptions_ = max_subscriptions;
  initialized_ = true;
}

bool PresenceManager::Subscribe(const uint64_t xuid,
                                const uint64_t subscriber_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  if (user->subscriptions_.size() >= kMaxUserSubscriptions) {
    return false;
  }

  if (!IsOnlineXUID(subscriber_xuid)) {
    return false;
  }

  if (user->GetOnlineXUID() == subscriber_xuid) {
    return false;
  }

  if (IsSubscribed(xuid, subscriber_xuid)) {
    return false;
  }

  // We can access the presence information for friends without subscribing.
  if (friends_manager_->IsFriend(xuid, subscriber_xuid)) {
    return true;
  }

  user->subscriptions_[subscriber_xuid] = {};

  return true;
}

bool PresenceManager::Unsubscribe(const uint64_t xuid,
                                  const uint64_t subscriber_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  if (!IsSubscribed(xuid, subscriber_xuid)) {
    return true;
  }

  return user->subscriptions_.erase(xuid);
}

bool PresenceManager::IsSubscribed(const uint64_t xuid,
                                   const uint64_t subscriber_xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  if (!IsOnlineXUID(subscriber_xuid)) {
    return false;
  }

  return user->subscriptions_.contains(subscriber_xuid);
}

std::set<uint64_t> PresenceManager::GetSubscribedXUIDs(
    const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {};
  }

  const auto subscribed_xuids_view = std::views::keys(user->subscriptions_);

  std::set<uint64_t> subscribed_xuids(subscribed_xuids_view.begin(),
                                      subscribed_xuids_view.end());

  return subscribed_xuids;
}

uint32_t PresenceManager::GetMaxPeerSubscriptions() const {
  return max_subscriptions_;
}

uint32_t PresenceManager::GetSubscribedPeersTotal() const {
  uint32_t total_subscribed_peers = 0;

  for (uint8_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    const auto profile = profile_manager_->GetProfile(user_index);

    if (profile) {
      total_subscribed_peers += profile->subscriptions_.size();
    }
  }

  return total_subscribed_peers;
}

bool PresenceManager::IsPresenceOutOfSync(
    uint64_t xuid, std::vector<FriendPresenceObjectJSON> subscribers) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return false;
  }

  if (subscribers.empty()) {
    return false;
  }

  bool sync_state = false;

  for (const auto& player : subscribers) {
    const uint64_t subscribed_xuid = player.XUID();

    if (!IsSubscribed(xuid, subscribed_xuid)) {
      XELOGI("Requested unknown peer presence: {} - {:016X}", player.Gamertag(),
             subscribed_xuid);
      continue;
    }

    if (sync_state) {
      break;
    }

    const auto subscription = GetSubscription(xuid, subscribed_xuid);

    if (subscription.has_value()) {
      const X_ONLINE_PRESENCE peer = subscription.value();
      const X_ONLINE_PRESENCE updated_peer_presence =
          player.ToOnlineRichPresence();

      sync_state =
          std::memcmp(&peer, &updated_peer_presence, sizeof(X_ONLINE_PRESENCE));
    }
  }

  return sync_state;
}

std::unique_ptr<FriendsPresenceObjectJSON>
PresenceManager::GetLivePresenceFriends(const uint64_t xuid) const {
  return GetFriendsPresence(xuid, friends_manager_->GetFriendsXUIDs(xuid));
}

std::unique_ptr<FriendsPresenceObjectJSON>
PresenceManager::GetLivePresenceSubscribers(const uint64_t xuid) const {
  return GetFriendsPresence(xuid, GetSubscribedXUIDs(xuid));
}

std::unique_ptr<FriendsPresenceObjectJSON> PresenceManager::GetLivePresence(
    const uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {};
  }

  const auto friends_xuids = friends_manager_->GetFriendsXUIDs(xuid);
  const auto subscribed_xuids = GetSubscribedXUIDs(xuid);

  std::set<uint64_t> friends_and_subscribed_xuids = {};

  std::set_union(friends_xuids.cbegin(), friends_xuids.cend(),
                 subscribed_xuids.cbegin(), subscribed_xuids.cend(),
                 std::inserter(friends_and_subscribed_xuids,
                               friends_and_subscribed_xuids.cbegin()));

  return GetFriendsPresence(xuid, friends_and_subscribed_xuids);
}

PresenceSynced PresenceManager::GetPresenceSyncState(uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {{}, {}, {}};
  }

  const auto live_presences = GetLivePresence(xuid);
  const auto& presence_players = live_presences->PlayersPresence();

  if (presence_players.empty()) {
    return {{}, {}, {}};
  }

  const auto friends_xuids = friends_manager_->GetFriendsXUIDs(xuid);
  const auto subscribed_xuids = GetSubscribedXUIDs(xuid);

  auto friends_presence_view =
      presence_players |
      std::views::filter([&friends_xuids](FriendPresenceObjectJSON presence) {
        return friends_xuids.contains(presence.XUID());
      });

  auto subscribed_presence_view =
      presence_players |
      std::views::filter(
          [&subscribed_xuids](FriendPresenceObjectJSON presence) {
            return subscribed_xuids.contains(presence.XUID());
          });

  std::vector<FriendPresenceObjectJSON> friends_presence;
  std::vector<FriendPresenceObjectJSON> subscribed_presence;

  for (const auto& friend_ : friends_presence_view) {
    friends_presence.push_back(friend_);
  }

  for (const auto& subscriber : subscribed_presence_view) {
    subscribed_presence.push_back(subscriber);
  }

  const bool friends_sync_state =
      friends_manager_->IsPresenceOutOfSync(xuid, friends_presence);
  const bool subscribed_sync_state =
      IsPresenceOutOfSync(xuid, subscribed_presence);

  const PresenceSyncState presence_sync_state = {
      .friends = friends_sync_state, .subscribers = subscribed_sync_state};

  PresenceSynced presence_synced_data(presence_sync_state, friends_presence,
                                      subscribed_presence);

  return presence_synced_data;
}

std::future<void> PresenceManager::SyncPresenceAsync(uint64_t xuid) const {
  return std::async(std::launch::async, &PresenceManager::SyncPresence, this,
                    xuid);
}

void PresenceManager::SyncPresence(uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return;
  }

  const PresenceSynced presence_synced = GetPresenceSyncState(xuid);

  if (!presence_synced.GetSyncState().IsOutOfSync()) {
    return;
  }

  XELOGD("Friends/Subscribed peers presence state changed.");

  for (const auto& player : presence_synced.GetFriendsView()) {
    friends_manager_->UpdateFriend(xuid, player.GetFriendPresence());
  }

  for (const auto& player : presence_synced.GetSubscribersView()) {
    UpdateSubscription(xuid, player.ToOnlineRichPresence());
  }

  if (presence_synced.GetSyncState().friends) {
    const uint32_t user_index =
        profile_manager_->GetUserIndexAssignedToProfile(user->xuid());

    kernel_state_->BroadcastNotification(kXNotificationFriendsPresenceChanged,
                                         user_index);
  }

  if (IsInitialized()) {
    if (presence_synced.GetSyncState().subscribers) {
      kernel_state_->BroadcastNotification(kXNotificationLivePresenceChanged,
                                           0);
    }
  }
}

std::vector<X_ONLINE_FRIEND> PresenceManager::GetFriendsPresenceSorted(
    uint64_t xuid) const {
  const auto user = profile_manager_->GetProfileAny(xuid);
  if (!user) {
    return {};
  }

  const auto friends_presence_ref = friends_manager_->GetFriends(xuid);

  if (!friends_presence_ref.has_value()) {
    return {};
  }

  std::vector<X_ONLINE_FRIEND> friends_presence =
      friends_presence_ref.value().get();

  std::sort(
      friends_presence.begin(), friends_presence.end(),
      [](X_ONLINE_FRIEND& peer_1, X_ONLINE_FRIEND& peer_2) {
        uint32_t peer_1_state = peer_1.state & 0xFF;
        uint32_t peer_2_state = peer_2.state & 0xFF;

        if (peer_1_state == peer_2_state &&
            (peer_1.session_id.as_uint64() || peer_2.session_id.as_uint64())) {
          if (peer_1.session_id.as_uint64() && peer_2.session_id.as_uint64()) {
            return true;
          }

          return peer_1.session_id.as_uint64() ? true : false;
        }

        return peer_1_state > peer_2_state;
      });

  return friends_presence;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
