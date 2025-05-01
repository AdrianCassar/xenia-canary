/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/emulator.h"
#include "xenia/kernel/xam/user_profile.h"

#include <ranges>
#include <sstream>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/stb/stb_image.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_data.h"
#include "xenia/kernel/xam/user_property.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"

DECLARE_int32(user_language);

DECLARE_int32(user_country);

namespace xe {
namespace kernel {
namespace xam {

bool UserTracker::AddUser(uint64_t xuid) {
  if (IsUserTracked(xuid)) {
    XELOGW("{}: User is already on tracking list!");
    return false;
  }

  tracked_xuids_.insert(xuid);

  if (spa_data_) {
    AddTitleToPlayedList(xuid);
    AddDefaultProperties(xuid);
    AddDefaultContexts(xuid);
  }
  return true;
}

bool UserTracker::RemoveUser(uint64_t xuid) {
  if (!IsUserTracked(xuid)) {
    XELOGW("{}: User is not on tracking list!");
    return false;
  }

  tracked_xuids_.erase(xuid);
  FlushUserData(xuid);
  return true;
}

bool UserTracker::UnlockAchievement(uint64_t xuid, uint32_t achievement_id) {
  if (!IsUserTracked(xuid)) {
    XELOGW("{}: User is not on tracking list!", __func__);
    return false;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return false;
  }

  if (!spa_data_) {
    XELOGW("{}: Missing title SPA.", __func__);
    return false;
  }

  const auto spa_achievement = spa_data_->GetAchievement(achievement_id);
  if (!spa_achievement) {
    XELOGW("{}: Missing achievement data in SPA.", __func__);
    return false;
  }

  // Update data in profile gpd.
  auto title_info = user->dashboard_gpd_.GetTitleInfo(spa_data_->title_id());
  if (!title_info) {
    return false;
  }

  // Update title gpd
  auto title_gpd = &user->games_gpd_[spa_data_->title_id()];
  // Achievement is unlocked, so we need to add achievement icon
  title_gpd->AddImage(spa_achievement->image_id,
                      spa_data_->GetIcon(spa_achievement->image_id));

  auto gpd_achievement = title_gpd->GetAchievementEntry(spa_achievement->id);
  if (!gpd_achievement) {
    XELOGW(
        "{}: Missing achievement data in title GPD. (User: {} Title: {:08X})",
        __func__, user->name(), spa_data_->title_id());
    return false;
  }

  title_info->achievements_unlocked++;
  title_info->gamerscore_earned += spa_achievement->gamerscore;

  const std::string achievement_name = spa_data_->GetStringTableEntry(
      spa_data_->default_language(), spa_achievement->label_id);

  XELOGI("Player: {} Unlocked Achievement: {}", user->name(),
         achievement_name.c_str());

  gpd_achievement->flags = gpd_achievement->flags |
                           static_cast<uint32_t>(AchievementFlags::kAchieved);
  gpd_achievement->unlock_time = Clock::QueryGuestSystemTime();

  UpdateSettingValue(xuid, kDashboardID, UserSettingId::XPROFILE_GAMERCARD_CRED,
                     gpd_achievement->gamerscore);
  UpdateSettingValue(xuid, kDashboardID,
                     UserSettingId::XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED, 1);
  UpdateSettingValue(xuid, spa_data_->title_id(),
                     UserSettingId::XPROFILE_GAMERCARD_TITLE_CRED_EARNED,
                     gpd_achievement->gamerscore);
  UpdateSettingValue(
      xuid, spa_data_->title_id(),
      UserSettingId::XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED, 1);

  FlushUserData(xuid);
  return true;
}

void UserTracker::FlushUserData(const uint64_t xuid) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  user->WriteGpd(kDashboardID);

  if (spa_data_) {
    user->WriteGpd(spa_data_->title_id());
  }
}

void UserTracker::AddTitleToPlayedList() {
  if (!spa_data_) {
    return;
  }

  for (const uint64_t xuid : tracked_xuids_) {
    AddTitleToPlayedList(xuid);
  }
}

void UserTracker::AddTitleToPlayedList(uint64_t xuid) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  if (!spa_data_) {
    return;
  }

