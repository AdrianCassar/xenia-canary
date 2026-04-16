/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_TRACKER_H_
#define XENIA_KERNEL_XAM_USER_TRACKER_H_

#include <chrono>
#include <set>
#include <span>

#include "xenia/xbox.h"

#include "xenia/kernel/json/friend_presence_object_json.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/user_settings.h"

using namespace std::chrono_literals;

namespace xe {
namespace kernel {
namespace xam {

struct TitleInfo {
  std::u16string title_name;
  uint32_t id;
  uint32_t unlocked_achievements_count;
  uint32_t achievements_count;
  uint32_t title_earned_gamerscore;
  uint32_t gamerscore_amount;
  uint32_t flags;
  uint16_t online_unlocked_achievements;
  X_XDBF_AVATARAWARDS_COUNTER all_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER male_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER female_avatar_awards;
  std::chrono::sys_time<std::chrono::system_clock::duration> last_played;

  std::span<const uint8_t> icon;

  bool WasTitlePlayed() const {
    return last_played.time_since_epoch().count() != 0;
  }
};

struct PresenceSyncState {
  bool friends;
  bool peers;

  bool IsOutOfSync() const { return friends || peers; }
};

class UserTracker {
 public:
  UserTracker() = default;
  ~UserTracker() = default;

  // UserTracker specific methods
  bool AddUser(uint64_t xuid);
  bool RemoveUser(uint64_t xuid);

  // SPA related methods
  void UpdateSpaInfo(SpaInfo* spa_info);

  // User related methods
  bool UnlockAchievement(uint64_t xuid, uint32_t achievement_id);
  void RefreshTitleSummary(uint64_t xuid, uint32_t title_id);

  PresenceSyncState IsPresenceOutOfSync(
      const uint64_t xuid,
      const std::vector<FriendPresenceObjectJSON> presence_info) const;

  // XFriendsCreateEnumerator & XPresenceCreateEnumerator
  void RefershFriendsAndSubscribersPresence(const uint64_t xuid) const;

  // XSessions
  void AddOwnedSession(const uint64_t xuid,
                       const uint32_t session_handle) const;
  void RemoveOwnedSession(const uint64_t xuid,
                          const uint32_t session_handle) const;
  bool HasOwnedSessions(const uint64_t xuid) const;
  void CleanupOwnedSessions(const uint64_t xuid) const;

  // Periodic Maintenance
  void PeriodicMaintenance(const uint64_t xuid,
                           const size_t iteration_count) const;
  void StartPeriodicMaintenance(const uint64_t xuid) const;
  void StopPeriodicMaintenance(const uint64_t xuid) const;

  // Context
  void UpdateContext(uint64_t xuid, uint32_t id, uint32_t value);
  std::optional<uint32_t> GetUserContext(uint64_t xuid, uint32_t id) const;
  uint32_t GetContextValue(const uint64_t xuid, const uint32_t id) const;
  uint32_t GetGameModeValue(const uint64_t xuid) const;
  uint32_t GetGameTypeValue(const uint64_t xuid) const;
  std::vector<AttributeKey> GetUserContextIds(uint64_t xuid) const;
  std::u16string GetContextLocalizedString(uint64_t xuid, uint32_t id) const;
  std::u16string GetContextGameModeLocalizedString(uint64_t xuid) const;
  std::u16string GetContextDescription(uint64_t xuid, uint32_t id) const;
  void AddDefaultContexts();

  // Property
  void AddProperty(const uint64_t xuid, const Property* property);
  X_STATUS GetProperty(const uint64_t xuid, uint32_t* property_size,
                       XUSER_PROPERTY* property);
  const Property* GetProperty(const uint64_t xuid, const uint32_t id) const;
  std::vector<AttributeKey> GetUserPropertyIds(uint64_t xuid) const;
  std::u16string GetPropertyDescription(uint32_t id) const;
  void AddDefaultProperties();

  // Settings
  void UpsertSetting(uint64_t xuid, uint32_t title_id,
                     const UserSetting* setting);

  std::optional<UserSetting> GetSetting(UserProfile* user, uint32_t title_id,
                                        uint32_t setting_id) const;

  bool GetUserSetting(uint64_t xuid, uint32_t title_id, uint32_t setting_id,
                      X_USER_PROFILE_SETTING* setting_ptr,
                      uint32_t& extended_data_address) const;

  // Titles
  void AddTitleToPlayedList();
  void RemoveTitleFromPlayedList(uint64_t xuid, uint32_t title_id);
  std::vector<TitleInfo> GetPlayedTitles(uint64_t xuid) const;
  std::optional<TitleInfo> GetUserTitleInfo(uint64_t xuid,
                                            uint32_t title_id) const;

  // Achievements
  std::vector<Achievement> GetUserTitleAchievements(uint64_t xuid,
                                                    uint32_t title_id) const;
  std::span<const uint8_t> GetAchievementIcon(uint64_t xuid, uint32_t title_id,
                                              uint32_t achievement_id) const;

  // Images
  bool UpdateUserIcon(uint64_t xuid, std::span<const uint8_t> icon_data);

  void UpdateGamerpicSetting(uint64_t xuid, uint32_t title_id,
                             uint32_t big_tile_id, uint32_t small_tile_id);

  bool UpdateUserGamerpic(uint64_t xuid, uint32_t title_id,
                          uint32_t big_tile_id, uint32_t small_tile_id,
                          std::vector<uint8_t> small_gamerpic_icon,
                          std::vector<uint8_t> big_gamerpic_icon);

  std::optional<xam::GamerPictureKey> GetUserGamerpicSetting(uint64_t xuid);

  std::span<const uint8_t> GetIcon(uint64_t xuid, uint32_t title_id,
                                   XTileType tile_type, uint64_t tile_id) const;

 private:
  bool IsUserTracked(uint64_t xuid) const;

  void UpdateSettingValue(uint64_t xuid, uint32_t title_id,
                          UserSettingId setting_id, int32_t difference);
  std::optional<UserSetting> GetGpdSetting(UserProfile* user, uint32_t title_id,
                                           uint32_t setting_id) const;

  void AddTitleToPlayedList(uint64_t xuid);
  void AddDefaultProperties(uint64_t xuid);
  void AddDefaultContexts(uint64_t xuid);
  void UpdateTitleGpdFile();
  void UpdateProfileGpd();
  void UpdateMissingAchievemntsIcons();

  void FlushUserData(const uint64_t xuid);

  SpaInfo* spa_data_ = nullptr;

  std::set<uint64_t> tracked_xuids_;

  const std::chrono::seconds periodic_maintenance_interval_ = 5s;

  struct CaseInsensitive {
    bool operator()(const std::u16string lhs, const std::u16string rhs) const {
      const std::u16string lhs_tidy =
          to_utf16(utf8::lower_ascii(xe::to_utf8(lhs)));
      const std::u16string rhs_tidy =
          to_utf16(utf8::lower_ascii(xe::to_utf8(rhs)));

      return std::lexicographical_compare(lhs_tidy.cbegin(), lhs_tidy.cend(),
                                          rhs_tidy.cbegin(), rhs_tidy.cend());
    }
  };
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
