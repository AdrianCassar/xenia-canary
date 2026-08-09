/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/profile_dialogs.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_ui.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_host_notification.h"

#include <xenia/vfs/devices/host_path_entry.h>
#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/signin_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"

namespace xe {
namespace app {

void NoProfileDialog::OnDraw(ImGuiIO& io) {
  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  if (profile_manager->GetAccountCount()) {
    Close();
    return;
  }

  const auto window_position =
      ImVec2(GetIO().DisplaySize.x * 0.35f, GetIO().DisplaySize.y * 0.4f);

  ImGui::SetNextWindowPos(window_position, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0f);

  bool dialog_open = true;
  if (!ImGui::Begin("No Profiles Found", &dialog_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    Close();
    return;
  }

  const std::string message =
      "There is no profile available! You will not be able to save without "
      "one.\n\nWould you like to create one?";

  ImGui::TextUnformatted(message.c_str());

  ImGui::Separator();
  ImGui::NewLine();

  const auto content_files = xe::filesystem::ListDirectories(
      emulator_window_->emulator()->content_root());

  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  if (content_files.empty()) {
    if (ImGui::Button("Create Profile")) {
      new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                           emulator_window_->emulator());
    }
  } else {
    if (ImGui::Button("Create profile & migrate data")) {
      new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                           emulator_window_->emulator(), true);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Open profile menu")) {
    emulator_window_->ToggleProfilesConfigDialog();
  }

  ImGui::SameLine();
  if (ImGui::Button("Close") || !dialog_open) {
    emulator_window_->SetHotkeysState(true);
    ImGui::End();
    Close();
    return;
  }
  ImGui::End();
}

void ProfileConfigDialog::LoadProfileIcon() {
  if (!emulator_window_) {
    return;
  }

  for (uint8_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    const auto profile = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager()
                             ->GetProfile(user_index);

    if (!profile) {
      continue;
    }
    LoadProfileIcon(profile->xuid());
  }
}

void ProfileConfigDialog::LoadProfileIcon(const uint64_t xuid) {
  if (!emulator_window_) {
    return;
  }

  const auto xam_state =
      emulator_window_->emulator()->kernel_state()->xam_state();

  const auto profile_manager = xam_state->profile_manager();
  const auto user_tracker = xam_state->user_tracker();

  if (!profile_manager) {
    return;
  }

  if (!user_tracker) {
    return;
  }

  const auto profile = profile_manager->GetProfile(xuid);

  if (!profile) {
    if (profile_icon_.contains(xuid)) {
      profile_icon_[xuid].release();
    }

    if (profile_gamerpic_key_.contains(xuid)) {
      profile_gamerpic_key_.erase(xuid);
    }
    return;
  }

  const auto gamerpic_key = user_tracker->GetUserGamerpicSetting(xuid);

  if (gamerpic_key.has_value()) {
    profile_gamerpic_key_[xuid] = gamerpic_key.value();
  }

  const auto profile_icon =
      profile->GetProfileIcon(kernel::xam::XTileType::kGamerTile);
  if (profile_icon.empty()) {
    return;
  }

  profile_icon_[xuid].release();
  profile_icon_[xuid] = imgui_drawer()->LoadImGuiIcon(profile_icon);
}

void ProfileConfigDialog::LoadSignedInProfilesCount() {
  signed_in_profiles_count_ = emulator_window_->emulator()
                                  ->kernel_state()
                                  ->xam_state()
                                  ->profile_manager()
                                  ->SignedInProfilesCount();
}

void ProfileConfigDialog::OnDraw(ImGuiIO& io) {
  if (!emulator_window_->emulator() ||
      !emulator_window_->emulator()->kernel_state() ||
      !emulator_window_->emulator()->kernel_state()->xam_state()) {
    return;
  }

  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();
  if (!profile_manager) {
    return;
  }

  auto profiles = profile_manager->GetAccounts();

  ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.8f);