  if (!spa_data_->include_in_profile() || spa_data_->is_system_app()) {
    return;
  }

  const uint32_t title_id = spa_data_->title_id();
  auto title_gpd = user->games_gpd_.find(title_id);
  if (title_gpd == user->games_gpd_.end()) {
    user->games_gpd_.emplace(title_id, GpdInfoTitle(title_id));
    UpdateTitleGpdFile();
  }

  if (!spa_data_->include_in_profile()) {
    return;
  }

  const uint64_t current_time = Clock::QueryGuestSystemTime();

  auto title_info = user->dashboard_gpd_.GetTitleInfo(title_id);
  if (!title_info) {
    user->dashboard_gpd_.AddNewTitle(spa_data_);
    UpdateSettingValue(xuid, kDashboardID,
                       UserSettingId::XPROFILE_GAMERCARD_TITLES_PLAYED, 1);
    title_info = user->dashboard_gpd_.GetTitleInfo(title_id);
  }
  // Normally we only need to update last booted time. Everything else is filled
  // during creation time OR SPA UPDATE TIME!
  title_info->last_played = current_time;

  UpdateProfileGpd();
}

void UserTracker::AddDefaultProperties() {
  if (!spa_data_) {
    return;
  }

  for (const uint64_t xuid : tracked_xuids_) {
    AddDefaultProperties(xuid);
  }
}

void UserTracker::AddDefaultProperties(uint64_t xuid) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  const std::u16string gamertag =
      xe::string_util::read_u16string_and_swap(user->account_info_.gamertag);

  Property PUID =
      Property(XPROPERTY_GAMER_PUID,
               static_cast<int64_t>(user->account_info_.xuid_online));
  Property GAMER_HOST_NAME = Property(XPROPERTY_GAMER_HOSTNAME, gamertag);
  Property GAMER_NAME = Property(XPROPERTY_GAMERNAME, gamertag);
  Property GAMER_ZONE = Property(
      XPROPERTY_GAMER_ZONE,
      static_cast<int32_t>(GAMERCARD_ZONE_OPTIONS::GAMERCARD_ZONE_PRO));
  Property GAMER_COUNTRY =
      Property(XPROPERTY_GAMER_COUNTRY, cvars::user_country);
  Property GAMER_LANGUAGE =
      Property(XPROPERTY_GAMER_LANGUAGE, cvars::user_language);
  Property PLATFORM_TYPE = Property(
      XPROPERTY_PLATFORM_TYPE, static_cast<int32_t>(PLATFORM_TYPE::Xbox360));
  Property GAMER_MU = Property(XPROPERTY_GAMER_MU, 0.0);
  Property GAMER_SIGMA = Property(XPROPERTY_GAMER_SIGMA, 0.0);

  AddProperty(xuid, &PUID);  // Required - 58410AC2 sets this manually
  AddProperty(xuid, &GAMER_HOST_NAME);  // Required
  AddProperty(xuid, &GAMER_NAME);
  AddProperty(xuid, &GAMER_ZONE);
  AddProperty(xuid, &GAMER_COUNTRY);
  AddProperty(xuid, &GAMER_LANGUAGE);
  AddProperty(xuid, &PLATFORM_TYPE);
  AddProperty(xuid, &GAMER_MU);
  AddProperty(xuid, &GAMER_SIGMA);
}

void UserTracker::AddDefaultContexts() {
  if (!spa_data_) {
    return;
  }

  for (const uint64_t xuid : tracked_xuids_) {
    AddDefaultContexts(xuid);
  }
}

