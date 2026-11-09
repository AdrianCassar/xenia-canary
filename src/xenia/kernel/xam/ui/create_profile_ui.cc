/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/ui/resources.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

void CreateProfileUI::OnDraw(ImGuiIO& io) {
  if (!create_profile_args_.dialog_open) {
    ImGui::OpenPopup("Create Profile");
    create_profile_args_.dialog_open = true;
  }

  if (!xeDrawCreateProfile(imgui_drawer(), emulator_, create_profile_args_)) {
    Close();
  }
}

void CreateProfileUI::Initialize() {
  const auto gamerpic_key = GetDefaultGamerPictureKey();

  if (gamerpic_key.has_value()) {
    create_profile_args_.gamerpic_key = gamerpic_key;
    create_profile_args_.downloaded_gamerpics =
        kernel_state()->GetXboxLiveAPI()->DownloadCompleteGamerpic(
            gamerpic_key.value());
  }
}

std::optional<GamerPictureKey> CreateProfileUI::GetDefaultGamerPictureKey() {
  const auto default_picture_key = std::find_if(
      default_setting_values.cbegin(), default_setting_values.cend(),
      [](const UserSetting& setting) {
        return setting.get_setting_id() ==
               static_cast<uint32_t>(
                   UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY);
      });

  if (default_picture_key == default_setting_values.cend()) {
    return std::nullopt;
  }

  const UserSetting setting = *default_picture_key;

  const auto gamerpic_key = *reinterpret_cast<const GamerPictureKey*>(
      xe::to_utf8(std::get<std::u16string>(setting.get_host_data())).c_str());

  return gamerpic_key;
}

// Applies default settings and gamerpic, temporarily signing in the profile
// if needed.
void ApplyNewProfileDefaults(KernelState* kernel_state, uint64_t xuid,
                             bool signed_in, CreateProfileUIArgs& args) {
  auto xam_state = kernel_state->xam_state();
  auto profile_manager = xam_state->profile_manager();
  auto user_tracker = xam_state->user_tracker();

  uint64_t evicted_xuid = 0;

  if (!signed_in) {
    if (profile_manager->SignedInProfilesCount() == XUserMaxUserCount) {
      constexpr uint8_t last_user_index = XUserMaxUserCount - 1;

      evicted_xuid = profile_manager->GetProfile(last_user_index)->xuid();
      profile_manager->Logout(last_user_index);
    }

    profile_manager->Login(xuid);
  }

  if (args.live_enabled) {
    user_tracker->SetupDefaultProfileSettings(xuid);
  }

  if (args.gamerpic_key && args.downloaded_gamerpics.valid()) {
    const gamerpics_pair& gamerpics = args.downloaded_gamerpics.get();
    const GamerPictureKey& key = *args.gamerpic_key;

    user_tracker->UpdateUserGamerpic(xuid, key.GetTitleId(), key.GetBigTileId(),
                                     key.GetSmallTileId(), gamerpics.second,
                                     gamerpics.first);
  }

  if (!signed_in) {
    profile_manager->Logout(
        xam_state->GetUserIndexAssignedToProfileFromXUID(xuid));

    if (evicted_xuid) {
      profile_manager->Login(evicted_xuid);
    }
  }
}

void CreateProfileFromArgs(Emulator* emulator, CreateProfileUIArgs& args) {
  auto kernel_state = emulator->kernel_state();
  auto profile_manager = kernel_state->xam_state()->profile_manager();

  // The first profile is signed in automatically.
  const bool autologin = profile_manager->GetAccountCount() == 0;

  const uint32_t reserved_flags =
      args.live_enabled ? X_XAMACCOUNTINFO::AccountReservedFlags::kLiveEnabled
                        : 0;

  uint64_t xuid = 0;

  if (!profile_manager->CreateProfile(std::string(args.gamertag), autologin,
                                      args.migration, reserved_flags, &xuid)) {
    return;
  }

  if (args.migration) {
    emulator->DataMigration(xuid);
  }

  ApplyNewProfileDefaults(kernel_state, xuid, autologin, args);
}

bool xeDrawCreateProfile(xe::ui::ImGuiDrawer* imgui_drawer, Emulator* emulator,
                         CreateProfileUIArgs& args) {
  auto profile_manager =
      emulator->kernel_state()->xam_state()->profile_manager();

  if (!ImGui::BeginPopupModal("Create Profile", &args.dialog_open,
                              ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
    return false;
  }

  const float window_width = ImGui::GetContentRegionAvail().x;

  float btn_height = 25;
  float btn_width =
      (window_width * 0.5f) - (ImGui::GetStyle().ItemSpacing.x * 0.5f);
  ImVec2 half_width_btn = ImVec2(btn_width, btn_height);

  if (!args.big_gamerpic_texture && args.downloaded_gamerpics.valid() &&
      args.downloaded_gamerpics.wait_for(0s) == std::future_status::ready) {
    args.big_gamerpic_texture =
        imgui_drawer->LoadImGuiIcon(args.downloaded_gamerpics.get().first);
  }

  ImTextureID gamerpic =
      reinterpret_cast<ImTextureID>(imgui_drawer->GetLoadingTileIcon());

  if (args.big_gamerpic_texture) {
    gamerpic = reinterpret_cast<ImTextureID>(args.big_gamerpic_texture.get());
  }

  ImGui::Image(gamerpic, xe::ui::default_image_icon_size);

  ImGui::SameLine();

  ImGui::BeginGroup();

  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }

  ImGui::TextUnformatted("Gamertag:");

  const bool enter_pressed =
      ImGui::InputText("##Gamertag", args.gamertag, sizeof(args.gamertag),
                       ImGuiInputTextFlags_EnterReturnsTrue);

  if (ImGui::IsItemEdited() || enter_pressed) {
    args.valid_gamertag =
        profile_manager->IsGamertagValid(std::string(args.gamertag));
  }

  ImGui::Checkbox("Xbox Live Enabled", &args.live_enabled);

  ImGui::EndGroup();

  ImGui::BeginDisabled(!args.valid_gamertag);
  const bool create_pressed = ImGui::Button("Create", half_width_btn) ||
                              (enter_pressed && args.valid_gamertag);
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool cancel_pressed = ImGui::Button("Cancel", half_width_btn);

  if (create_pressed) {
    CreateProfileFromArgs(emulator, args);
  }

  if (create_pressed || cancel_pressed) {
    std::ranges::fill(args.gamertag, '\0');
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();

  return true;
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
