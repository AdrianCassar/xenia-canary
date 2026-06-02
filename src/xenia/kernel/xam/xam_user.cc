/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <ranges>

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_modules.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xsession.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/resources.h"
#include "xenia/xbox.h"

#include "third_party/stb/stb_image.h"

DECLARE_int32(user_language);
DECLARE_int32(user_country);

DECLARE_bool(offline_mode);

namespace xe {
namespace kernel {
namespace xam {

// XUserGetXUID = XamUserGetXUID(user_index, X_USER_XUID_OFFLINE |
// X_USER_XUID_ONLINE | X_USER_XUID_GUEST, xuid_ptr)
X_HRESULT_result_t XamUserGetXUID_entry(dword_t user_index, dword_t type_mask,
                                        lpqword_t xuid_ptr) {
  assert_true(type_mask == 1 || type_mask == 2 || type_mask == 3 ||
              type_mask == 4 || type_mask == 7);
  if (!xuid_ptr) {
    return X_E_INVALIDARG;
  }

  *xuid_ptr = 0;

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  uint32_t result = X_E_NO_SUCH_USER;
  uint64_t xuid = 0;

  if ((type_mask & X_USER_XUID_ONLINE)) {
    xuid = user_profile->GetLogonXUID();
    result = X_E_SUCCESS;
  } else if ((type_mask & X_USER_XUID_OFFLINE)) {
    xuid = user_profile->xuid();
    result = X_E_SUCCESS;
  }

  if (type_mask == X_USER_XUID_GUEST) {
    result = X_E_NO_SUCH_USER;
  }

  *xuid_ptr = xuid;
  return result;
}
DECLARE_XAM_EXPORT1(XamUserGetXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetIndexFromXUID_entry(qword_t xuid, dword_t flags,
                                             lpdword_t index) {
  if (!index) {
    return X_E_INVALIDARG;
  }

  const uint8_t user_index =
      kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(xuid);

  if (user_index == XUserIndexAny) {
    return X_E_NO_SUCH_USER;
  }

  *index = user_index;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetIndexFromXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetSigninState_entry(dword_t user_index) {
  X_USER_SIGNIN_STATE signin_state = X_USER_SIGNIN_STATE::NotSignedIn;
  if (user_index >= XUserMaxUserCount) {
    return static_cast<uint32_t>(signin_state);
  }

  if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);
    signin_state = user_profile->signin_state();
  }

  return static_cast<uint32_t>(signin_state);
}
DECLARE_XAM_EXPORT2(XamUserGetSigninState, kUserProfiles, kImplemented,
                    kHighFrequency);

X_HRESULT_result_t XamUserGetSigninInfo_entry(
    dword_t user_index, dword_t flags, pointer_t<X_USER_SIGNIN_INFO> info_ptr) {
  if (!info_ptr) {
    return X_E_INVALIDARG;
  }

  info_ptr.Zero();

  if (user_index >= XUserMaxUserCount) {
    return X_E_NO_SUCH_USER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  xe::string_util::copy_truncating(info_ptr->name, user_profile->name(),
                                   xe::countof(info_ptr->name));

  if (user_profile->IsLiveEnabled()) {
    info_ptr->flags |= X_USER_INFO_FLAG_LIVE_ENABLED;
  }

  // Online XUID if connected to Xbox Live, otherwise offline XUID
  // 434D0849, 4D5308AB pass XUID to XUserReadStats and XShowGamerCardUI
  if (!flags) {
    info_ptr->xuid = user_profile->GetLogonXUID();
  }

  // 415608CB joins systemlink session twice
  if (flags & X_USER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY) {
    info_ptr->xuid = user_profile->xuid();
  }

  // If (X_USER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY |
  // X_USER_GET_SIGNIN_INFO_ONLINE_XUID_ONLY) are provided return online XUID
  if (flags & X_USER_GET_SIGNIN_INFO_ONLINE_XUID_ONLY) {
    info_ptr->xuid = user_profile->GetOnlineXUID();
  }

  info_ptr->signin_state = static_cast<uint32_t>(user_profile->signin_state());

  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSigninInfo, kUserProfiles, kImplemented);

dword_result_t XamUserGetName_entry(dword_t user_index, dword_t buffer,
                                    dword_t buffer_len) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    // Based on XAM only first byte is cleared in case of lack of user.
    kernel_memory()->Zero(buffer, 1);
    return X_ERROR_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  // Because name is always limited to 15 characters we can assume length will
  // never exceed that limit.
  const auto& user_name = user_profile->name();

  // buffer_len includes null-terminator. user_name does not.
  const uint32_t bytes_to_copy = std::min(
      buffer_len.value(), static_cast<uint32_t>(user_name.length()) + 1);

  char* str_buffer = kernel_memory()->TranslateVirtual<char*>(buffer);
  xe::string_util::copy_truncating(str_buffer, user_name, bytes_to_copy);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetName, kUserProfiles, kImplemented);

dword_result_t XamUserGetGamerTag_entry(dword_t user_index, dword_t buffer,
                                        dword_t buffer_len) {
  if (!buffer || buffer_len < 16) {
    return X_E_INVALIDARG;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);
  auto user_name = xe::to_utf16(user_profile->name());

  char16_t* str_buffer = kernel_memory()->TranslateVirtual<char16_t*>(buffer);

  xe::string_util::copy_and_swap_truncating(
      str_buffer, user_name, std::min(buffer_len.value(), uint32_t(16)));
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetGamerTag, kUserProfiles, kImplemented);

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/xboxtools.cpp
uint32_t XamUserReadProfileSettingsEx(
    uint32_t title_id, uint32_t user_index, uint32_t xuid_count,
    be<uint64_t>* xuids, uint32_t setting_count, be<uint32_t>* setting_ids,
    uint32_t unused, be<uint32_t>* buffer_size_ptr, uint8_t* buffer,
    lpvoid_t overlapped_ptr) {
  // must have at least 1 to 32 settings
  if (setting_count < 1 || setting_count > 32) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // buffer size pointer must be valid
  if (!buffer_size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // if buffer size is non-zero, buffer pointer must be valid
  auto buffer_size = static_cast<uint32_t>(*buffer_size_ptr);
  if (buffer_size && !buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // Dashboard expects settings in order use vector to ensure insertion order
  // is maintained.
  const std::vector<uint32_t> settings_ids = {setting_ids,
                                              setting_ids + setting_count};

  // 454D07D2 reads settings from multiple XUIDs.
  const std::vector<uint64_t> profile_xuids = {xuids, xuids + xuid_count};

  uint32_t needed_header_size = 0;
  uint32_t needed_data_size = 0;
  for (const uint32_t setting_id : settings_ids) {
    if (!UserSetting::is_setting_valid(setting_id)) {
      if (!setting_id) {
        XELOGE("{} requested unknown setting {:08X}", __func__, setting_id);
      }
    }

    needed_header_size += sizeof(X_USER_PROFILE_SETTING);
    AttributeKey setting_key = {.value = setting_id};
    switch (static_cast<X_USER_DATA_TYPE>(setting_key.type)) {
      case X_USER_DATA_TYPE::WSTRING:
      case X_USER_DATA_TYPE::BINARY:
        needed_data_size += setting_key.size;
        break;
      default:
        break;
    }
  }
  if (xuids) {
    needed_header_size *= xuid_count;
    needed_data_size *= xuid_count;
  }
  needed_header_size += sizeof(X_USER_READ_PROFILE_SETTINGS);

  uint32_t needed_size = needed_header_size + needed_data_size;
  if (!buffer || buffer_size < needed_size) {
    if (!buffer_size) {
      *buffer_size_ptr = needed_size;
    }
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    extended_error = 0;
    length = 0;

    auto user_profile = kernel_state()->xam_state()->GetUserProfile(user_index);

    if (!user_profile && !xuids) {
      extended_error = X_E_NO_SUCH_USER;
      return X_ERROR_FUNCTION_FAILED;
    }

    const auto requested_settings_view =
        settings_ids | std::views::transform([](uint32_t id) {
          return static_cast<xam::UserSettingId>(id);
        });

    const std::vector<xam::UserSettingId> requested_settings_ids(
        requested_settings_view.begin(), requested_settings_view.end());

    user_settings_map remote_user_settings = {};
    user_settings_map local_user_settings = {};

    const uint32_t titleId = title_id ? title_id : kernel_state()->title_id();

    std::vector<uint64_t> total_profile_xuids = profile_xuids;

    // We split the settings into their respective title ids for lookup,
    // therefore we lose the setting ids order!
    const auto GetLocalUserSettings = [requested_settings_ids,
                                       titleId](UserProfile* user_profile) {
      std::map<uint32_t, std::vector<xam::UserSetting>> title_settings = {};

      if (!user_profile) {
        return title_settings;
      }

      for (const xam::UserSettingId setting_id : requested_settings_ids) {
        const uint32_t setting_id_val = static_cast<uint32_t>(setting_id);
        uint32_t setting_title_id = titleId;

        // If setting is not title specific then assume it belongs to the
        // dashboard.
        if (!xam::UserSetting::is_title_specific(
                static_cast<uint32_t>(setting_id_val))) {
          setting_title_id = kDashboardID;
        }

        const auto user_setting =
            kernel_state()->xam_state()->user_tracker()->GetSetting(
                user_profile, setting_title_id,
                static_cast<uint32_t>(setting_id_val));

        if (user_setting.has_value()) {
          title_settings[setting_title_id].push_back(user_setting.value());
        }
      }

      return title_settings;
    };

    if (!profile_xuids.empty()) {
      user_settingids_map remote_user_setting_ids = {};

      for (const uint64_t xuid : profile_xuids) {
        if (!kernel_state()->xam_state()->IsUserSignedIn(xuid) &&
            IsOnlineXUID(xuid)) {
          for (const xam::UserSettingId settings_id : requested_settings_ids) {
            uint32_t setting_title_id = titleId;

            // If setting is not title specific then assume it belongs to the
            // dashboard.
            if (!xam::UserSetting::is_title_specific(
                    static_cast<uint32_t>(settings_id))) {
              setting_title_id = kDashboardID;
            }

            remote_user_setting_ids[xuid][setting_title_id].push_back(
                settings_id);
          }
        } else {
          const auto user_profile =
              kernel_state()->xam_state()->GetUserProfileAny(xuid);

          const auto settings = GetLocalUserSettings(user_profile);

          if (user_profile) {
            local_user_settings[xuid] = settings;
          }
        }
      }

      if (!remote_user_setting_ids.empty()) {
        remote_user_settings =
            kernel_state()->GetXboxLiveAPI()->GetUsersSettings(
                remote_user_setting_ids);
      }
    } else {
      const auto user_profile =
          kernel_state()->xam_state()->GetUserProfile(user_index);

      const auto settings = GetLocalUserSettings(user_profile);

      if (user_profile) {
        total_profile_xuids.push_back(user_profile->xuid());
        local_user_settings[user_profile->xuid()] = settings;
      }
    }

    if (local_user_settings.empty() && remote_user_settings.empty()) {
      extended_error = X_E_NO_SUCH_USER;
      return X_ERROR_FUNCTION_FAILED;
    }

    // The order of xuids isn't preserved.
    user_settings_map users_settings = local_user_settings;
    users_settings.merge(remote_user_settings);

    auto out_header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS*>(buffer);

    // Maintain XUIDs and setting ids order.
    for (const uint64_t xuid : total_profile_xuids) {
      std::memset(out_header, 0, sizeof(X_USER_READ_PROFILE_SETTINGS));

      auto out_setting =
          reinterpret_cast<X_USER_PROFILE_SETTING*>(out_header + 1);

      std::fill_n(out_setting, setting_count, X_USER_PROFILE_SETTING{});

      out_header->setting_count =
          static_cast<uint32_t>(requested_settings_ids.size());
      out_header->settings_ptr = kernel_state()->memory()->HostToGuestVirtual(
          std::to_address(out_setting));

      uint32_t additional_data_buffer_ptr =
          out_header->settings_ptr +
          (setting_count * sizeof(X_USER_PROFILE_SETTING));

      // Maintain requested settings id order.
      for (const xam::UserSettingId setting_id : requested_settings_ids) {
        const uint32_t setting_id_val = static_cast<uint32_t>(setting_id);
        uint32_t setting_title_id = titleId;

        // If setting is not title specific then assume it belongs to the
        // dashboard.
        if (!xam::UserSetting::is_title_specific(
                static_cast<uint32_t>(setting_id_val))) {
          setting_title_id = kDashboardID;
        }

        std::vector<xam::UserSetting> settings = {};

        if (users_settings.contains(xuid)) {
          if (users_settings.at(xuid).contains(setting_title_id)) {
            settings = users_settings.at(xuid).at(setting_title_id);
          }
        }

        if (settings.empty()) {
          assert_always();

          XELOGI(fmt::format(
              "XamUserReadProfileSettingsEx: {:08X}: {:08X} not found!",
              setting_title_id, setting_id_val));
          continue;
        }

        auto it =
            std::find_if(settings.cbegin(), settings.cend(),
                         [&setting_id_val](xam::UserSetting setting) {
                           return setting_id_val == setting.get_setting_id();
                         });

        if (it == settings.cend()) {
          assert_always();

          XELOGI(fmt::format(
              "XamUserReadProfileSettingsEx: {:08X}: {:08X} not found!",
              setting_title_id, setting_id_val));
          continue;
        }

        xam::UserSetting setting = *it;

        out_setting->setting_id = setting.get_setting_id();
        out_setting->source = setting.get_setting_source();

        setting.WriteToGuest(out_setting, additional_data_buffer_ptr);

        if (xuids) {
          out_setting->xuid = xuid;
        } else {
          out_setting->user_index = user_index;
        }

        out_setting++;
      }

      // Next profile settings header
      out_header =
          kernel_memory()->TranslateVirtual<X_USER_READ_PROFILE_SETTINGS*>(
              additional_data_buffer_ptr);
    }

    return X_ERROR_SUCCESS;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    X_RESULT result = run(extended_error, length);

    return result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS
                                     : X_ERROR_FUNCTION_FAILED;
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}

dword_result_t XamUserReadProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    lpvoid_t buffer_ptr, lpvoid_t overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, 0,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserReadProfileSettingsEx_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    lpdword_t unkn_buffer_size_ptr, lpvoid_t buffer_ptr, lpvoid_t overlapped) {
  return XamUserReadProfileSettingsEx(
      title_id, user_index, xuid_count, xuids, setting_count, setting_ids, 0,
      buffer_size_ptr ? buffer_size_ptr : unkn_buffer_size_ptr, buffer_ptr,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettingsEx, kUserProfiles, kImplemented);

dword_result_t XamUserWriteProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t setting_count,
    pointer_t<X_USER_PROFILE_SETTING> settings, lpvoid_t overlapped) {
  if (!setting_count || !settings) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    bool was_avatar_setting_changed = false;
    const uint8_t user_index_bit = (1 << user_index) & 0xF;
    // Update and save settings.
    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);

    // Skip writing data about users with id != 0 they're not supported
    if (!user_profile) {
      extended_error = X_HRESULT_FROM_WIN32(X_ERROR_NO_SUCH_USER);
      length = 0;
      return X_ERROR_NO_SUCH_USER;
    }

    for (uint32_t n = 0; n < setting_count; ++n) {
      const UserSetting setting = UserSetting(&settings[n]);
      if (setting.get_setting_id() ==
          static_cast<uint32_t>(
              UserSettingId::XPROFILE_GAMERCARD_AVATAR_INFO_1)) {
        was_avatar_setting_changed = true;
      }

      if (!setting.is_valid_type()) {
        continue;
      }

      kernel_state()->xam_state()->user_tracker()->UpsertSetting(
          user_profile->xuid(), title_id, &setting);
    }

    kernel_state()->BroadcastNotification(
        kXNotificationSystemProfileSettingChanged, user_index_bit);
    if (was_avatar_setting_changed) {
      kernel_state()->BroadcastNotification(kXNotificationSystemAvatarChanged,
                                            user_index_bit);
    }

    extended_error = X_HRESULT_FROM_WIN32(X_STATUS_SUCCESS);
    length = 0;
    return X_STATUS_SUCCESS;
  };

  if (!overlapped) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamUserWriteProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserCheckPrivilege_entry(dword_t user_index, dword_t mask,
                                           lpdword_t out_value) {
  // 41560855 - XUserGetAccountInfoCheck
  // XPRIVILEGE_PII_ACCESS == 221

  *out_value = 0;

  if (user_index == XUserIndexAny) {
    for (uint8_t i = 0; i < XUserMaxUserCount; ++i) {
      const auto result = XamUserCheckPrivilege_entry(i, mask, out_value);
      if (result != X_ERROR_NO_SUCH_USER) {
        return result;
      }
    }

    return X_ERROR_NO_SUCH_USER;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (kernel_state()->xam_state()->GetUserProfile(user_index)->signin_state() !=
      X_USER_SIGNIN_STATE::SignedInToLive) {
    return X_ERROR_NOT_LOGGED_ON;
  }

  // Check Permissions settings in Dashboard GPD?

  // Allow all privileges including multiplayer.
  *out_value = 1;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCheckPrivilege, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetFlags_entry(dword_t user_index,
                                                       lpdword_t out_flags) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  // No restrictions?
  *out_flags = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetFlags, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetRating_entry(dword_t user_index,
                                                        dword_t unk1,
                                                        lpdword_t out_unk2,
                                                        lpdword_t out_unk3) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  // Some games have special case paths for 3F that differ from the failure
  // path, so my guess is that's 'don't care'.
  *out_unk2 = 0x3F;
  *out_unk3 = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetRating, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionCheckAccess_entry(
    dword_t user_index, dword_t unk1, dword_t unk2, dword_t unk3, dword_t unk4,
    lpdword_t out_unk5, dword_t overlapped_ptr) {
  *out_unk5 = 1;

  if (overlapped_ptr) {
    // TODO(benvanik): does this need the access arg on it?
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionCheckAccess, kUserProfiles, kStub);

dword_result_t XamUserIsOnlineEnabled_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount) {
    return 0;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return 0;
  }

  return kernel_state()
      ->xam_state()
      ->GetUserProfile(user_index)
      ->IsLiveEnabled();
}
DECLARE_XAM_EXPORT1(XamUserIsOnlineEnabled, kUserProfiles, kImplemented);

dword_result_t XamUserGetMembershipTier_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount) {
    return X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierNone;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierNone;
  }

  return kernel_state()
      ->xam_state()
      ->GetUserProfile(user_index)
      ->GetSubscriptionTier();
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTier, kUserProfiles, kImplemented);

dword_result_t XamUserGetMembershipTierFromXUID_entry(qword_t xuid) {
  const auto profile = kernel_state()->xam_state()->GetUserProfileAny(xuid);
  if (!profile) {
    return X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierNone;
  }

  return profile->GetSubscriptionTier();
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTierFromXUID, kUserProfiles,
                    kImplemented);

dword_result_t XamUserAreUsersFriends_entry(
    dword_t user_index, lpqword_t xuids_ptr, dword_t xuids_count,
    lpdword_t are_friends_ptr, pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  X_RESULT result = X_ERROR_SUCCESS;
  bool are_friends = false;

  // 415607D2 provides are_friends_ptr and overlapped_ptr possibly a bug?
  assert_true(!overlapped_ptr);

  if (are_friends_ptr) {
    *are_friends_ptr = 0;
  }

  if (user_index >= XUserMaxUserCount) {
    result = X_ERROR_INVALID_PARAMETER;
  } else {
    if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile =
          kernel_state()->xam_state()->GetUserProfile(user_index);

      if (user_profile->signin_state() != X_USER_SIGNIN_STATE::SignedInToLive) {
        result = X_ERROR_NOT_LOGGED_ON;
      } else {
        uint32_t friends_count = 0;

        for (uint32_t i = 0; i < xuids_count; i++) {
          // xuid is 0 sometimes?
          uint64_t xuid = xuids_ptr[i];

          if (user_profile->IsFriend(xuid)) {
            friends_count++;
          }
        }

        are_friends = friends_count == xuids_count;
      }
    } else {
      result = X_ERROR_NO_SUCH_USER;
    }
  }