void UserTracker::AddDefaultContexts(uint64_t xuid) {
  Property GAME_MODE = Property(XCONTEXT_GAME_MODE, static_cast<uint32_t>(0));
  Property GAME_TYPE = Property(XCONTEXT_GAME_TYPE, static_cast<uint32_t>(0));

  if (kernel_state()->emulator()->game_info_database()->HasXLast()) {
    const util::XLast* xlast =
        kernel_state()->emulator()->game_info_database()->GetXLast();

    bool initialize_all_contexts = false;

    // Initialize all contexts to default values
    if (initialize_all_contexts) {
      for (const uint32_t& context_id :
           xlast->GetContextsQuery()->GetContextsIDs()) {
        std::optional<uint32_t> default_value =
            xlast->GetContextsQuery()->GetContextDefaultValue(context_id);

        if (default_value.has_value()) {
          Property prop = Property(context_id, default_value.value());

          AddProperty(xuid, &prop);
        }
      }
    }

    // System contexts
    std::optional<uint32_t> game_mode_default =
        xlast->GetGameModeQuery()->GetGameModeDefaultValue();
    std::optional<uint32_t> game_type_default =
        xlast->GetContextsQuery()->GetContextDefaultValue(XCONTEXT_GAME_TYPE);

    if (game_mode_default.has_value()) {
      GAME_MODE = Property(XCONTEXT_GAME_MODE, game_mode_default.value());
    }

    if (game_type_default.has_value()) {
      GAME_TYPE = Property(XCONTEXT_GAME_TYPE, game_type_default.value());
    }
  }

  AddProperty(xuid, &GAME_MODE);
  AddProperty(xuid, &GAME_TYPE);
}

std::u16string UserTracker::GetContextLocalizedString(uint64_t xuid,
                                                      uint32_t id) const {
  const Property* context = GetProperty(xuid, id);

  if (!context) {
    return u"";
  }

  if (id == XCONTEXT_GAME_MODE) {
    return GetContextGameModeLocalizedString(xuid);
  }

  if (id == XCONTEXT_PRESENCE) {
    auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
    if (!user) {
      return u"";
    }

    return user->GetPresenceString();
  }

  std::u16string localized_string = u"";

  if (kernel_state()->emulator()->game_info_database()->HasXLast()) {
    util::XLast* xlast =
        kernel_state()->emulator()->game_info_database()->GetXLast();

    util::XLastContextsQuery* context_query = xlast->GetContextsQuery();

    std::optional<std::uint32_t> context_value_string =
        context_query->GetContextValueStringID(id,
                                               context->get_data()->data.u32);

    if (context_value_string.has_value()) {
      localized_string = xlast->GetLocalizedString(
          context_value_string.value(),
          static_cast<XLanguage>(cvars::user_language));
    }
  }

  return localized_string;
}

std::u16string UserTracker::GetContextGameModeLocalizedString(
    uint64_t xuid) const {
  const Property* context = GetProperty(xuid, XCONTEXT_GAME_MODE);

  if (!context) {
    return u"";
  }

  std::u16string localized_string = u"";

  if (kernel_state()->emulator()->game_info_database()->HasXLast()) {
    util::XLast* xlast =
        kernel_state()->emulator()->game_info_database()->GetXLast();

    util::XLastGameModeQuery* gamemode_query = xlast->GetGameModeQuery();

    std::optional<std::uint32_t> gamemode_value_string =
        gamemode_query->GetGameModeStringID(context->get_data()->data.u32);

    if (gamemode_value_string.has_value()) {
      localized_string = xlast->GetLocalizedString(
          gamemode_value_string.value(),
          static_cast<XLanguage>(cvars::user_language));
    }
  }

  return localized_string;
}

std::u16string UserTracker::GetContextDescription(uint64_t xuid,
                                                  uint32_t id) const {
  const auto& context_data = spa_data_->GetContext(id);
  if (!context_data) {
    return u"";
  }

  const Property* context = GetProperty(xuid, id);

  std::set<std::u16string, CompareEqualString> context_strings = {};

  switch (id) {
    case XCONTEXT_PRESENCE: {
      auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
      if (!user) {
        return u"";
      }

      context_strings.emplace(user->GetPresenceString());
    } break;
    case XCONTEXT_GAME_MODE: {
      context_strings.emplace(GetContextGameModeLocalizedString(xuid));
    } break;
    default: {
      uint16_t string_id = context_data->string_id;

      if (string_id == std::numeric_limits<uint16_t>::max()) {
        return u"";
      }

      context_strings.emplace(GetContextLocalizedString(xuid, id));

      if (kernel_state()->emulator()->game_info_database()->HasXLast()) {
        auto context_query = kernel_state()
                                 ->emulator()
                                 ->game_info_database()
                                 ->GetXLast()
                                 ->GetContextsQuery();

        std::optional<std::string> friendly_name =
            context_query->GetContextFriendlyName(context_data->id);

        if (friendly_name.has_value()) {
          context_strings.emplace(xe::to_utf16(friendly_name.value()));
        }
      }
    } break;
  }

  if (context_strings.empty()) {
    return u"";
  }

  std::u16string context_desc = u"";

  for (uint32_t index = 1; const auto& desc : context_strings) {
    if (desc.empty()) {
      continue;
    }

    context_desc.append(desc);

    if (index != context_strings.size()) {
      context_desc.append(u", ");
    }

    index++;
  }

  if (!context_desc.empty()) {
    std::string context_desc_fmt =
        fmt::format("Context: {:08X} - {}", context_data->id.get(),
                    xe::to_utf8(context_desc));

    context_desc = xe::to_utf16(context_desc_fmt);
  }

  return context_desc;
}

