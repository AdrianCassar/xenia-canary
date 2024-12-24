/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include "xenia/app/profile_dialogs.h"

#include <algorithm>
#include <cfloat>

#include "xenia/app/emulator_window.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_ui.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_host_notification.h"

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

  const auto profile_manager = emulator_window_->emulator()
                                   ->kernel_state()
                                   ->xam_state()
                                   ->profile_manager();
  if (!profile_manager) {
    return;
  }

  for (const auto& [xuid, account] : *profile_manager->GetAccounts()) {
    LoadProfileIcon(xuid);
  }
}

void ProfileConfigDialog::LoadProfileIcon(const uint64_t xuid) {
  if (!emulator_window_) {
    return;
  }

  const auto profile_manager = emulator_window_->emulator()
                                   ->kernel_state()
                                   ->xam_state()
                                   ->profile_manager();
  if (!profile_manager) {
    return;
  }

  const auto profile_icon =
      profile_manager->GetAccountIcon(xuid, kernel::xam::XTileType::kGamerTile);

  if (auto& current_icon = profile_icon_[xuid]; current_icon) {
    released_icons_.push_back(std::move(current_icon));
  }

  profile_icon_[xuid] = profile_icon.empty()
                            ? nullptr
                            : imgui_drawer()->LoadImGuiIcon(profile_icon);
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

  // Previous frame is already rendered, so these aren't referenced anymore.
  released_icons_.clear();

  for (const uint64_t xuid : *pending_icon_reloads_) {
    LoadProfileIcon(xuid);
  }
  pending_icon_reloads_->clear();

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

  // Scroll instead of running off the window, keeping Create Profile visible.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float reserved_height = style.ItemSpacing.y * 2.0f +
                                ImGui::GetFrameHeightWithSpacing() +
                                style.WindowPadding.y;
  const float max_list_height =
      std::max(viewport->WorkPos.y + viewport->WorkSize.y -
                   ImGui::GetCursorScreenPos().y - reserved_height,
               ui::default_image_icon_size.y * 1.5f);
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(FLT_MAX, max_list_height));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ImGui::GetFontSize() * 0.8f);
  ImGui::BeginChild("##ProfileList", ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
  ImGui::PopStyleVar();

  for (auto& [xuid, account] : *profiles) {
    ImGui::PushID(fmt::format("{:016X}", xuid).c_str());

    const uint8_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid);

    // Profile might be created after dialog was opened.
    if (!profile_icon_.contains(xuid)) {
      LoadProfileIcon(xuid);
    }

    const auto profile_icon = profile_icon_[xuid].get();

    auto context_menu_fun = [=, this]() -> bool {
      if (ImGui::BeginPopupContextItem("Profile Menu")) {
        //*selected_xuid = xuid;
        if (user_index == XUserIndexAny) {
          ImGui::BeginDisabled(!profile_manager->IsAnyProfileSlotFree());

          if (ImGui::MenuItem("Login")) {
            profile_manager->Login(xuid);
            LoadProfileIcon(xuid);
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
          ImGui::EndDisabled();
        } else {
          if (ImGui::MenuItem("Logout")) {
            profile_manager->Logout(user_index);
            LoadProfileIcon(xuid);
          }
        }

        if (ImGui::MenuItem("Modify")) {
          // Gamercard might outlive this dialog, so don't capture this.
          auto on_icon_change = [pending_icon_reloads =
                                     std::weak_ptr(pending_icon_reloads_),
                                 xuid]() {
            if (const auto pending = pending_icon_reloads.lock()) {
              pending->insert(xuid);
            }
          };

          new kernel::xam::ui::GamercardUI(
              emulator_window_->window(), emulator_window_->imgui_drawer(),
              emulator_window_->emulator()->kernel_state(), xuid,
              on_icon_change);
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

        if (!emulator_window_->emulator()->is_title_open()) {
          ImGui::Separator();

          if (account.IsLiveEnabled()) {
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
                ImGui::EndPopup();
                return false;
              }

              ImGui::EndMenu();
            }
          } else {
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
                ImGui::EndPopup();
                return false;
              }

              ImGui::EndMenu();
            }
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
      ImGui::EndChild();
      ImGui::End();
      return;
    }

    ImGui::PopID();
    ImGui::Separator();
  }

  ImGui::EndChild();

  ImGui::Spacing();

  if (ImGui::Button("Create Profile")) {
    new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                         emulator_window_->emulator());
  }

  ImGui::End();
}
void FriendsManagerDialog::OnDraw(ImGuiIO& io) {
  if (!has_opened_) {
    ImGui::OpenPopup("Friends Manager");
    has_opened_ = true;
  }

  const uint32_t user_index = 0;

  auto profile =
      emulator_window_->emulator()->kernel_state()->xam_state()->GetUserProfile(
          user_index);

  ImVec2 btn_size = ImVec2(ImGui::GetWindowSize().x * 0.4f, 0);
  ImVec2 btn2_size = ImVec2(ImGui::GetWindowSize().x * 0.2f, 0);
  ImVec2 btn3_size = ImVec2(ImGui::GetWindowSize().x * 0.215f, 0);

  if (ImGui::BeginPopupModal("Friends Manager", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::Button("Add Friend", btn_size)) {
      ImGui::OpenPopup("Add Friend");
    }

    ImGui::SameLine();

    if (ImGui::Button("Remove All Friends", btn_size)) {
      ImGui::OpenPopup("Remove All Friends");
    }

    if (ImGui::BeginPopupModal("Add Friend", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      const uint32_t user_index = 0;

      if (are_friends) {
        ImGui::Text("Friend Added!");
        ImGui::Separator();
      }

      const std::string xuid_string = std::string(add_xuid_);

      uint64_t xuid = 0;

      if (xuid_string.length() == 16) {
        if (xuid_string.substr(0, 4) == "0009") {
          xuid = string_util::from_string<uint64_t>(xuid_string, true);

          valid_xuid = kernel::IsOnlineXUID(xuid);
          are_friends = profile->IsFriend(xuid);
        }

        if (!valid_xuid) {
          ImGui::Text("Invalid XUID!");
          ImGui::Separator();
        }
      } else {
        valid_xuid = false;
        are_friends = false;
      }

      ImGui::Text("Friend's Online XUID:");

      ImGui::SameLine();

      const std::string friends_count =
          fmt::format("\t\t\t\t\t\t\t\t{}/100", profile->GetFriendsCount());

      ImGui::Text(friends_count.c_str());

      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

      ImGui::InputTextWithHint("##AddFriend", "0009XXXXXXXXXXXX", add_xuid_,
                               sizeof(add_xuid_),
                               ImGuiInputTextFlags_CharsHexadecimal |
                                   ImGuiInputTextFlags_CharsUppercase);

      if (ImGui::Button("Paste Clipboard", btn3_size)) {
        const char* clipboard = ImGui::GetClipboardText();

        if (clipboard) {
          std::string clipboard_str = std::string(clipboard);

          if (!clipboard_str.empty()) {
            strcpy(add_xuid_, clipboard_str.substr(0, 16).c_str());
          }
        }
      }

      ImGui::SameLine();

      ImGui::BeginDisabled(!valid_xuid || are_friends);
      if (ImGui::Button("Add", btn3_size)) {
        profile->AddFriendFromXUID(xuid);
        xe::kernel::XLiveAPI::AddFriend(xuid);

        emulator_window_->emulator()->kernel_state()->BroadcastNotification(
            kXNotificationFriendsFriendAdded, user_index);

        emulator_window_->emulator()
            ->display_window()
            ->app_context()
            .CallInUIThread([&]() {
              new xe::ui::HostNotificationWindow(imgui_drawer(), "Added Friend",
                                                 xuid_string, 0);
            });
      }
      ImGui::EndDisabled();

      ImGui::SameLine();

      if (ImGui::Button("Close", btn3_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Remove All Friends", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Are you sure?");
      ImGui::Separator();

      if (ImGui::Button("Yes", btn2_size)) {
        for (const auto friend_ : profile->GetFriends()) {
          profile->RemoveFriend(friend_.xuid);
          xe::kernel::XLiveAPI::RemoveFriend(friend_.xuid);
        }

        emulator_window_->emulator()->kernel_state()->BroadcastNotification(
            kXNotificationFriendsFriendRemoved, user_index);

        emulator_window_->emulator()
            ->display_window()
            ->app_context()
            .CallInUIThread([&]() {
              new xe::ui::HostNotificationWindow(
                  imgui_drawer(), "Removed All Friends", "Success", 0);
            });

        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", btn2_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (ImGui::Button("Refresh Presence", btn_size)) {
      emulator_window_->emulator()->kernel_state()->BroadcastNotification(
          kXNotificationFriendsPresenceChanged, user_index);

      emulator_window_->emulator()
          ->display_window()
          ->app_context()
          .CallInUIThread([&]() {
            new xe::ui::HostNotificationWindow(
                imgui_drawer(), "Refreshed Presence", "Success", 0);
          });
    }

    ImGui::SameLine();

    if (ImGui::Button("Exit", btn_size)) {
      ImGui::CloseCurrentPopup();
      emulator_window_->ToggleFriendsDialog();
    }
    ImGui::EndPopup();
  }
}

}  // namespace app
}  // namespace xe