  if (overlapped_ptr) {
    assert_true(!are_friends_ptr);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped_ptr,
        result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED,
        X_HRESULT_FROM_WIN32(result), are_friends);

    return X_ERROR_IO_PENDING;
  }

  if (!overlapped_ptr && are_friends_ptr) {
    *are_friends_ptr = are_friends;
  }

  return result;
}
DECLARE_XAM_EXPORT1(XamUserAreUsersFriends, kUserProfiles, kImplemented);

dword_result_t XamUserGetAgeGroup_entry(
    dword_t user_index, lpdword_t age_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (!age_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  auto run = [user_index, age_ptr, overlapped_ptr](
                 uint32_t& extended_error, uint32_t& length) -> X_RESULT {
    X_RESULT result = X_ERROR_SUCCESS;

    *age_ptr = X_USER_AGE_GROUP::ADULT;

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;

    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamUserGetAgeGroup, kUserProfiles, kImplemented);

// 454109D0
dword_result_t XamUserGetAge_entry(dword_t user_index, lpdword_t age_ptr,
                                   pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (!age_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (!xboxkrnl::XexCheckExecutablePrivilege(XEX_PRIVILEGE_PII_ACCESS)) {
    return X_ONLINE_E_ACCESS_DENIED;
  }

  auto run = [user_index, age_ptr, overlapped_ptr](
                 uint32_t& extended_error, uint32_t& length) -> X_RESULT {
    X_RESULT result = X_ERROR_SUCCESS;

    *age_ptr = 0;

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;

    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamUserGetAge, kUserProfiles, kImplemented);

dword_result_t XamUserCreateAchievementEnumerator_entry(
    dword_t title_id, dword_t user_index, qword_t xuid, dword_t flags,
    dword_t offset, dword_t count, lpdword_t buffer_size_ptr,
    lpdword_t handle_ptr) {
  if (!count || !buffer_size_ptr || !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  size_t entry_size = sizeof(X_ACHIEVEMENT_DETAILS);
  if (flags & 7) {
    entry_size += X_ACHIEVEMENT_DETAILS::kStringBufferSize;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = static_cast<uint32_t>(entry_size * count);
  }

  auto e = object_ref<XAchievementEnumerator>(
      new XAchievementEnumerator(kernel_state(), count, offset, flags));
  auto result = e->Initialize(user_index, 0xFB, 0xB000A, 0xB000B, 0);
  if (XFAILED(result)) {
    return result;
  }

  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  uint64_t requester_xuid = user->xuid();

  // 58410B63 and 4D530860 use online XUID for local signed-in user
  if (xuid && user->GetOnlineXUID() != xuid) {
    requester_xuid = xuid;
  }

  const uint32_t title_id_ =
      title_id ? static_cast<uint32_t>(title_id) : kernel_state()->title_id();

  const auto user_title_achievements =
      kernel_state()->achievement_manager()->GetTitleAchievements(
          requester_xuid, title_id_);

  if (user_title_achievements.empty()) {
    return X_ERROR_INVALID_PARAMETER;
  }

  for (const auto& entry : user_title_achievements) {
    auto unlock_time = X_FILETIME();
    if (entry.IsUnlocked() && entry.unlock_time.is_valid()) {
      unlock_time = entry.unlock_time;
    }

    auto item = AchievementDetails(
        entry.achievement_id, entry.achievement_name.c_str(),
        entry.unlocked_description.c_str(), entry.locked_description.c_str(),
        entry.image_id, entry.gamerscore, unlock_time, entry.flags);

    e->AppendItem(item);
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateAchievementEnumerator, kUserProfiles,
                    kSketchy);

dword_result_t XamUserCreateTitlesPlayedEnumerator_entry(
    dword_t title_id, dword_t user_index, qword_t xuid, dword_t starting_index,
    dword_t game_count, lpdword_t buffer_size_ptr, lpdword_t handle_ptr) {
  if (user_index >= XUserMaxUserCount && game_count != 0 && !buffer_size_ptr &&
      !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const uint32_t kEntrySize = sizeof(XTitleEnumerator::XTITLE_PLAYED);
  if (buffer_size_ptr) {
    *buffer_size_ptr = kEntrySize * game_count;
  }

  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto e = object_ref<XTitleEnumerator>(
      new XTitleEnumerator(kernel_state(), game_count));
  auto result =
      e->Initialize(user_index, 0xFB, 0xB0050, 0xB000B, 0x20, game_count, 0);
  if (XFAILED(result)) {
    return result;
  }

  const auto user_titles =
      kernel_state()->xam_state()->user_tracker()->GetPlayedTitles(
          user->xuid());

  if (!user_titles.empty()) {
    for (const auto& title : user_titles) {
      if (title.id == kDashboardID) {
        continue;
      }
      if (!title.achievements_count || !title.gamerscore_amount) {
        continue;
      }
      e->AppendItem(title);
    }
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateTitlesPlayedEnumerator, kUserProfiles, kStub);

dword_result_t XamReadTile_entry(dword_t tile_type, dword_t title_id,
                                 qword_t item_id, dword_t user_index,
                                 lpdword_t output_ptr,
                                 lpdword_t buffer_size_ptr,
                                 lpvoid_t overlapped_ptr) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    user = kernel_state()->xam_state()->GetUserProfileAny(item_id);
    if (!user) {
      return X_ERROR_INVALID_PARAMETER;
    }
  }

  if (!buffer_size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    std::span<const uint8_t> tile =
        kernel_state()->xam_state()->user_tracker()->GetIcon(
            user->xuid(), title_id, static_cast<XTileType>(tile_type.value()),
            item_id);

    auto result = X_ERROR_SUCCESS;

    if (tile.empty()) {
      result = X_ERROR_FILE_NOT_FOUND;
    }

    *buffer_size_ptr = static_cast<uint32_t>(tile.size());

    if (output_ptr) {
      memcpy(output_ptr, tile.data(), tile.size());
    } else {
      result = X_ERROR_INSUFFICIENT_BUFFER;
    }

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;
    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamReadTile, kUserProfiles, kSketchy);

dword_result_t XamReadTileEx_entry(dword_t tile_type, dword_t game_id,
                                   qword_t item_id, dword_t offset,
                                   dword_t unk1, dword_t unk2,
                                   lpdword_t output_ptr,
                                   lpdword_t buffer_size_ptr) {
  return XamReadTile_entry(tile_type, game_id, item_id, offset, output_ptr,
                           buffer_size_ptr, 0);
}
DECLARE_XAM_EXPORT1(XamReadTileEx, kUserProfiles, kSketchy);

dword_result_t XamParseGamerTileKey_entry(pointer_t<X_USER_DATA> key_ptr,
                                          lpdword_t title_id_ptr,
                                          lpdword_t big_tile_id_ptr,
                                          lpdword_t small_tile_id_ptr) {
  if (!key_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (key_ptr->type != X_USER_DATA_TYPE::WSTRING) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (key_ptr->data.unicode.size > 0x64) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!key_ptr->data.unicode.ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const std::string tile_key = xe::to_utf8(string_util::read_u16string_and_swap(
      kernel_memory()->TranslateVirtual<const char16_t*>(
          key_ptr->data.unicode.ptr)));

  if (tile_key.empty() || tile_key.size() != sizeof(GamerPictureKey)) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const bool is_valid_hex_string =
      std::all_of(tile_key.cbegin(), tile_key.cend(),
                  [](unsigned char c) { return std::isxdigit(c); });

  // If there's no valid key then set a default one?

  if (!is_valid_hex_string) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const GamerPictureKey* gamer_picture_key =
      reinterpret_cast<const GamerPictureKey*>(tile_key.data());

  if (title_id_ptr) {
    *title_id_ptr = gamer_picture_key->GetTitleId();
  }

  if (big_tile_id_ptr) {
    *big_tile_id_ptr = gamer_picture_key->GetBigTileId();
  }

  if (small_tile_id_ptr) {
    *small_tile_id_ptr = gamer_picture_key->GetSmallTileId();
  }

  if (title_id_ptr) {
    if (IsGamerPictureKeySet(*title_id_ptr)) {
      // Set default gamer picture key?
    }
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamParseGamerTileKey, kUserProfiles, kImplemented);

dword_result_t XamReadTileToTextureEx_entry(
    dword_t tile_type, dword_t title_id, qword_t tile_id, dword_t user_index,
    dword_t fsmall, pointer_t<X_USER_DATA> key_ptr, lpvoid_t buffer_ptr,
    dword_t stride, dword_t tile_height, dword_t overlapped_ptr) {
  if (!buffer_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    extended_error = X_ERROR_SUCCESS;
    length = 0;

    XTileType xtile_type = static_cast<XTileType>(tile_type.value());

    // 5454084E
    size_t valid_tile_height = std::min<uint32_t>(64, tile_height);

    assert_false(tile_height > 64);

    if (fsmall) {
      valid_tile_height = std::min<uint32_t>(32, tile_height);
    }

    const size_t buffer_size = size_t(stride) * valid_tile_height;

    std::vector<uint8_t> gamerpic_icon = {};

    // 5454084E
    if (user_index == -1) {
      extended_error = X_E_NO_SUCH_USER;
      return X_ERROR_FUNCTION_FAILED;
    }

    // Local user
    if (user_index < XUserMaxUserCount) {
      auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
      if (!user) {
        // 5841091E expects failure
        extended_error = X_E_NO_SUCH_USER;
        return X_ERROR_FUNCTION_FAILED;
      }

      if (IsGamerPictureAvatar(title_id)) {
        if (fsmall) {
          xtile_type = XTileType::kAvatarGamerTileSmall;
        } else {
          xtile_type = XTileType::kAvatarGamerTile;
        }
      } else {
        // 434D0849
        if (fsmall) {
          xtile_type = XTileType::kGamerTileSmall;
        }
      }

      const auto tile_icon =
          kernel_state()->xam_state()->user_tracker()->GetIcon(
              user->xuid(), title_id, xtile_type, tile_id);

      if (!tile_icon.empty()) {
        gamerpic_icon.assign(tile_icon.begin(), tile_icon.end());
      } else {
        // Profile does not have a gamerpic.
      }
    } else if (user_index == XUserIndexNone) {
      // Remote user

      xe::be<uint32_t> title_id_ = 0;
      xe::be<uint32_t> big_tile_id = 0;
      xe::be<uint32_t> small_tile_id = 0;

      if (key_ptr) {
        // XamReadTileToTextureEx
        XamParseGamerTileKey_entry(key_ptr, &title_id_.value,
                                   &big_tile_id.value, &small_tile_id.value);
      } else {
        // XamReadTileToTexture
        // 534507D4 - Doesn't redirect to Ex version even with supported XAM.
        title_id_ = title_id.value();
        big_tile_id = static_cast<uint32_t>(tile_id);
      }

      const uint32_t gamerpic_id = fsmall ? small_tile_id : big_tile_id;

      if (!IsGamerPictureAvatar(title_id_) &&
          !IsGamerPictureCustom(title_id_)) {
        const auto gamerpic_data =
            kernel_state()->GetXboxLiveAPI()->GetCachedGamerpic(gamerpic_id);

        if (gamerpic_data.has_value()) {
          gamerpic_icon = gamerpic_data.value();
        } else {
          gamerpic_icon =
              kernel_state()->GetXboxLiveAPI()->DownloadGamerpicTile(
                  title_id_, gamerpic_id);

          if (!gamerpic_icon.empty()) {
            kernel_state()->GetXboxLiveAPI()->AddCachedGamerpic(gamerpic_id,
                                                                gamerpic_icon);
          }
        }
      } else {
        // We do not support avatar or custom gamerpics.
        // If remote user is local we still cannot provide gamerpic as we would
        // need to determine the user profile from the gamerpic key.
      }
    }

    std::fill_n(reinterpret_cast<uint8_t*>(buffer_ptr.host_address()),
                buffer_size, 0);

    if (gamerpic_icon.empty()) {
      std::span<uint8_t> black_texture = std::span<uint8_t>(
          reinterpret_cast<uint8_t*>(buffer_ptr.host_address()), buffer_size);

      // Create a solid black texture
      uint32_t count = 0;
      std::generate(black_texture.begin(), black_texture.end(),
                    [&count]() { return (count++ % 4 == 0) ? 0xFF : 0x00; });

      return X_ERROR_SUCCESS;
    }

    int width, height, channels;
    unsigned char* imageData = stbi_load_from_memory(
        gamerpic_icon.data(), static_cast<int>(gamerpic_icon.size()), &width,
        &height, &channels, STBI_rgb_alpha);

    const size_t icon_dimmension_size = size_t(width) * size_t(height);
    for (int i = 0; i < icon_dimmension_size; i++) {
      unsigned char* pixel = &imageData[i * sizeof(uint32_t)];

      // RGBA to ARGB. TODO: Find faster method!
      // RGBA->AGBR
      std::swap(pixel[0], pixel[3]);
      // AGBR->ARBG
      std::swap(pixel[1], pixel[3]);
      // ARBG->ARGB
      std::swap(pixel[2], pixel[3]);
    }

    const size_t row_size_bytes = width * sizeof(uint32_t);
    std::vector<uint8_t> final_tile(buffer_size, 0);

    /*
     Process image rows to include stride padding

     Row (32px) = 128 Bytes
     Stride = 256 Bytes
     Padding Bytes = Stride - Row
    */
    for (int y = 0; y < height; ++y) {
      const unsigned char* src_row_start = &imageData[y * row_size_bytes];
      uint8_t* dest_row_start = final_tile.data() + (y * stride);

      memcpy(dest_row_start, src_row_start, row_size_bytes);
    }

    memcpy(buffer_ptr, final_tile.data(), buffer_size);

    stbi_image_free(imageData);

    return X_ERROR_SUCCESS;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    X_RESULT result = run(extended_error, length);

    return result;
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamReadTileToTextureEx, kUserProfiles, kSketchy);

dword_result_t XamReadTileToTexture_entry(dword_t tile_type, dword_t title_id,
                                          qword_t tile_id, dword_t user_index,
                                          lpvoid_t buffer_ptr, dword_t stride,
                                          dword_t tile_height,
                                          dword_t overlapped_ptr) {
  return XamReadTileToTextureEx_entry(tile_type, title_id, tile_id, user_index,
                                      false, nullptr, buffer_ptr, stride,
                                      tile_height, overlapped_ptr);
}
DECLARE_XAM_EXPORT1(XamReadTileToTexture, kUserProfiles, kSketchy);

// Alias XUserAwardGamerPicture
dword_result_t XamWriteGamerTile_entry(
    dword_t user_index, dword_t title_id, dword_t big_tile_id,
    dword_t small_tile_id, dword_t for_enumerate,
    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  // What is 0x10 flag?
  const uint32_t flags = (for_enumerate != 0 ? 0 : 0x10) | 1;

  const WriteTileType tile_type = static_cast<WriteTileType>((flags & 0xF));

  auto WriteGamerTileByKey = [=](uint32_t& extended_error, uint32_t& length) {
    extended_error = X_ERROR_SUCCESS;
    length = 0;

    auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
    if (!user) {
      extended_error = X_E_INVALIDARG;
      return X_ERROR_FUNCTION_FAILED;
    }

    const uint32_t content_title_id =
        title_id ? title_id.value() : kernel_state()->title_id();

    const std::string gamerpic_key =
        fmt::format("{:08x}{:08x}{:08x}", content_title_id, big_tile_id.value(),
                    small_tile_id.value());

    const std::string big_gamerpic_filename =
        fmt::format("64_{}.png", gamerpic_key);

    const std::string small_gamerpic_filename =
        fmt::format("32_{}.png", gamerpic_key);

    const std::string common_content_str = fmt::format("{:016X}", 0);
    const std::string content_type_str =
        fmt::format("{:08X}", uint32_t(XContentType::kGamerPicture));
    const std::string content_title_id_str =
        fmt::format("{:08x}", content_title_id);

    const std::filesystem::path gamer_pictures_storage_path =
        kernel_state()->emulator()->content_root() / common_content_str /
        kDashboardStringID / content_type_str / content_title_id_str;

    const std::filesystem::path big_gamerpic_path =
        gamer_pictures_storage_path / big_gamerpic_filename;

    const std::filesystem::path small_gamerpic_path =
        gamer_pictures_storage_path / small_gamerpic_filename;

    const auto gamerpic_big_png =
        kernel_state()->xam_state()->spa_info()->GetIcon(big_tile_id);

    const auto gamerpic_small_png =
        kernel_state()->xam_state()->spa_info()->GetIcon(small_tile_id);

    const std::error_code ec =
        xe::filesystem::CreateFolder(gamer_pictures_storage_path);

    FILE* big_gamerpic_file = xe::filesystem::OpenFile(big_gamerpic_path, "ab");
    FILE* small_gamerpic_file =
        xe::filesystem::OpenFile(small_gamerpic_path, "ab");

    X_RESULT result = X_ERROR_SUCCESS;

    if (ec || !big_gamerpic_file || !small_gamerpic_file) {
      extended_error = X_E_FUNCTION_FAILED;
      return X_ERROR_FUNCTION_FAILED;
    }

    fwrite(gamerpic_big_png.data(), 1, gamerpic_big_png.size(),
           big_gamerpic_file);
    fclose(big_gamerpic_file);

    fwrite(gamerpic_small_png.data(), 1, gamerpic_small_png.size(),
           small_gamerpic_file);
    fclose(small_gamerpic_file);

    XELOGI("Player: {} Unlocked Gamerpic: {}", user->name(),
           big_gamerpic_filename);

    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    X_RESULT result = WriteGamerTileByKey(extended_error, length);

    return result == X_ERROR_SUCCESS ? result : extended_error;
  }

  kernel_state()->CompleteOverlappedDeferredEx(WriteGamerTileByKey,
                                               overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamWriteGamerTile, kUserProfiles, kSketchy);

dword_result_t XamSessionCreateHandle_entry(lpdword_t handle_ptr) {
  auto e = object_ref<XSession>(new XSession(kernel_state()));
  auto result = (uint32_t)e->Initialize();
  if (XFAILED(result)) {
    return result;
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionCreateHandle, kUserProfiles, kImplemented);

dword_result_t XamSessionRefObjByHandle_entry(dword_t handle,
                                              lpdword_t obj_ptr) {
  auto object = kernel_state()->object_table()->LookupObject<XSession>(handle);
  if (!object) {
    return X_STATUS_INVALID_HANDLE;
  }

  object->RetainHandle();

  *obj_ptr = (uint32_t)object->guest_object();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionRefObjByHandle, kUserProfiles, kImplemented);

dword_result_t XamUserIsUnsafeProgrammingAllowed_entry(dword_t user_index,
                                                       dword_t unk,
                                                       lpdword_t result_ptr) {
  if (!result_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index != XUserIndexAny && user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // uint32_t result = XamUserCheckPrivilege_entry(user_index, 0xD4u,
  // result_ptr);

  *result_ptr = 1;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserIsUnsafeProgrammingAllowed, kUserProfiles, kStub);

dword_result_t XamUserGetSubscriptionType_entry(dword_t user_index,
                                                lpdword_t subscription_ptr,
                                                lpdword_t r5,
                                                dword_t overlapped_ptr) {
  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!subscription_ptr || !r5) {
    return X_E_INVALIDARG;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  *subscription_ptr = user->GetSubscriptionTier();
  *r5 = 0x0;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSubscriptionType, kUserProfiles, kStub);

dword_result_t XamUserGetCachedUserFlags_entry(dword_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return 0;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetCachedUserFlags, kUserProfiles, kImplemented);

dword_result_t XamUserGetUserFlags_entry(dword_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return 0;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetUserFlags, kUserProfiles, kImplemented);

dword_result_t XamUserGetUserFlagsFromXUID_entry(qword_t xuid) {
  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfileAny(xuid);
  if (!user_profile) {
    return 0;
  }

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetUserFlagsFromXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetOnlineLanguageFromXUID_entry(qword_t xuid) {
  const auto& user = kernel_state()->xam_state()->GetUserProfileAny(xuid);
  if (!user) {
    return kernel_state()->xconfig()->ReadSetting<uint32_t>(
        XCONFIG_USER_CATEGORY, XCONFIG_USER_LANGUAGE);
  }
  return user->GetLanguage();
}
DECLARE_XAM_EXPORT1(XamUserGetOnlineLanguageFromXUID, kUserProfiles,
                    kImplemented);

dword_result_t XamUserGetOnlineCountryFromXUID_entry(qword_t xuid) {
  const auto& user = kernel_state()->xam_state()->GetUserProfileAny(xuid);
  if (!user) {
    return kernel_state()->xconfig()->ReadSetting<uint8_t>(
        XCONFIG_USER_CATEGORY, XCONFIG_USER_COUNTRY);
  }
  return user->GetCountry();
}
DECLARE_XAM_EXPORT1(XamUserGetOnlineCountryFromXUID, kUserProfiles,
                    kImplemented);

dword_result_t XamUserIsParentalControlled_entry(dword_t user_index) {
  /* Notes:
      - if (data_address < 1 || XamExecutingOnBehalfOfTitle == 0 || title id ==
     kDashboardID || user_type != offline) (type mask used in XamUserGetXUID) go
     forward else return false
  */
  const auto& user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user || !user->IsParentalControlled()) {
    return false;
  }
  return true;
}
DECLARE_XAM_EXPORT1(XamUserIsParentalControlled, kUserProfiles, kImplemented);

dword_result_t XamUserCreateStatsEnumerator_entry(
    dword_t title_id, dword_t enumerator_type, qword_t pivot_user,
    dword_t num_rows, dword_t num_stats_specs,
    pointer_t<X_USER_STATS_SPEC> stats_ptr, lpdword_t buffer_size_ptr,
    lpdword_t handle_ptr) {
  assert_false(enumerator_type > X_STATS_ENUMERATOR_TYPE::BY_RATING);

  const uint32_t title_id_ =
      title_id ? title_id.value() : kernel_state()->title_id();

  if (!handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  *handle_ptr = 0;

  if (!buffer_size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  *buffer_size_ptr = 0;

  if (!pivot_user || !stats_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!num_rows || num_rows > X_STATS_MAX_ROW_COUNT) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!num_stats_specs) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (enumerator_type > X_STATS_ENUMERATOR_TYPE::BY_RATING) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto e = new XStaticEnumerator<X_USER_STATS_READ_RESULTS>(kernel_state(), 1);

  const X_STATUS result =
      e->Initialize(XUserIndexNone, 0xFB, 0xB0023, 0xB0024, 0);

  if (XFAILED(result)) {
    return result;
  }

  const X_STATS_ENUMERATOR_TYPE type =
      static_cast<X_STATS_ENUMERATOR_TYPE>(enumerator_type.value());

  uint64_t xuid = 0;
  uint32_t start_rank = 0;
  uint64_t start_i64rating = 0;

  switch (type) {
    case X_STATS_ENUMERATOR_TYPE::XUID: {
      xuid = pivot_user;
      XELOGI("StatsEnumeratorByXUID: {:016X}", xuid);
    } break;
    case X_STATS_ENUMERATOR_TYPE::RANK: {
      // 58410826 expects row ranks to start at rank_start.
      start_rank = pivot_user & 0xFFFF;
      XELOGI("StatsEnumeratorByRank at rank start {}", start_rank);
    } break;
    case X_STATS_ENUMERATOR_TYPE::RANK_PER_SPEC: {
      start_rank = pivot_user & 0xFFFF;
      XELOGI("StatsEnumeratorByRankPreSpec");
    } break;
    case X_STATS_ENUMERATOR_TYPE::BY_RATING: {
      start_i64rating = pivot_user;
      XELOGI("StatsEnumeratorByRating at i64Rating of {:016X}",
             start_i64rating);
    } break;
  }

  const uint32_t page_size =
      kernel_state()->memory()->GetPhysicalHeap()->page_size();

  // sizeof(X_USER_STATS_VIEW) becomes page_size of 4096.
  const uint32_t view_address =
      kernel_state()->memory()->SystemHeapAlloc(page_size);

  X_USER_STATS_VIEW* views_ptr =
      kernel_state()->memory()->TranslateVirtual<X_USER_STATS_VIEW*>(
          view_address);

  uint32_t rows = num_rows.value();

  uint32_t total_rows_size = 0;
  uint32_t total_columns_size = 0;

  // Tell game we have no rows to display
  rows = 0;

  const X_USER_STATS_SPEC* stat_specs_ptr = stats_ptr;

  for (size_t view_index = 0; view_index < num_stats_specs; view_index++) {
    const X_USER_STATS_SPEC& stat_spec_ptr = stat_specs_ptr[view_index];
    X_USER_STATS_VIEW& view_ptr = views_ptr[view_index];
    const uint32_t view_id = stat_spec_ptr.view_id;

    const auto spa_stats_view =
        kernel_state()->emulator()->game_info_database()->GetStatsView(view_id);

    if (IsTrueSkillViewID(view_id)) {
      XELOGI("TrueSkill View ID: {:08X}", view_id);
    }

    // 4B5607E8 expects view id otherwise crashes.
    view_ptr.view_id = view_id;
    view_ptr.total_view_rows = rows;
    view_ptr.num_rows = rows;

    // 545107D1 wants this set to prevent XUserReadStats
    // from crashing?
    // view_ptr->num_rows = num_rows.value();

    const uint32_t rows_size = sizeof(X_USER_STATS_ROW) * rows;

    total_rows_size += rows_size;

    const uint32_t rows_address =
        kernel_state()->memory()->SystemHeapAlloc(rows_size);

    X_USER_STATS_ROW* rows_ptr =
        kernel_state()->memory()->TranslateVirtual<X_USER_STATS_ROW*>(
            rows_address);

    // 584111FA and 5841089F want rows pointer even if row count is 0 to prevent
    // crashing.
    view_ptr.rows_ptr = rows_address;

    for (uint32_t row_index = 0; row_index < rows; row_index++) {
      X_USER_STATS_ROW& row_ptr = rows_ptr[row_index];

      const uint32_t entry_count = row_index + 1;

      // Dummy players
      const std::string gamertag = fmt::format("Xenia User {}", entry_count);
      xe::string_util::copy_truncating(row_ptr.gamertag, gamertag.c_str(),
                                       sizeof(row_ptr.gamertag));

      row_ptr.rank = entry_count;
      row_ptr.i64Rating = entry_count;

      row_ptr.xuid =
          kernel_state()->xam_state()->profile_manager()->GenerateXuidOnline();

      if (!stat_spec_ptr.num_column_ids) {
        continue;
      }

      const uint32_t columns_count = stat_spec_ptr.num_column_ids;
      const uint32_t columns_size = sizeof(X_USER_STATS_COLUMN) * columns_count;

      const uint32_t columns_address =
          kernel_state()->memory()->SystemHeapAlloc(columns_size);

      X_USER_STATS_COLUMN* columns_ptr =
          kernel_state()->memory()->TranslateVirtual<X_USER_STATS_COLUMN*>(
              columns_address);

      total_columns_size += columns_size;

      row_ptr.num_columns = columns_count;
      row_ptr.columns_ptr = columns_address;

      for (size_t column_index = 0; column_index < columns_count;
           column_index++) {
        X_USER_STATS_COLUMN& column_ptr = columns_ptr[column_index];
        const uint32_t column_id = stat_spec_ptr.column_ids[column_index];

        column_ptr.column_id = column_id;
        column_ptr.value.data = {};

        // Determine the property type
        if (IsTrueSkillViewID(view_id)) {
          column_ptr.value.type = GetTrueSkillColumnType(column_ptr.column_id);
        } else {
          if (spa_stats_view.has_value()) {
            for (const auto& column :
                 spa_stats_view.value().shared_view.column_entries) {
              if (column.attribute_id == column_id) {
                column_ptr.value.type =
                    xam::UserData::get_type(column.property_id);
              }
            }
          } else {
            column_ptr.value.type = xam::X_USER_DATA_TYPE::INT32;
            assert_always();
          }
        }
      }
    }
  }

  X_USER_STATS_READ_RESULTS* results = e->AppendItem();

  results->num_views = num_stats_specs.value();
  results->views_ptr = view_address;

  *buffer_size_ptr = sizeof(X_USER_STATS_READ_RESULTS) +
                     (num_stats_specs * sizeof(X_USER_STATS_VIEW)) +
                     total_rows_size + total_columns_size;

  assert_false(*buffer_size_ptr == 0);

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateStatsEnumerator, kUserProfiles, kSketchy);

dword_result_t XamUserGetUserTenure_entry(dword_t user_index,
                                          lpdword_t tenure_level_ptr,
                                          lpdword_t milestone_ptr,
                                          lpqword_t milestone_date_ptr,
                                          dword_t overlap_ptr) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  if (const auto setting =
          kernel_state()->xam_state()->user_tracker()->GetSetting(
              user_profile, kDashboardID,
              static_cast<uint32_t>(UserSettingId::XPROFILE_TENURE_LEVEL))) {
    *tenure_level_ptr = std::get<int32_t>(setting->get_host_data());
  }

  if (const auto setting =
          kernel_state()->xam_state()->user_tracker()->GetSetting(
              user_profile, kDashboardID,
              static_cast<uint32_t>(
                  UserSettingId::XPROFILE_TENURE_MILESTONE))) {
    *milestone_ptr = std::get<int32_t>(setting->get_host_data());
  }

  if (const auto setting =
          kernel_state()->xam_state()->user_tracker()->GetSetting(
              user_profile, kDashboardID,
              static_cast<uint32_t>(
                  UserSettingId::XPROFILE_TENURE_NEXT_MILESTONE_DATE))) {
    *milestone_date_ptr = std::get<int64_t>(setting->get_host_data());
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetUserTenure, kUserProfiles, kImplemented);

// https://github.com/TeaModz/XeLiveStealth-Full-Source/blob/d4a7439ac6241c4a13e883a6f156623d1c08f6eb/XeLive/Utils.cpp#L416
dword_result_t XamUserLogon_entry(lpqword_t xuids_ptr, dword_t flags,
                                  pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  const auto host_xuids_ptr =
      kernel_memory()->TranslateVirtual<xe::be<uint64_t>*>(xuids_ptr);

  const auto xuids = std::vector<xe::be<uint64_t>>(
      host_xuids_ptr, host_xuids_ptr + XUserMaxUserCount);

  if (!xuids_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [xuids, flags](uint32_t& extended_error,
                            uint32_t& length) -> X_RESULT {
    auto const profile_manager = kernel_state()->xam_state()->profile_manager();

    X_STATUS result = X_ERROR_SUCCESS;

    if (flags & static_cast<uint32_t>(UserLogonFlags::AddUser)) {
      for (auto& xuid : xuids) {
        if (xuid) {
          profile_manager->Login(xuid, XUserIndexAny, true);
        }
      }
    }

    if (flags & static_cast<uint32_t>(UserLogonFlags::RemoveUser)) {
      for (auto& xuid : xuids) {
        const uint8_t assigned_index =
            profile_manager->GetUserIndexAssignedToProfile(xuid);

        if (xuid) {
          if (kernel_state()->xam_state()->IsUserSignedIn(xuid)) {
            profile_manager->Logout(assigned_index, true);
          }
        }
      }
    }

    // Log everyone out
    if (flags & static_cast<uint32_t>(UserLogonFlags::ForceLiveLogOff)) {
      for (uint32_t user_index = 0; user_index < XUserMaxUserCount;
           user_index++) {
        if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
          profile_manager->Logout(user_index, true);
        }
      }
    }

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;

    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length = 0;
    return run(extended_error, length);
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
    return X_ERROR_IO_PENDING;
  }
}
DECLARE_XAM_EXPORT1(XamUserLogon, kUserProfiles, kImplemented);

dword_result_t XamUserLogonEx_entry(pointer_t<X_PROFILEENUMRESULT> profile_ptr,
                                    dword_t flags,
                                    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  auto run = [profile_ptr, flags](uint32_t& extended_error,
                                  uint32_t& length) -> X_RESULT {
    X_STATUS result = X_ERROR_SUCCESS;

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;

    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length = 0;
    return run(extended_error, length);
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
    return X_ERROR_IO_PENDING;
  }
}
DECLARE_XAM_EXPORT1(XamUserLogonEx, kUserProfiles, kSketchy);

X_HRESULT_result_t XamUserValidateAvatarManifest_entry() {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserValidateAvatarManifest, kUserProfiles, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(User);