std::u16string UserTracker::GetPropertyDescription(uint32_t id) const {
  std::set<std::u16string, CompareEqualString> property_strings = {};

  const auto& property_data = spa_data_->GetProperty(id);
  if (!property_data) {
    return u"";
  }

  uint16_t string_id = property_data->string_id;

  if (string_id == std::numeric_limits<uint16_t>::max()) {
    return u"";
  }

  if (kernel_state()->emulator()->game_info_database()->HasXLast()) {
    util::XLast* xlast =
        kernel_state()->emulator()->game_info_database()->GetXLast();

    std::u16string localized_string = xlast->GetLocalizedString(
        property_data->string_id, static_cast<XLanguage>(cvars::user_language));

    property_strings.emplace(localized_string);

    const auto property_query = xlast->GetPropertiesQuery();

    std::optional<std::string> friendly_name =
        property_query->GetPropertyFriendlyName(property_data->id);

    if (friendly_name.has_value()) {
      property_strings.emplace(xe::to_utf16(friendly_name.value()));
    }
  }

  std::u16string property_desc = u"";

  for (uint32_t index = 1; const auto& desc : property_strings) {
    if (desc.empty()) {
      continue;
    }

    property_desc.append(desc);

    if (index != property_strings.size()) {
      property_desc.append(u", ");
    }

    index++;
  }

  std::string property_desc_fmt =
      fmt::format("Property: {:08X} - {}", property_data->id.get(),
                  xe::to_utf8(property_desc));

  property_desc = xe::to_utf16(property_desc_fmt);

  return property_desc;
}

// Privates
bool UserTracker::IsUserTracked(uint64_t xuid) const {
  return tracked_xuids_.find(xuid) != tracked_xuids_.cend();
}

std::optional<TitleInfo> UserTracker::GetUserTitleInfo(
    uint64_t xuid, uint32_t title_id) const {
  if (!IsUserTracked(xuid)) {
    XELOGW("{}: User is not on tracking list!");
    return std::nullopt;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return std::nullopt;
  }

  const auto title_data = user->dashboard_gpd_.GetTitleInfo(title_id);
  if (!title_data) {
    return std::nullopt;
  }

  auto game_gpd = user->games_gpd_.find(title_id);
  if (game_gpd == user->games_gpd_.cend()) {
    return std::nullopt;
  }

  TitleInfo info;
  info.id = title_data->title_id;
  info.achievements_count = title_data->achievements_count;
  info.unlocked_achievements_count = title_data->achievements_unlocked;
  info.gamerscore_amount = title_data->gamerscore_total;
  info.title_earned_gamerscore = title_data->gamerscore_earned;
  info.title_name = user->dashboard_gpd_.GetTitleName(title_id);
  info.icon = game_gpd->second.GetImage(kXdbfIdTitle);

  if (title_data->last_played.is_valid()) {
    info.last_played = chrono::WinSystemClock::to_local(
        title_data->last_played.to_time_point());
  }

  return info;
}