  bool dialog_open = true;
  if (!ImGui::Begin("Profiles Menu", &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    return;
  }

  // For whatever reason dialog wasn't opened. It's probably in closing state.
  // We need to handle it here before it will make icons allocation.
  if (!dialog_open) {
    ImGui::CloseCurrentPopup();
    Close();
    ImGui::End();
    emulator_window_->ToggleProfilesConfigDialog();
    return;
  }

  if (profiles->empty()) {
    ImGui::TextUnformatted("No profiles found!");
    ImGui::Spacing();
    ImGui::Separator();
  }

  const ImVec2 next_window_position =
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 20.f,
             ImGui::GetWindowPos().y);

  // Load profile icon for profiles created while dialog is rendering.
  if (signed_in_profiles_count_ != profile_manager->SignedInProfilesCount()) {
    LoadProfileIcon();
    signed_in_profiles_count_ = profile_manager->SignedInProfilesCount();
  }

  for (auto& [xuid, account] : *profiles) {
    ImGui::PushID(fmt::format("{:016X}", xuid).c_str());

    const uint8_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid);

    // Detect the change because creating ImmediateTexture every frame will
    // impact performance.
    //
    // If dialog is open while Gamerpic Browser changes gamerpic then we want to
    // detect the change.
    const auto user_tracker = emulator_window_->emulator()
                                  ->kernel_state()
                                  ->xam_state()
                                  ->user_tracker();

    // Detect sign out from outside this dialog. e.g. another dialog.
    const bool signed_out = user_index == XUserIndexAny;

    if (profile_gamerpic_key_.contains(xuid) && signed_out) {
      profile_gamerpic_key_.erase(xuid);
    }

    if (profile_icon_.contains(xuid) && signed_out) {
      if (profile_icon_[xuid]) {
        profile_icon_[xuid].release();
      }
    }

    if (user_tracker && profile_gamerpic_key_.contains(xuid)) {
      const auto gamerpic_key = user_tracker->GetUserGamerpicSetting(xuid);

      if (gamerpic_key.has_value()) {
        const kernel::xam::GamerPictureKey cached_gamerpic_key =
            profile_gamerpic_key_.at(xuid);
        const kernel::xam::GamerPictureKey current_gamerpic_key =
            gamerpic_key.value();

        // Reload gamerpic icon if changed.
        if (memcmp(&cached_gamerpic_key, &current_gamerpic_key,
                   sizeof(kernel::xam::GamerPictureKey)) != 0) {
          LoadProfileIcon(xuid);
        }
      }
    }

    const auto profile_icon = profile_icon_.find(xuid) != profile_icon_.cend()
                                  ? profile_icon_[xuid].get()
                                  : nullptr;

