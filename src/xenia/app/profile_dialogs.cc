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

extern bool xeDrawFriendsContent(
    ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
    FriendsContentArgs& args, std::vector<FriendPresenceObjectJSON>* presences);

extern bool xeDrawSessionsContent(
    ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
    SessionsContentArgs& sessions_args,
    std::vector<std::unique_ptr<SessionObjectJSON>>* sessions);

extern bool xeDrawMyDeletedProfiles(
    ui::ImGuiDrawer* imgui_drawer, MyDeletedProfilesArgs& args,
    std::map<uint64_t, std::string>* deleted_profiles);

}  // namespace xam
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

void ManagerDialog::OnDraw(ImGuiIO& io) {
  if (!manager_opened_) {
    manager_opened_ = true;
    ImGui::OpenPopup("Manager");

    if (kernel::XLiveAPI::IsConnectedToServer()) {
      friends_args.filter_offline = true;
    }

    sessions_args.filter_own = true;
  }

  // Add profile dropdown selector?
  const uint32_t user_index = 0;

  auto profile =
      emulator_window_->emulator()->kernel_state()->xam_state()->GetUserProfile(
          user_index);

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
      friends_args.friends_open = true;
      ImGui::OpenPopup("Friends");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(is_profile_signed_in ||
                         !kernel::XLiveAPI::IsConnectedToServer());
    if (ImGui::Button("Sessions", btn_size)) {
      sessions_args.sessions_open = true;
      ImGui::OpenPopup("Sessions");
    }
    ImGui::EndDisabled();

    if (kernel::XLiveAPI::xuid_mismatch) {
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

    ImGui::BeginDisabled(is_profile_signed_in);
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
    ImGui::EndDisabled();

    ImGui::SetWindowFontScale(1.0f);

    if (!friends_args.friends_open) {
      friends_args.first_draw = false;
      friends_args.refresh_presence_sync = true;
      presences = {};
    }

    if (!sessions_args.sessions_open) {
      sessions_args.first_draw = false;
      sessions_args.refresh_sessions_sync = true;
      sessions.clear();
    }

    xeDrawFriendsContent(imgui_drawer(), profile, friends_args, &presences);

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

        deleted_profiles = kernel::XLiveAPI::DeleteMyProfiles();

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
      kernel::XLiveAPI::xuid_mismatch = false;

      deletion_args.deleted_profiles_open = true;
      ImGui::OpenPopup("Deleted Profiles");
    }

    xeDrawMyDeletedProfiles(imgui_drawer(), deletion_args, &deleted_profiles);

    ImGui::EndPopup();
  }

  if (!manager_opened_) {
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleFriendsDialog();
  }
}

}  // namespace app
}  // namespace xe