std::vector<TitleInfo> UserTracker::GetPlayedTitles(uint64_t xuid) const {
  auto user = kernel_state()->xam_state()->GetUserProfileAny(xuid);
  if (!user) {
    return {};
  }

  std::vector<TitleInfo> played_titles;

  const auto titles_data = user->dashboard_gpd_.GetTitlesInfo();
  for (const auto& title_data : titles_data) {
    if (!title_data->include_in_enumerator()) {
      continue;
    }

    TitleInfo info;
    info.id = title_data->title_id;
    info.achievements_count = title_data->achievements_count;
    info.unlocked_achievements_count = title_data->achievements_unlocked;
    info.gamerscore_amount = title_data->gamerscore_total;
    info.title_earned_gamerscore = title_data->gamerscore_earned;
    info.flags = title_data->flags;
    info.all_avatar_awards = title_data->all_avatar_awards;
    info.male_avatar_awards = title_data->male_avatar_awards;
    info.female_avatar_awards = title_data->female_avatar_awards;
    info.online_unlocked_achievements = title_data->online_achievement_count;
    info.title_name = user->dashboard_gpd_.GetTitleName(title_data->title_id);

    if (title_data->last_played.is_valid()) {
      info.last_played = chrono::WinSystemClock::to_local(
          title_data->last_played.to_time_point());
    }

    auto game_gpd = user->games_gpd_.find(title_data->title_id);
    if (game_gpd != user->games_gpd_.cend()) {
      info.icon = game_gpd->second.GetImage(kXdbfIdTitle);
    }

    played_titles.push_back(info);
  }

  std::sort(played_titles.begin(), played_titles.end(),
            [](const TitleInfo& first, const TitleInfo& second) {
              return first.last_played > second.last_played;
            });

  return played_titles;
}

void UserTracker::UpdateMissingAchievemntsIcons() {
  for (auto& user_xuid : tracked_xuids_) {
    auto user = kernel_state()->xam_state()->GetUserProfile(user_xuid);
    if (!user) {
      continue;
    }

    auto game_gpd = user->games_gpd_.find(spa_data_->title_id());
    if (game_gpd == user->games_gpd_.cend()) {
      continue;
    }

    for (const auto& id : game_gpd->second.GetAchievementsIds()) {
      const auto entry = game_gpd->second.GetAchievementEntry(id);

      if (!entry) {
        continue;
      }

      if (!entry->is_achievement_unlocked()) {
        continue;
      }

      if (!game_gpd->second.GetImage(entry->image_id).empty()) {
        continue;
      }

      game_gpd->second.AddImage(entry->image_id,
                                spa_data_->GetIcon(entry->image_id));
    }
    user->WriteGpd(spa_data_->title_id());
  }
}

void UserTracker::UpdateSpaInfo(SpaInfo* spa_info) {
  spa_data_ = spa_info;

  if (!spa_data_) {
    return;
  }

  UpdateProfileGpd();
  UpdateTitleGpdFile();
  UpdateMissingAchievemntsIcons();
}

void UserTracker::UpdateTitleGpdFile() {
  for (auto& user_xuid : tracked_xuids_) {
    auto user = kernel_state()->xam_state()->GetUserProfile(user_xuid);
    if (!user) {
      continue;
    }

    auto game_gpd = user->games_gpd_.find(spa_data_->title_id());
    if (game_gpd == user->games_gpd_.cend()) {
      continue;
    }

    auto user_language = spa_data_->GetExistingLanguage(
        static_cast<XLanguage>(cvars::user_language));

    // First add achievements because of lowest ID
    for (const auto& entry : spa_data_->GetAchievements()) {
      AchievementDetails details(entry, spa_data_, user_language);
      game_gpd->second.AddAchievement(&details);
    }

    // Then add game icon
    game_gpd->second.AddImage(kXdbfIdTitle, spa_data_->title_icon());

    // At the end add title name entry
    game_gpd->second.AddString(kXdbfIdTitle,
                               xe::to_utf16(spa_data_->title_name()));

    // Check if we have icon for every unlocked achievements.
    FlushUserData(user_xuid);
  }
}