    auto context_menu_fun = [=, this]() -> bool {
      if (ImGui::BeginPopupContextItem("Profile Menu")) {
        //*selected_xuid = xuid;
        if (user_index == XUserIndexAny) {
          if (ImGui::MenuItem("Login")) {
            profile_manager->Login(xuid);
            if (!profile_manager->GetProfile(xuid)
                     ->GetProfileIcon(kernel::xam::XTileType::kGamerTile)
                     .empty()) {
              LoadProfileIcon(xuid);
            }
          }
          if (ImGui::BeginMenu("Login to slot:")) {
            for (uint8_t i = 1; i <= XUserMaxUserCount; i++) {
              if (ImGui::MenuItem(fmt::format("slot {}", i).c_str())) {
                uint64_t current_slot_xuid = 0;

                if (const auto current_profile = profile_manager->GetProfile(
                        static_cast<uint8_t>(i - 1));
                    current_profile) {
                  current_slot_xuid = current_profile->xuid();
                }

                profile_manager->Login(xuid, i - 1);
                LoadProfileIcon(xuid);

                // Release resources
                if (current_slot_xuid) {
                  LoadProfileIcon(current_slot_xuid);
                }
              }
            }
            ImGui::EndMenu();
          }
        } else {
          if (ImGui::MenuItem("Logout")) {
            profile_manager->Logout(user_index);
            LoadProfileIcon(xuid);
          }
        }

        if (ImGui::MenuItem("Modify")) {
          new kernel::xam::ui::GamercardUI(
              emulator_window_->window(), emulator_window_->imgui_drawer(),
              emulator_window_->emulator()->kernel_state(), xuid);
        }

        if (ImGui::BeginMenu("Copy")) {
          if (ImGui::MenuItem("Gamertag")) {
            ImGui::SetClipboardText(account.GetGamertagString().c_str());
          }

          if (ImGui::MenuItem("XUID")) {
            ImGui::SetClipboardText(fmt::format("{:016X}", xuid).c_str());
          }

          if (account.IsLiveEnabled()) {
            if (ImGui::MenuItem("XUID Online")) {
              ImGui::SetClipboardText(
                  fmt::format("{:016X}", account.xuid_online.get()).c_str());
            }
          }

          ImGui::EndMenu();
        }

        const bool is_signedin = profile_manager->GetProfile(xuid) != nullptr;
        ImGui::BeginDisabled(!is_signedin);
        if (ImGui::MenuItem("Show Played Titles")) {
          new kernel::xam::ui::TitleListUI(
              emulator_window_->imgui_drawer(), next_window_position,
              profile_manager->GetProfile(user_index));
        }
        ImGui::EndDisabled();

        if (ImGui::MenuItem("Show Content Directory")) {
          const auto path = profile_manager->GetProfileContentPath(
              xuid, emulator_window_->emulator()->kernel_state()->title_id());

          if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
          }

          std::thread path_open(LaunchFileExplorer, path);
          path_open.detach();
        }

        ImGui::BeginDisabled(!account.IsLiveEnabled());
        if (ImGui::BeginMenu("XStorage Content")) {
          const auto emulator = emulator_window_->emulator();
          const auto filesystem = emulator->kernel_state()->file_system();

          xe::vfs::HostPathEntry* entry = nullptr;
          std::string symlink_path;

          if (ImGui::MenuItem("Show Game Data")) {
            if (emulator->is_title_open()) {
              symlink_path =
                  fmt::format("XSTORAGE:user\\{:016X}\\title\\{:08X}",
                              account.xuid_online.get(), emulator->title_id());

            } else {
              symlink_path = fmt::format("XSTORAGE:user\\{:016X}",
                                         account.xuid_online.get());
            }
          }

          ImGui::BeginDisabled(!emulator->is_title_open());
          if (ImGui::MenuItem("Show Game Clips")) {
            symlink_path =
                fmt::format("XSTORAGE:clips\\title\\{:08X}\\{:016X}",
                            emulator->title_id(), account.xuid_online.get());
          }
          ImGui::EndDisabled();

          if (!symlink_path.empty()) {
            entry = reinterpret_cast<xe::vfs::HostPathEntry*>(
                filesystem->ResolvePath(symlink_path));

            if (entry) {
              std::thread path_open(LaunchFileExplorer, entry->host_path());
              path_open.detach();
            }
          }

          ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        if (!emulator_window_->emulator()->is_title_open()) {
          ImGui::Separator();

          if (account.IsLiveEnabled()) {
            ImGui::BeginDisabled(!is_signedin);
            if (ImGui::BeginMenu("Convert to Offline Profile")) {
              ImGui::BeginTooltip();
              ImGui::TextUnformatted(
                  fmt::format(
                      "You're about to convert profile: {} (XUID: {:016X}) "
                      "to an offline profile. Are you sure?",
                      account.GetGamertagString(), xuid)
                      .c_str());
              ImGui::EndTooltip();

              if (ImGui::MenuItem("Yes, convert it!")) {
                profile_manager->ConvertToOfflineProfile(xuid);
                ImGui::EndMenu();
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return false;
              }

              ImGui::EndMenu();
            }
            ImGui::EndDisabled();
          } else {
            ImGui::BeginDisabled(!is_signedin);
            if (ImGui::BeginMenu("Convert to Xbox Live-Enabled Profile")) {
              ImGui::BeginTooltip();
              ImGui::TextUnformatted(
                  fmt::format(
                      "You're about to convert profile: {} (XUID: {:016X}) "
                      "to an Xbox Live-Enabled profile. Are you sure?",
                      account.GetGamertagString(), xuid)
                      .c_str());
              ImGui::EndTooltip();

              if (ImGui::MenuItem("Yes, convert it!")) {
                profile_manager->ConvertToXboxLiveEnabledProfile(xuid);
                ImGui::EndMenu();
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return false;
              }

              ImGui::EndMenu();
            }
            ImGui::EndDisabled();
          }

          if (ImGui::BeginMenu("Delete Profile")) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(
                fmt::format(
                    "You're about to delete profile: {} (XUID: {:016X}). "
                    "This will remove all data assigned to this profile "
                    "including savefiles. Are you sure?",
                    account.GetGamertagString(), xuid)
                    .c_str());
            ImGui::EndTooltip();

            if (ImGui::MenuItem("Yes, delete it!")) {
              profile_manager->DeleteProfile(xuid);
              ImGui::EndMenu();
              ImGui::EndPopup();
              return false;
            }

            ImGui::EndMenu();
          }
        }
        ImGui::EndPopup();
      }
      return true;
    };

