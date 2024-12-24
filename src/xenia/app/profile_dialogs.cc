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
#include "xenia/app/emulator_window.h"
#include "xenia/base/system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/ui/imgui_host_notification.h"

namespace xe {
namespace kernel {
namespace xam {
extern bool xeDrawProfileContent(ui::ImGuiDrawer* imgui_drawer,
                                 const uint64_t xuid, const uint8_t user_index,
                                 const X_XAMACCOUNTINFO* account,
                                 uint64_t* selected_xuid);
}
}  // namespace kernel
namespace app {

void CreateProfileDialog::OnDraw(ImGuiIO& io) {
  if (!has_opened_) {
    ImGui::OpenPopup("Create Profile");
    has_opened_ = true;
  }

  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  bool dialog_open = true;
  if (!ImGui::BeginPopupModal("Create Profile", &dialog_open,
                              ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    return;
  }

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)) {
    ImGui::SetKeyboardFocusHere(0);
  }

  ImGui::TextUnformatted("Gamertag:");
  ImGui::InputText("##Gamertag", gamertag_, sizeof(gamertag_));

  ImGui::Checkbox("Xbox Live Enabled", &live_enabled);

  const std::string gamertag_string = std::string(gamertag_);
  bool valid = profile_manager->IsGamertagValid(gamertag_string);

  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("Create")) {
    bool autologin = (profile_manager->GetAccountCount() == 0);
    uint32_t reserved_flags = 0;

    if (live_enabled) {
      reserved_flags |= X_XAMACCOUNTINFO::AccountReservedFlags::kLiveEnabled;
    }

    if (profile_manager->CreateProfile(gamertag_string, autologin, migration_,
                                       reserved_flags) &&
        migration_) {
      emulator_window_->emulator()->DataMigration(0xB13EBABEBABEBABE);
    }
    std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
    dialog_open = false;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  if (ImGui::Button("Cancel")) {
    std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
    dialog_open = false;
  }

  if (!dialog_open) {
    ImGui::CloseCurrentPopup();
    Close();
    ImGui::EndPopup();
    return;
  }
  ImGui::EndPopup();
}

void NoProfileDialog::OnDraw(ImGuiIO& io) {
  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  if (profile_manager->GetAccountCount()) {
    delete this;
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
    delete this;
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

  if (content_files.empty()) {
    if (ImGui::Button("Create Profile")) {
      new CreateProfileDialog(emulator_window_->imgui_drawer(),
                              emulator_window_);
    }
  } else {
    if (ImGui::Button("Create profile & migrate data")) {
      new CreateProfileDialog(emulator_window_->imgui_drawer(),
                              emulator_window_, true);
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
    delete this;
    return;
  }
  ImGui::End();
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

  if (profiles->empty()) {
    ImGui::TextUnformatted("No profiles found!");
    ImGui::Spacing();
    ImGui::Separator();
  }

  for (auto& [xuid, account] : *profiles) {
    ImGui::PushID(static_cast<int>(xuid));

    const uint8_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid);

    if (!kernel::xam::xeDrawProfileContent(imgui_drawer(), xuid, user_index,
                                           &account, &selected_xuid_)) {
      ImGui::PopID();
      ImGui::End();
      return;
    }

    ImGui::PopID();
    ImGui::Spacing();
    ImGui::Separator();
  }

  ImGui::Spacing();

  if (ImGui::Button("Create Profile")) {
    new CreateProfileDialog(emulator_window_->imgui_drawer(), emulator_window_);
  }

  ImGui::End();

  if (!dialog_open) {
    emulator_window_->ToggleProfilesConfigDialog();
    return;
  }
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

          valid_xuid = IsOnlineXUID(xuid);
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
            kXNotificationIDFriendsFriendAdded, user_index);

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
        for each (const auto friend_ in profile->GetFriends()) {
          profile->RemoveFriend(friend_.xuid);
          xe::kernel::XLiveAPI::RemoveFriend(friend_.xuid);
        }

        emulator_window_->emulator()->kernel_state()->BroadcastNotification(
            kXNotificationIDFriendsFriendRemoved, user_index);

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
          kXNotificationIDFriendsPresenceChanged, user_index);

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