/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/user_profile.h"

#include <ranges>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/emulator.h"
#include "xenia/kernel/util/presence_string_builder.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xlast.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"

#include "xenia/kernel/XLiveAPI.h"

DECLARE_int32(discord_presence_user_index);

namespace xe {
namespace kernel {
namespace xam {

UserProfile::UserProfile(const uint64_t xuid,
                         const X_XAMACCOUNTINFO* account_info)
    : xuid_(xuid), account_info_(*account_info), profile_images_() {
  // 58410A1F checks the user XUID against a mask of 0x00C0000000000000 (3<<54),
  // if non-zero, it prevents the user from playing the game.
  // "You do not have permissions to perform this operation."
  LoadProfileGpds();

  LoadProfileIcon(XTileType::kGamerTile);
  LoadProfileIcon(XTileType::kGamerTileSmall);

  friends_ = std::vector<X_ONLINE_FRIEND>();
  subscriptions_ = std::map<uint64_t, X_ONLINE_PRESENCE>();
  self_invite = {};

  for (const auto& friend_xuid : XLiveAPI::ParseFriendsXUIDs()) {
    AddFriendFromXUID(friend_xuid);
  }
}

GpdInfo* UserProfile::GetGpd(const uint32_t title_id) {
  return const_cast<GpdInfo*>(
      const_cast<const UserProfile*>(this)->GetGpd(title_id));
}

const GpdInfo* UserProfile::GetGpd(const uint32_t title_id) const {
  if (title_id == kDashboardID) {
    return &dashboard_gpd_;
  }

  if (!games_gpd_.count(title_id)) {
    return nullptr;
  }

  return &games_gpd_.at(title_id);
}

void UserProfile::LoadProfileGpds() {
  // First load dashboard GPD because it stores all opened games
  dashboard_gpd_ = LoadGpd(kDashboardID);
  if (!dashboard_gpd_.IsValid()) {
    dashboard_gpd_ = GpdInfoProfile();
  }

  const auto gpds_to_load = dashboard_gpd_.GetTitlesInfo();

  for (const auto& gpd : gpds_to_load) {
    const auto gpd_data = LoadGpd(gpd->title_id);
    if (gpd_data.empty()) {
      continue;
    }

    games_gpd_.emplace(gpd->title_id, GpdInfoTitle(gpd->title_id, gpd_data));
  }
}

void UserProfile::LoadProfileIcon(XTileType tile_type) {
  if (!kTileFileNames.count(tile_type)) {
    return;
  }

  const std::string path =
      fmt::format("User_{:016X}:\\{}", xuid_, kTileFileNames.at(tile_type));

  vfs::File* file = nullptr;
  vfs::FileAction action;

  const X_STATUS result = kernel_state()->file_system()->OpenFile(
      nullptr, path, vfs::FileDisposition::kOpen, vfs::FileAccess::kGenericRead,
      false, true, &file, &action);

  if (result != X_STATUS_SUCCESS) {
    return;
  }

  std::vector<uint8_t> data(file->entry()->size());
  size_t written_bytes = 0;
  file->ReadSync(std::span<uint8_t>(data.data(), file->entry()->size()), 0,
                 &written_bytes);
  file->Destroy();

  profile_images_.insert_or_assign(tile_type, data);
}

void UserProfile::WriteProfileIcon(XTileType tile_type,
                                   std::span<const uint8_t> icon_data) {
  const std::string path =
      fmt::format("User_{:016X}:\\{}", xuid_, kTileFileNames.at(tile_type));

  vfs::File* file = nullptr;
  vfs::FileAction action;

  const X_STATUS result = kernel_state()->file_system()->OpenFile(
      nullptr, path, vfs::FileDisposition::kOverwriteIf,
      vfs::FileAccess::kGenericAll, false, true, &file, &action);

  if (result != X_STATUS_SUCCESS) {
    return;
  }

  size_t written_bytes = 0;

  file->WriteSync({icon_data.data(), icon_data.size()}, 0, &written_bytes);
  file->Destroy();

  profile_images_.insert_or_assign(
      tile_type, std::vector<uint8_t>(icon_data.begin(), icon_data.end()));
}

std::vector<uint8_t> UserProfile::LoadGpd(const uint32_t title_id) {
  auto entry = kernel_state()->file_system()->ResolvePath(
      fmt::format("User_{:016X}:\\{:08X}.gpd", xuid_, title_id));

  if (!entry) {
    XELOGW("User {} (XUID: {:016X}) doesn't have profile GPD!", name(), xuid());
    return {};
  }

  vfs::File* file;
  auto result = entry->Open(vfs::FileAccess::kFileReadData, &file);
  if (result != X_STATUS_SUCCESS) {
    XELOGW("User {} (XUID: {:016X}) cannot open profile GPD!", name(), xuid());
    return {};
  }

  std::vector<uint8_t> data(entry->size());

  size_t read_size = 0;
  result = file->ReadSync(std::span<uint8_t>(data.data(), entry->size()), 0,
                          &read_size);
  if (result != X_STATUS_SUCCESS || read_size != entry->size()) {
    XELOGW(
        "User {} (XUID: {:016X}) cannot read profile GPD! Status: {:08X} read: "
        "{}/{} bytes",
        name(), xuid(), result, read_size, entry->size());
    return {};
  }

  file->Destroy();
  return data;
}

bool UserProfile::WriteGpd(const uint32_t title_id) {
  const GpdInfo* gpd = GetGpd(title_id);
  if (!gpd) {
    return false;
  }

  std::vector<uint8_t> data = gpd->Serialize();

  vfs::File* file = nullptr;
  vfs::FileAction action;

  const std::string mounted_path =
      fmt::format("User_{:016X}:\\{:08X}.gpd", xuid_, title_id);

  const X_STATUS result = kernel_state()->file_system()->OpenFile(
      nullptr, mounted_path, vfs::FileDisposition::kOverwriteIf,
      vfs::FileAccess::kGenericWrite, false, true, &file, &action);

  if (result != X_STATUS_SUCCESS) {
    return false;
  }

  size_t written_bytes = 0;
  file->WriteSync(std::span<uint8_t>(data.data(), data.size()), 0,
                  &written_bytes);
  file->Destroy();
  return true;
}

X_ONLINE_FRIEND UserProfile::GenerateDummyFriend() {
  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<int> dist(0x00, 0xFF);

  X_ONLINE_FRIEND dummy_friend = {};

  // Friend is playing same title
  dummy_friend.title_id = kernel_state()->title_id();

  const uint32_t player_state = X_ONLINE_FRIENDSTATE_FLAG_ONLINE |
                                X_ONLINE_FRIENDSTATE_FLAG_JOINABLE |
                                X_ONLINE_FRIENDSTATE_FLAG_PLAYING;

  const uint32_t user_state = X_ONLINE_FRIENDSTATE_ENUM_ONLINE;

  dummy_friend.xuid =
      kernel_state()->xam_state()->profile_manager()->GenerateXuidOnline();
  dummy_friend.session_id = XNKID();
  dummy_friend.state = player_state | user_state;

  xe::be<uint64_t> session_id = 0xAE00FFFFFFFFFFFF;
  memcpy(dummy_friend.session_id.ab, &session_id, sizeof(XNKID));

  // uint64_t xnkidInvite = 0xAE00FFFFFFFFFFFF;
  // memcpy(dummy_friend.xnkidInvite.ab, &xnkidInvite, sizeof(XNKID));

  std::string gamertag = fmt::format("Player {}", dist(gen));
  std::u16string rich_presence = u"Playing on Xenia";

  char* gamertag_ptr = reinterpret_cast<char*>(dummy_friend.Gamertag);
  strcpy(gamertag_ptr, gamertag.c_str());

  char16_t* rich_presence_ptr =
      reinterpret_cast<char16_t*>(dummy_friend.wszRichPresence);
  xe::string_util::copy_and_swap_truncating(
      rich_presence_ptr, rich_presence, sizeof(dummy_friend.wszRichPresence));

  dummy_friend.cchRichPresence =
      static_cast<uint32_t>(rich_presence.size() * sizeof(char16_t));

  return dummy_friend;
}

void UserProfile::AddDummyFriends(const uint32_t friends_count) {
  if (friends_.size() >= X_ONLINE_MAX_FRIENDS) {
    return;
  }

  for (uint32_t i = 0; i < friends_count; i++) {
    X_ONLINE_FRIEND peer = GenerateDummyFriend();

    AddFriend(&peer);
  }
}

bool UserProfile::GetFriendPresenceFromXUID(const uint64_t xuid,
                                            X_ONLINE_PRESENCE* presence) {
  if (presence == nullptr) {
    return false;
  }

  X_ONLINE_FRIEND peer = {};

  const bool is_friend = GetFriendFromXUID(xuid, &peer);

  if (!is_friend) {
    return false;
  }

  presence->title_id = peer.title_id;
  presence->state = peer.state;
  presence->xuid = peer.xuid;
  presence->session_id = peer.session_id;
  presence->cchRichPresence = peer.cchRichPresence;

  memcpy(presence->wszRichPresence, peer.wszRichPresence,
         presence->cchRichPresence);

  return true;
}

bool UserProfile::SetFriend(const X_ONLINE_FRIEND& update_peer) {
  auto it = std::find_if(
      friends_.begin(), friends_.end(), [&update_peer](X_ONLINE_FRIEND& peer) {
        if (peer.xuid == update_peer.xuid) {
          memcpy(&peer, &update_peer, sizeof(X_ONLINE_FRIEND));
          return true;
        }

        return false;
      });

  if (it != friends_.end()) {
    return false;
  }

  return true;
}

bool UserProfile::AddFriendFromXUID(const uint64_t xuid) {
  X_ONLINE_FRIEND peer = X_ONLINE_FRIEND();
  peer.xuid = xuid;

  return AddFriend(&peer);
}

bool UserProfile::AddFriend(X_ONLINE_FRIEND* peer) {
  if (friends_.size() >= X_ONLINE_MAX_FRIENDS) {
    return false;
  }

  if (GetOnlineXUID() == peer->xuid) {
    return false;
  }

  if (peer == nullptr) {
    return false;
  }

  if (IsFriend(peer->xuid)) {
    return true;
  }

  std::string default_gamertag = fmt::format("{:016X}", peer->xuid.get());

  XELOGI("{}: Added gamertag: {}", __func__, default_gamertag);

  strcpy(peer->Gamertag, default_gamertag.c_str());

  friends_.push_back(*peer);

  return true;
}

bool UserProfile::RemoveFriend(const X_ONLINE_FRIEND& peer) {
  return RemoveFriend(peer.xuid);
}

bool UserProfile::RemoveFriend(const uint64_t xuid) {
  bool removed = false;

  auto it = std::remove_if(
      friends_.begin(), friends_.end(),
      [&xuid](const X_ONLINE_FRIEND& peer) { return peer.xuid == xuid; });

  if (it != friends_.end()) {
    const size_t friends_size = friends_.size();

    friends_.erase(it, friends_.end());
    removed = friends_.size() != friends_size;
  }

  return removed;
}

void UserProfile::RemoveAllFriends() {
  for (const auto& friend_ : GetFriends()) {
    RemoveFriend(friend_.xuid);
    kernel::XLiveAPI::RemoveFriend(friend_.xuid);
  }
}

bool UserProfile::GetFriendFromIndex(const uint32_t index,
                                     X_ONLINE_FRIEND* peer) {
  if (index >= X_ONLINE_MAX_FRIENDS || index >= friends_.size()) {
    return false;
  }

  if (peer == nullptr) {
    return false;
  }

  memcpy(peer, &friends_[index], sizeof(X_ONLINE_FRIEND));

  return true;
}

bool UserProfile::GetFriendFromXUID(const uint64_t xuid,
                                    X_ONLINE_FRIEND* peer) {
  if (peer == nullptr) {
    return false;
  }

  return IsFriend(xuid, peer);
}

bool UserProfile::IsFriend(const uint64_t xuid, X_ONLINE_FRIEND* peer) {
  auto it = std::find_if(
      friends_.begin(), friends_.end(),
      [&xuid](const X_ONLINE_FRIEND& peer) { return peer.xuid == xuid; });

  if (it == friends_.end()) {
    return false;
  }

  if (peer != nullptr) {
    memcpy(peer, &*it, sizeof(X_ONLINE_FRIEND));
  }

  return true;
}

const std::vector<uint64_t> UserProfile::GetFriendsXUIDs() const {
  std::vector<uint64_t> xuids;
  xuids.reserve(friends_.size());

  for (const auto& peer : friends_) {
    xuids.push_back(peer.xuid);
  }

  return xuids;
}

const uint32_t UserProfile::GetFriendsCount() const {
  return static_cast<uint32_t>(friends_.size());
}

bool UserProfile::SetSubscriptionFromXUID(const uint64_t xuid,
                                          X_ONLINE_PRESENCE* peer) {
  if (peer == nullptr) {
    return false;
  }

  memcpy(&subscriptions_[xuid], &peer, sizeof(X_ONLINE_PRESENCE));

  return true;
}

bool UserProfile::GetSubscriptionFromXUID(const uint64_t xuid,
                                          X_ONLINE_PRESENCE* peer) {
  if (!IsSubscribed(xuid)) {
    return false;
  }

  if (peer == nullptr) {
    return false;
  }

  memcpy(peer, &subscriptions_[xuid], sizeof(X_ONLINE_PRESENCE));

  return true;
}

bool UserProfile::SubscribeFromXUID(const uint64_t xuid) {
  if (subscriptions_.size() >= X_ONLINE_PEER_SUBSCRIPTIONS) {
    return false;
  }

  subscriptions_[xuid] = {};

  return true;
}

bool UserProfile::UnsubscribeFromXUID(const uint64_t xuid) {
  if (!IsSubscribed(xuid)) {
    return true;
  }

  if (subscriptions_.erase(xuid)) {
    return true;
  }

  return false;
}

bool UserProfile::IsSubscribed(const uint64_t xuid) {
  return subscriptions_.count(xuid) != 0;
}

void UserProfile::SetSelfInvite(X_INVITE_INFO* invite_info) {
  memcpy(&self_invite, invite_info, sizeof(X_INVITE_INFO));
}

const std::vector<uint64_t> UserProfile::GetSubscribedXUIDs() const {
  std::vector<uint64_t> subscribed_xuids;

  for (const auto& [key, _] : subscriptions_) {
    subscribed_xuids.push_back(key);
  }

  return subscribed_xuids;
}

bool UserProfile::MutePlayer(uint64_t xuid) {
  const bool muted = IsPlayerMuted(xuid);

  if (!muted) {
    muted_players_.push_back(xuid);
  }

  return !muted;
}

bool UserProfile::UnmutePlayer(uint64_t xuid) {
  const bool unmuted = std::erase_if(
      muted_players_,
      [xuid](const uint64_t muted_xuid) { return muted_xuid == xuid; });

  return unmuted;
}

bool UserProfile::IsPlayerMuted(uint64_t xuid) const {
  const auto it = std::find_if(
      muted_players_.cbegin(), muted_players_.cend(),
      [xuid](const uint64_t muted_xuid) { return muted_xuid == xuid; });

  return it != muted_players_.end();
}

std::u16string UserProfile::GetPresenceString() const {
  return online_presence_desc_;
}

bool UserProfile::UpdatePresence() {
  const auto current_presence = GetPresenceString();

  bool updated = false;

  if (BuildPresenceString()) {
    const auto updated_presence = GetPresenceString();

    updated = current_presence != updated_presence;

    if (!updated) {
      return false;
    }

    XELOGI("{}: {} - {}", __func__, name(), xe::to_utf8(updated_presence));

    const uint32_t user_index =
        kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
            xuid_);

    if (cvars::discord_presence_user_index == user_index) {
      kernel_state()->emulator()->on_presence_change(
          kernel_state()->emulator()->title_name(), updated_presence);
    }

    auto run = [] { XLiveAPI::SetPresence(); };

    std::thread update_presence(run);
    update_presence.detach();
  }

  return updated;
}

bool UserProfile::BuildPresenceString() {
  bool completed = false;

  const xam::Property* presence_prop =
      kernel_state()->xam_state()->user_tracker()->GetProperty(
          xuid_, XCONTEXT_PRESENCE);

  if (!presence_prop) {
    return completed;
  }

  const auto gdb = kernel_state()->emulator()->game_info_database();

  if (!gdb->HasXLast()) {
    return completed;
  }

  const auto xlast = gdb->GetXLast();

  const std::u16string raw_presence =
      xlast->GetPresenceRawString(presence_prop);

  const auto presence_string_formatter =
      util::AttributeStringFormatter::AttributeStringFormatter(raw_presence,
                                                               xlast, xuid_);

  const auto presence_parsed = presence_string_formatter.GetPresenceString();

  if (online_presence_desc_ != presence_parsed) {
    online_presence_desc_ = presence_parsed;
  }

  completed = presence_string_formatter.IsComplete();

  return completed;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
