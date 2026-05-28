/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_state.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xam/online_schema.h"

namespace xe {
namespace kernel {
namespace xam {

XamState::XamState(Emulator* emulator, KernelState* kernel_state)
    : kernel_state_(kernel_state) {
  app_manager_ = std::make_unique<AppManager>();

  auto content_root = emulator->content_root();
  if (!content_root.empty()) {
    content_root = std::filesystem::absolute(content_root);
  }
  content_manager_ =
      std::make_unique<ContentManager>(kernel_state, content_root);

  user_tracker_ = std::make_unique<UserTracker>();
  profile_manager_ =
      std::make_unique<ProfileManager>(kernel_state, user_tracker_.get());
  achievement_manager_ = std::make_unique<AchievementManager>();

  LoadOnlineSchema();
  LoadLanguageLocaleFallback();
  LoadIptvServiceName();

  AppManager::RegisterApps(kernel_state, app_manager_.get());
}

void XamState::LoadOnlineSchema() {
  constexpr uint32_t schema_data_address = 0x80E00000;

  if (kernel_state_->memory()
          ->LookupHeap(0x80000000)
          ->AllocFixed(
              schema_data_address,
              sizeof(XONLINE_SCHEMA_DATA) + sizeof(OnlineSchemaData_v6_5),
              0x1000, kMemoryAllocationCommit,
              kMemoryProtectRead | kMemoryProtectWrite)) {
    online_schema_data_address = schema_data_address;

    XONLINE_SCHEMA_DATA* schema_ptr =
        kernel_state_->memory()->TranslateVirtual<XONLINE_SCHEMA_DATA*>(
            schema_data_address);

    std::memcpy(schema_ptr + 1, OnlineSchemaData_v6_5,
                sizeof(OnlineSchemaData_v6_5));

    schema_ptr->schema_ptr = kernel_state_->memory()->HostToGuestVirtual(
        std::to_address(schema_ptr + 1));
    schema_ptr->schema_size = sizeof(OnlineSchemaData_v6_5);
  }
}

void XamState::LoadLanguageLocaleFallback() {
  const std::array<std::u16string, 18> locale_data = {
      u"",      u"",      u"ja-JP", u"de-DE", u"fr-FR",  u"es-ES",
      u"it-IT", u"ko-KR", u"zh-TW", u"pt-BR", u"zh-CHS", u"pl-PL",
      u"ru-RU", u"sv-SE", u"tr-TR", u"nb-NO", u"nl-NL",  u"zh-CHS"};

  constexpr uint32_t array_start = 0x80D00000;

  if (kernel_state_->memory()
          ->LookupHeap(0x80000000)
          ->AllocFixed(array_start, 0xC8, 0x1000, kMemoryAllocationCommit,
                       kMemoryProtectRead | kMemoryProtectWrite)) {
    char16_t* ptr =
        kernel_state_->memory()->TranslateVirtual<char16_t*>(array_start);

    for (size_t i = 1; i < locale_data.size(); i++) {
      language_fallback_address_[i] =
          kernel_state_->memory()->HostToGuestVirtual(ptr);
      ptr += xe::string_util::copy_and_swap_truncating(
                 ptr, locale_data.at(i), locale_data.at(i).size() + 1) +
             1;
    }
  }
}

void XamState::LoadIptvServiceName() {
  constexpr uint32_t address = 0x80D10000;

  if (kernel_state_->memory()
          ->LookupHeap(0x80000000)
          ->AllocFixed(address, 0x78, 0x1000, kMemoryAllocationCommit,
                       kMemoryProtectRead | kMemoryProtectWrite)) {
    iptv_name_address_ = address;
  }
}

UserProfile* XamState::GetUserProfile(uint32_t user_index) const {
  if (user_index >= XUserMaxUserCount && user_index < XUserIndexLatest) {
    return nullptr;
  }

  return profile_manager_->GetProfile(static_cast<uint8_t>(user_index));
}

UserProfile* XamState::GetUserProfile(uint64_t xuid) const {
  if (IsOnlineXUID(xuid)) {
    assert_always();
    XELOGI("{}: Using online XUID {:016X}", __func__, xuid);
  }

  return profile_manager_->GetProfile(xuid);
}

UserProfile* XamState::GetUserProfileLive(uint64_t xuid) const {
  return profile_manager_->GetProfileLive(xuid);
}

UserProfile* XamState::GetUserProfileAny(uint64_t xuid) const {
  auto profile = profile_manager_->GetProfile(xuid);

  if (profile != nullptr) {
    return profile;
  }

  return profile_manager_->GetProfileLive(xuid);
}

uint8_t XamState::GetUserIndexAssignedToProfileFromXUID(uint64_t xuid) const {
  const uint8_t user_index =
      profile_manager_->GetUserIndexAssignedToProfile(xuid);

  if (user_index != XUserIndexAny) {
    return user_index;
  }

  return profile_manager_->GetUserIndexAssignedToLiveProfile(xuid);
}

bool XamState::IsUserSignedIn(uint32_t user_index) const {
  return profile_manager_->GetProfile(static_cast<uint8_t>(user_index)) !=
         nullptr;
}

bool XamState::IsUserSignedIn(uint64_t xuid) const {
  return GetUserProfileAny(xuid) != nullptr;
}

void XamState::LoadSpaInfo(const SpaInfo* info) {
  if (!info) {
    return;
  }
  // Check if we have loaded SpaInfo already. If yes then check currently loaded
  // version.
  if (spa_info_) {
    // Trying to load spa with lower version, for whatever reason.
    if (*info <= *spa_info_) {
      return;
    }
  }

  spa_info_ = std::make_unique<SpaInfo>(*info);
  spa_info_->Load();
  user_tracker_->UpdateSpaInfo(spa_info_.get());
}

void XamState::StartPeriodicMaintenance() const {
  for (uint32_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    const auto profile = GetUserProfile(user_index);

    if (profile) {
      user_tracker()->StartPeriodicMaintenance(profile->xuid());
    }
  }
}

void XamState::StopPeriodicMaintenance() const {
  for (uint32_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    const auto profile = GetUserProfile(user_index);

    if (profile) {
      user_tracker()->StopPeriodicMaintenance(profile->xuid());
    }
  }
}

void XamState::SetContentRegisterCallback(uint32_t callback) {
  content_register_callback = callback;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