    if (!kernel::xam::xeDrawProfileContent(
            imgui_drawer(), xuid, user_index, &account, profile_icon,
            context_menu_fun, [=, this]() { LoadProfileIcon(xuid); },
            &selected_xuid_)) {
      ImGui::PopID();
      ImGui::End();
      return;
    }

    ImGui::PopID();
    ImGui::Separator();
  }

  ImGui::Spacing();

  if (ImGui::Button("Create Profile")) {
    new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                         emulator_window_->emulator());
  }

  ImGui::End();
}

void ManagerDialog::Initialize(ui::ImGuiDrawer* imgui_drawer,
                               uint32_t user_index) {
  emulator_ = emulator_window_->emulator();

  const auto profile =
      emulator_->kernel_state()->xam_state()->GetUserProfile(user_index);

  if (!profile) {
    return;
  }

  // If title is open we can assume periodic maintenance is running, therefore
  // presence sync isn't needed.
  if (!emulator_->is_title_open()) {
    friends_ui_args_.friends_presence_sync =
        emulator_->kernel_state()->presence_manager()->SyncPresenceAsync(
            profile->xuid());
  } else {
    friends_ui_args_.friends_presence =
        emulator_->kernel_state()
            ->xam_state()
            ->presence_manager()
            ->GetFriendsPresenceSorted(profile->xuid());
  }

  friends_ui_args_.immediate_gamerpics =
      emulator_->GetXboxLiveAPI()->GetFriendsGamerpicsAsync(profile->xuid(),
                                                            imgui_drawer);
}