void UserTracker::UpdateProfileGpd() {
  for (auto& user_xuid : tracked_xuids_) {
    auto user = kernel_state()->xam_state()->GetUserProfile(user_xuid);
    if (!user) {
      continue;
    }

    auto title_data = user->dashboard_gpd_.GetTitleInfo(spa_data_->title_id());
    if (!title_data) {
      continue;
    }

    const uint32_t achievements_count = spa_data_->achievement_count();
    // If achievements count doesn't match then obviously gamerscore won't match
    // either
    if (title_data->achievements_count < achievements_count) {
      X_XDBF_GPD_TITLE_PLAYED title_updated_data = *title_data;

      title_updated_data.achievements_count = achievements_count;
      title_updated_data.gamerscore_total = spa_data_->total_gamerscore();
      user->dashboard_gpd_.UpdateTitleInfo(spa_data_->title_id(),
                                           &title_updated_data);
    }

    FlushUserData(user_xuid);
  }
}

std::vector<Achievement> UserTracker::GetUserTitleAchievements(
    uint64_t xuid, uint32_t title_id) const {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  auto game_gpd = user->games_gpd_.find(title_id);
  if (game_gpd == user->games_gpd_.cend()) {
    return {};
  }

  std::vector<Achievement> achievements;

  for (const uint32_t id : game_gpd->second.GetAchievementsIds()) {
    Achievement achievement(game_gpd->second.GetAchievementEntry(id));

    achievement.achievement_name = game_gpd->second.GetAchievementTitle(id);
    achievement.unlocked_description =
        game_gpd->second.GetAchievementDescription(id);
    achievement.locked_description =
        game_gpd->second.GetAchievementUnachievedDescription(id);

    achievements.push_back(std::move(achievement));
  }

  return achievements;
};

std::span<const uint8_t> UserTracker::GetAchievementIcon(
    uint64_t xuid, uint32_t title_id, uint32_t achievement_id) const {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  auto game_gpd = user->games_gpd_.find(title_id);
  if (game_gpd == user->games_gpd_.cend()) {
    return {};
  }

  const auto entry = game_gpd->second.GetAchievementEntry(achievement_id);
  if (!entry) {
    return {};
  }

  return GetIcon(xuid, title_id, XTileType::kAchievement, entry->image_id);
}

void UserTracker::AddProperty(const uint64_t xuid, const Property* property) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  const auto property_id = property->GetPropertyId();

  if (property->IsContext()) {
    const auto context_data = spa_data_->GetContext(property_id.value);
    if (UserData::is_system_property(property_id.value)) {
      if (!context_data) {
        XELOGD("{}: System Context {:08X} not in SPA - Adding anyway!",
               __func__, property_id.value);
      }
    } else {
      if (!context_data) {
        return;
      }
    }
  } else {
    const auto property_data = spa_data_->GetProperty(property_id.value);

    // 534507D4 doesn't include system properties in SPA therefore we must
    // always add system properties
    if (UserData::is_system_property(property_id.value)) {
      if (!property_data) {
        XELOGD("{}: System Property {:08X} not in SPA - Adding anyway!",
               __func__, property_id.value);
      }
    } else {
      if (!property_data) {
        return;
      }
    }
  }

  auto entry = std::find_if(user->properties_.begin(), user->properties_.end(),
                            [property_id](const Property& property_data) {
                              return property_data.GetPropertyId().value ==
                                     property_id.value;
                            });

  if (entry != user->properties_.end()) {
    *entry = *property;
    return;
  }

  user->properties_.push_back(*property);
}

X_STATUS UserTracker::GetProperty(const uint64_t xuid, uint32_t* property_size,
                                  XUSER_PROPERTY* property) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return X_E_NOTFOUND;
  }

  *property_size = 0;
  const auto& property_id = property->property_id;

  const auto entry =
      std::find_if(user->properties_.cbegin(), user->properties_.cend(),
                   [property_id](const Property& property_data) {
                     return property_data.GetPropertyId().value == property_id;
                   });

  if (entry == user->properties_.cend()) {
    return X_E_INVALIDARG;
  }

  if (entry->requires_additional_data()) {
    if (!property->data.data.binary.ptr) {
      return X_E_INVALIDARG;
    }
  }

  *property_size = static_cast<uint32_t>(entry->get_data_size());
  entry->WriteToGuest(property);
  return X_E_SUCCESS;
}