void ManagerDialog::OnDraw(ImGuiIO& io) {
  if (!manager_opened_) {
    manager_opened_ = true;
    ImGui::OpenPopup("Manager");

    sessions_args.filter_own = true;
  }

  // Add profile dropdown selector?
  const uint32_t user_index = 0;

  auto profile =
      emulator_->kernel_state()->xam_state()->GetUserProfile(user_index);

  const bool is_profile_signed_in = profile == nullptr;

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Manager", &manager_opened_,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImVec2 btn_size = ImVec2(200, 40);

    if (is_profile_signed_in) {
      ImGui::Text("You're not logged into a profile!");
      ImGui::Separator();
    }

    ImGui::SetWindowFontScale(1.2f);

    ImGui::BeginDisabled(is_profile_signed_in);
    if (ImGui::Button("Friends", btn_size)) {
      friends_ui_args_.content_args.friends_open = true;
      friends_ui_args_.content_args.first_draw = true;

      if (emulator_->kernel_state()->GetXboxLiveAPI()->IsConnectedToServer()) {
        friends_ui_args_.content_args.filter_offline = true;
      }

      ImGui::OpenPopup("Friends");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(is_profile_signed_in ||
                         !emulator_->GetXboxLiveAPI()->IsConnectedToServer());
    if (ImGui::Button("Sessions", btn_size)) {
      sessions_args.sessions_open = true;
      ImGui::OpenPopup("Sessions");
    }
    ImGui::EndDisabled();

    if (emulator_->GetXboxLiveAPI()->IsXUIDMismatched()) {
      ImVec2 button_pos = ImGui::GetCursorScreenPos();
      ImVec2 button_end =
          ImVec2(button_pos.x + btn_size.x, button_pos.y + btn_size.y);

      ImDrawList* draw_list = ImGui::GetWindowDrawList();

      draw_list->AddRect(button_pos, button_end, IM_COL32(255, 0, 0, 255), 0.0f,
                         0, 3.0f);
    }

    if (ImGui::Button("Delete Netplay Profiles", btn_size)) {
      ImGui::OpenPopup("Delete Profiles");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Delete profiles to fix XUID mismatch error.");
    }

    ImGui::SameLine();

    if (ImGui::Button("UPnP and Ports", btn_size)) {
      upnp_and_ports_args.first_draw = true;
      upnp_and_ports_args.dialog_open = true;
      ImGui::OpenPopup("UPnP and Ports");
    }

    ImGui::SetWindowFontScale(1.0f);

    if (!sessions_args.sessions_open) {
      sessions_args.first_draw = false;
      sessions_args.refresh_sessions_sync = true;
      sessions.clear();
    }

    xeDrawFriendsUI(imgui_drawer(), profile, friends_ui_args_);

    xeDrawSessionsContent(imgui_drawer(), profile, sessions_args, &sessions);

    if (!deletion_args.deleted_profiles_open) {
      deletion_args.first_draw = false;
      deleted_profiles = {};
    }

    bool open_deleted_profiles = false;

    float btn_height = 25;
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(225, -1), ImVec2(225, -1));
    if (ImGui::BeginPopupModal("Delete Profiles", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
        ImGui::CloseCurrentPopup();
      }

      float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                        (ImGui::GetStyle().ItemSpacing.x * 0.5f);
      ImVec2 btn_size = ImVec2(btn_width, btn_height);

      const std::string desc = "Are you sure?";
      const std::string desc2 = "You will be signed out.";

      ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());
      ImVec2 desc2_size = ImGui::CalcTextSize(desc2.c_str());

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
      ImGui::Text(desc.c_str());

      if (!is_profile_signed_in) {
        ImGui::Spacing();

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc2_size.x) * 0.5f);
        ImGui::Text(desc2.c_str());
      }

      ImGui::Separator();

      if (ImGui::Button("Yes", btn_size)) {
        if (!is_profile_signed_in) {
          std::map<uint8_t, uint64_t> xuids;

          kernel::xam::XamState* xam_state =
              emulator_window_->emulator()->kernel_state()->xam_state();

          for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
            if (xam_state->IsUserSignedIn(i)) {
              xuids[i] = xam_state->GetUserProfile(i)->xuid();
            }
          }

          xam_state->profile_manager()->LogoutMultiple(xuids);
        }

        deleted_profiles = emulator_->GetXboxLiveAPI()->DeleteMyProfiles();

        open_deleted_profiles = true;

        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", btn_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (open_deleted_profiles) {
      emulator_->GetXboxLiveAPI()->SetXUIDMismatch(false);

      deletion_args.deleted_profiles_open = true;
      ImGui::OpenPopup("Deleted Profiles");
    }

    xe::kernel::xam::xeDrawMyDeletedProfiles(imgui_drawer(), deletion_args,
                                             &deleted_profiles);

    if (!upnp_and_ports_args.dialog_open) {
      upnp_and_ports_args.first_draw = false;
    }

    xe::kernel::xam::xeDrawUPnPAndPorts(imgui_drawer(), upnp_and_ports_args,
                                        emulator_->GetUPnP());

    ImGui::EndPopup();
  }

  if (!manager_opened_) {
    Close();
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleFriendsDialog();
  }
}

}  // namespace app
}  // namespace xe