const Property* UserTracker::GetProperty(const uint64_t xuid,
                                         const uint32_t id) const {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return nullptr;
  }

  const auto entry =
      std::find_if(user->properties_.cbegin(), user->properties_.cend(),
                   [id](const Property& property_data) {
                     return property_data.GetPropertyId().value == id;
                   });

  if (entry == user->properties_.cend()) {
    return nullptr;
  }

  return &(*entry);
}

std::optional<UserSetting> UserTracker::GetGpdSetting(
    UserProfile* user, uint32_t title_id, uint32_t setting_id) const {
  auto game_gpd = user->games_gpd_.find(title_id);
  if (game_gpd != user->games_gpd_.cend()) {
    auto setting = game_gpd->second.GetSetting(setting_id);
    if (setting) {
      return std::make_optional<UserSetting>(
          setting, game_gpd->second.GetSettingData(setting_id));
    }
  }

  auto setting = user->dashboard_gpd_.GetSetting(setting_id);
  if (setting) {
    return std::make_optional<UserSetting>(
        setting, user->dashboard_gpd_.GetSettingData(setting_id));
  }

  return std::nullopt;
}

std::optional<UserSetting> UserTracker::GetSetting(UserProfile* user,
                                                   uint32_t title_id,
                                                   uint32_t setting_id) const {
  auto gpd_setting = GetGpdSetting(user, title_id, setting_id);
  if (gpd_setting) {
    return gpd_setting.value();
  }

  return UserSetting::GetDefaultSetting(user, setting_id);
}

bool UserTracker::GetUserSetting(uint64_t xuid, uint32_t title_id,
                                 uint32_t setting_id,
                                 X_USER_PROFILE_SETTING* setting_ptr,
                                 uint32_t& extended_data_address) const {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return false;
  }

  auto setting = GetSetting(user, title_id, setting_id);
  if (!setting) {
    return false;
  }
  setting_ptr->setting_id = setting_id;
  setting_ptr->source = setting->get_setting_source();

  setting->WriteToGuest(setting_ptr, extended_data_address);
  return true;
}

void UserTracker::UpdateContext(uint64_t xuid, uint32_t id, uint32_t value) {
  if (!IsUserTracked(xuid)) {
    return;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  const auto& context_data = spa_data_->GetContext(id);
  if (!context_data) {
    return;
  }

  if (value > context_data->max_value) {
    return;
  }

  const auto entry =
      std::find_if(user->properties_.begin(), user->properties_.end(),
                   [id](const Property& property_data) {
                     return property_data.IsContext() &&
                            property_data.GetPropertyId().value == id;
                   });

  if (entry != user->properties_.cend()) {
    *entry = Property(id, value);
    return;
  }

  user->properties_.push_back(Property(id, value));
}

std::optional<uint32_t> UserTracker::GetUserContext(uint64_t xuid,
                                                    uint32_t id) const {
  if (!IsUserTracked(xuid)) {
    return std::nullopt;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return std::nullopt;
  }

  const auto& context_data = spa_data_->GetContext(id);
  if (!context_data) {
    return std::nullopt;
  }

  const auto entry = std::find_if(
      user->properties_.cbegin(), user->properties_.cend(),
      [id](const Property& property_data) {
        return property_data.get_type() == X_USER_DATA_TYPE::CONTEXT &&
               property_data.GetPropertyId().value == id;
      });

  if (entry == user->properties_.cend()) {
    return std::nullopt;
  }

  return entry->get_data()->data.u32;
}

std::vector<AttributeKey> UserTracker::GetUserContextIds(uint64_t xuid) const {
  if (!IsUserTracked(xuid)) {
    return {};
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  std::vector<AttributeKey> entries;

  for (const auto& property : user->properties_) {
    if (!property.IsContext()) {
      continue;
    }

    entries.push_back(property.GetPropertyId());
  }
  return entries;
}

std::vector<AttributeKey> UserTracker::GetUserPropertyIds(uint64_t xuid) const {
  if (!IsUserTracked(xuid)) {
    return {};
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  std::vector<AttributeKey> entries;

  for (const auto& property : user->properties_) {
    if (property.IsContext()) {
      continue;
    }

    entries.push_back(property.GetPropertyId());
  }
  return entries;
}

void UserTracker::UpdateSettingValue(uint64_t xuid, uint32_t title_id,
                                     UserSettingId setting_id,
                                     int32_t difference) {
  if (!IsUserTracked(xuid)) {
    return;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  GpdInfo* info = user->GetGpd(title_id);
  if (!info) {
    return;
  }

  auto setting = info->GetSetting(static_cast<uint32_t>(setting_id));

  if (!setting) {
    UserSetting new_setting(setting_id, difference);
    info->UpsertSetting(&new_setting);
    return;
  }

  const int32_t new_value = setting->base_data.s32 + difference;
  UserSetting new_setting(setting_id, new_value);
  info->UpsertSetting(&new_setting);
}

void UserTracker::UpsertSetting(uint64_t xuid, uint32_t title_id,
                                const UserSetting* setting) {
  if (!IsUserTracked(xuid)) {
    return;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  // Sometimes games like to ignore providing expicitly title_id, so we need to
  // check it.
  if (!title_id) {
    title_id = spa_data_->title_id();
  }

  GpdInfo* info = user->GetGpd(title_id);
  if (!info) {
    return;
  }

  info->UpsertSetting(setting);
  FlushUserData(xuid);
}

bool UserTracker::UpdateUserIcon(uint64_t xuid,
                                 std::span<const uint8_t> icon_data) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return false;
  }

  int width, height, channels;
  if (!stbi_info_from_memory(icon_data.data(),
                             static_cast<int>(icon_data.size()), &width,
                             &height, &channels)) {
    return false;
  }

  XTileType icon_type = XTileType::kGameIcon;

  if (std::pair<uint16_t, uint16_t>(width, height) == kProfileIconSize) {
    icon_type = XTileType::kGamerTile;
  } else if (std::pair<uint16_t, uint16_t>(width, height) ==
             kProfileIconSizeSmall) {
    icon_type = XTileType::kGamerTileSmall;
  } else {
    return false;
  }

  user->WriteProfileIcon(icon_type, icon_data);
  return true;
}

std::span<const uint8_t> UserTracker::GetIcon(uint64_t xuid, uint32_t title_id,
                                              XTileType tile_type,
                                              uint64_t tile_id) const {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  if (!title_id) {
    if (kernel_state()->emulator()->is_title_open()) {
      title_id = kernel_state()->title_id();
    }
  }

  switch (tile_type) {
    case XTileType::kAchievement: {
      if (title_id == kernel_state()->title_id()) {
        return spa_data_->GetIcon(tile_id);
      } else {
        const auto gpd = user->GetGpd(title_id);
        if (!gpd) {
          return {};
        }

        return gpd->GetImage(tile_id);
      }
    }
    case XTileType::kGameIcon: {
      const auto gpd = user->GetGpd(title_id);
      if (!gpd) {
        return {};
      }

      return gpd->GetImage(tile_id);
    }
    case XTileType::kGamerTile:
    case XTileType::kGamerTileSmall:
    case XTileType::kLocalGamerTile:
    case XTileType::kLocalGamerTileSmall:
    case XTileType::kPersonalGamerTile:
    case XTileType::kPersonalGamerTileSmall:
      return user->GetProfileIcon(tile_type);

    default:
      XELOGW("{}: Unsupported tile_type: {:08X} for title: {:08X} Id: {:16X}",
             __func__, static_cast<uint32_t>(tile_type), title_id, tile_id);
  }

  return {};
}

void UserTracker::RefreshTitleSummary(uint64_t xuid, uint32_t title_id) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  auto profile_gpd = user->GetGpd(kDashboardID);
  if (!profile_gpd) {
    return;
  }

  const auto title_gpd =
      reinterpret_cast<GpdInfoTitle*>(user->GetGpd(title_id));
  if (!title_gpd) {
    return;
  }

  auto title_data = user->dashboard_gpd_.GetTitleInfo(title_id);
  if (!title_data) {
    return;
  }

  title_data->achievements_count = title_gpd->GetAchievementCount();
  title_data->achievements_unlocked = title_gpd->GetUnlockedAchievementCount();
  title_data->gamerscore_total = title_gpd->GetTotalGamerscore();
  title_data->gamerscore_earned = title_gpd->GetGamerscore();

  user->WriteGpd(kDashboardID);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe