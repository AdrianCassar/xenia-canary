/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/profile_dialogs.h"
#include "build/version.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_ui.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_host_notification.h"

#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/signin_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"

#include "third_party/imgui/imgui_internal.h"

#ifdef XE_PLATFORM_WIN32
#include <shlobj.h>
#include <windows.h>
#else
// TODO: Crossplatform alternatives for clipboard
#endif

namespace xe {
namespace app {

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
    delete this;
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

  const auto profile_manager = emulator_window_->emulator()
                                   ->kernel_state()
                                   ->xam_state()
                                   ->profile_manager();
  if (!profile_manager) {
    return;
  }

  const auto profile = profile_manager->GetProfile(xuid);

  if (!profile) {
    if (profile_icon_.contains(xuid)) {
      profile_icon_[xuid].release();
    }
    return;
  }

  const auto profile_icon =
      profile->GetProfileIcon(kernel::xam::XTileType::kGamerTile);
  if (profile_icon.empty()) {
    return;
  }

  profile_icon_[xuid].release();
  profile_icon_[xuid] = imgui_drawer()->LoadImGuiIcon(profile_icon);
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

  const ImVec2 next_window_position =
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 20.f,
             ImGui::GetWindowPos().y);

  for (auto& [xuid, account] : *profiles) {
    ImGui::PushID(static_cast<int>(xuid));

    const uint8_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid);

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
                profile_manager->Login(xuid, i - 1);
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

    xe::kernel::xam::xeDrawMyDeletedProfiles(imgui_drawer(), deletion_args,
                                             &deleted_profiles);

    ImGui::EndPopup();
  }

  if (!manager_opened_) {
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleFriendsDialog();
  }
}

// https://github.com/ocornut/imgui/issues/1537#issuecomment-780262461
bool UpdaterDialog::ToggleButton(const char* str_id, bool* v) {
  ImVec4* colors = ImGui::GetStyle().Colors;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  float height = ImGui::GetFrameHeight();
  float width = height * 2.00f;
  float radius = height * 0.50f;

  bool clicked = false;

  ImGui::InvisibleButton(str_id, ImVec2(width, height));

  if (ImGui::IsItemClicked()) {
    *v = !*v;
    clicked = true;
  }

  ImGuiContext& gg = *GImGui;
  float ANIM_SPEED = 0.085f;
  if (gg.LastActiveId ==
      gg.CurrentWindow->GetID(str_id))  // && g.LastActiveIdTimer < ANIM_SPEED)
    float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
  if (ImGui::IsItemHovered())
    draw_list->AddRectFilled(
        p, ImVec2(p.x + width, p.y + height),
        ImGui::GetColorU32(*v ? colors[ImGuiCol_ButtonActive]
                              : ImVec4(0.78f, 0.78f, 0.78f, 1.0f)),
        height * 0.5f);
  else
    draw_list->AddRectFilled(
        p, ImVec2(p.x + width, p.y + height),
        ImGui::GetColorU32(*v ? colors[ImGuiCol_Button]
                              : ImVec4(0.85f, 0.85f, 0.85f, 1.0f)),
        height * 0.50f);
  draw_list->AddCircleFilled(
      ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f),
             p.y + radius),
      radius - 1.5f, IM_COL32(255, 255, 255, 255));

  return clicked;
}

void UpdaterDialog::OnDraw(ImGuiIO& io) {
  if (!updater_opened_) {
    updater_opened_ = true;
    ImGui::OpenPopup("Updater");
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height_padding = ImGui::GetStyle().FramePadding.x * 2.5f;
  float btn_width_padding = ImGui::GetStyle().FramePadding.x * 5.0f;

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

#ifndef DEBUG
  if (changelog_.empty() || (checked_for_updates_ && !update_available_)) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, -1), ImVec2(300, -1));
  } else {
    // Using -1 for y with SetWindowFontScale causes Separator to appear thin.
    // Ideally use a larger font instead of using SetWindowFontScale.
    ImGui::SetNextWindowSizeConstraints(ImVec2(450, -1), ImVec2(450, -1));
  }
#endif

  if (ImGui::BeginPopupModal(
          "Updater", &updater_opened_,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::SetWindowFontScale(1.05f);

#ifdef DEBUG
    ImGui::Text("This is a debug build, therefore updates are unavailable.");

    ImGui::SetWindowFontScale(1.0f);

    ImGui::EndPopup();
  }
#else
    float cursor_x = ImGui::GetCursorPosX();

    ImGui::BeginGroup();

    std::string update_desc = stable_toggle_ ? "Check for Stable Updates"
                                             : "Check for Nightly Updates";
    ImVec2 update_lbl_size = ImGui::CalcTextSize(update_desc.c_str());
    ImVec2 update_btn_size = ImVec2(update_lbl_size.x + btn_width_padding,
                                    update_lbl_size.y + btn_height_padding);

    if (ImGui::Button(update_desc.c_str(), update_btn_size)) {
      checked_for_updates_ = true;

      update_available_ = updater_->CheckForUpdates(
          stable_toggle_, XE_BUILD_BRANCH, &latest_commit_hash_,
          &latest_commit_date_, &stable_release_tag_, &update_response_code_);

      if (update_response_code_ != HTTP_STATUS_CODE::HTTP_OK) {
        update_available_ = false;
      }

      if (update_available_) {
        commit_messages_.clear();

        uint32_t result = 0;

        std::string commit_compare_status_ = "";

        if (stable_toggle_) {
          result = updater_->GetChangelogBetweenCommits(
              XE_BUILD_COMMIT, stable_release_tag_, commit_compare_status_,
              commit_messages_);
        } else {
          result = updater_->GetChangelogBetweenCommits(
              XE_BUILD_COMMIT, latest_commit_hash_, commit_compare_status_,
              commit_messages_);
        }

        if (commit_compare_status_ == "identical") {
          update_available_ = false;
          compare_status_ = COMPARE_STATE::IDENTICAL;
        } else if (commit_compare_status_ == "ahead") {
          compare_status_ = COMPARE_STATE::AHEAD;
        } else if (commit_compare_status_ == "behind") {
          compare_status_ = COMPARE_STATE::BEHIND;
        } else if (commit_compare_status_ == "diverged") {
          compare_status_ = COMPARE_STATE::DIVERGED;
        }

        if (result == HTTP_STATUS_CODE::HTTP_OK) {
          if (compare_status_ == COMPARE_STATE::AHEAD ||
              compare_status_ == COMPARE_STATE::BEHIND) {
            if (!commit_messages_.empty()) {
              changelog_.clear();
            }

            for (const auto& message : commit_messages_) {
              changelog_.append(fmt::format("- {}\n", message));
            }
          }
        }
      }

      checked_for_updates_ = true;
    }

    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();

    const std::string toggle_lbl = "Stable";

    // same as in ToggleButton()
    float toggle_btn_height = ImGui::GetFrameHeight();
    float toggle_btn_width = ImGui::GetFrameHeight() * 2.00f;

    ImVec2 text_size = ImGui::CalcTextSize(toggle_lbl.c_str());

    float total_width =
        text_size.x + ImGui::GetStyle().ItemSpacing.x + toggle_btn_width;

    const float region_max =
        ImGui::GetContentRegionAvail().x + ImGui::GetCursorPos().x;
    float lbl_align_x = region_max - total_width;
    ImGui::SetCursorPosX(lbl_align_x);

    ImGui::Text(toggle_lbl.c_str());

    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();

    if (ToggleButton("ToggleStable", &stable_toggle_)) {
      // Reset current data if toggled
      update_response_code_ = 0;
      download_response_code_ = 0;
      checked_for_updates_ = false;
      update_available_ = false;
      replace_file_ = false;
      compare_status_ = COMPARE_STATE::IDENTICAL;
      latest_commit_hash_ = "";
      latest_commit_date_ = "";
      stable_release_tag_ = "";
      changelog_.clear();

      // Download state reset
      downloaded_ = false;
      downloaded_failed_ = false;
      downloading_ = false;
      applying_update_failed_ = false;
      hide_download_button_ = false;
      download_progress_ = 0.0f;
      downloaded_file_path_.clear();
    }

    ImGui::SetCursorPosX(cursor_x);
    ImGui::Dummy(ImVec2(0, 0));

    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (checked_for_updates_ && update_available_) {
      const uint32_t lines = 15;
      float height = ImGui::GetTextLineHeight() * lines;

      if (!changelog_.empty()) {
        if (compare_status_ == COMPARE_STATE::AHEAD) {
          ImGui::Text("What's new:");
        } else if (compare_status_ == COMPARE_STATE::BEHIND) {
          ImGui::Text("Rolling back:");
        } else {
          ImGui::Text("Changelog:");
        }

        ImGui::Spacing();

        const ImVec2 muli_input_text_pos = ImGui::GetCursorScreenPos();

        ImGui::BeginChild("##ChangelogChild", ImVec2(-1, height),
                          ImGuiChildFlags_Borders);
        ImGui::TextWrapped(changelog_.c_str());
        ImGui::EndChild();
        const ImVec2 item_size = ImGui::GetItemRectSize();
        const ImVec2 end_pos = ImVec2(muli_input_text_pos.x + item_size.x,
                                      muli_input_text_pos.y + item_size.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRect(muli_input_text_pos, end_pos,
                           IM_COL32(50, 96, 168, 200), 0.0f, 0, 3.0f);
      }

      if (!latest_commit_date_.empty()) {
        ImGui::Text(fmt::format("Build Date: {}", latest_commit_date_).c_str());
      }

      ImGui::Spacing();

      if (downloading_) {
        if (!changelog_.empty()) {
          ImGui::Separator();
        }

        ImGui::ProgressBar(download_progress_, ImVec2(-1.0f, 0.0f));
        ImGui::Spacing();

        std::string downloading_lbl = "Downloading...";

        ImVec2 dl_lbl_size = ImGui::CalcTextSize(downloading_lbl.c_str());
        ImVec2 dl_btn_size = ImVec2(dl_lbl_size.x + btn_width_padding,
                                    dl_lbl_size.y + btn_height_padding);

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - dl_btn_size.x) * 0.5f);

        ImGui::BeginDisabled(true);
        ImGui::Button(downloading_lbl.c_str(), dl_btn_size);
        ImGui::EndDisabled();
      }

      if (downloaded_failed_) {
        std::string dl_failed_desc = "Downloading update failed, try again!";

        ImVec2 dl_lbl_size = ImGui::CalcTextSize(dl_failed_desc.c_str());
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - dl_lbl_size.x) * 0.5f);
        ImGui::Text(dl_failed_desc.c_str());

        if (download_response_code_ != HTTP_STATUS_CODE::HTTP_OK) {
          std::string error_code = fmt::format(
              "Error Code: {}", static_cast<int32_t>(download_response_code_));

          ImGui::SetCursorPosX((ImGui::GetWindowWidth() - dl_lbl_size.x) *
                               0.5f);
          ImGui::Text(error_code.c_str());
        }
      }

      if (!hide_download_button_) {
        if (!changelog_.empty()) {
          ImGui::Separator();
        }

        std::string dl_lbl =
            stable_toggle_ ? fmt::format("Download {}", stable_release_tag_)
                           : "Download";

        ImVec2 dl_lbl_size = ImGui::CalcTextSize(dl_lbl.c_str());
        ImVec2 dl_btn_size = ImVec2(dl_lbl_size.x + btn_width_padding,
                                    dl_lbl_size.y + btn_height_padding);

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - dl_btn_size.x) * 0.5f);

        if (ImGui::Button(dl_lbl.c_str(), dl_btn_size)) {
          downloaded_file_path_ =
              xe::filesystem::GetExecutableFolder() / windows_artifact_name_;
        }

        if (!downloaded_file_path_.empty()) {
          if (std::filesystem::exists(downloaded_file_path_) &&
              !replace_file_ && !show_replace_dialog_) {
            show_replace_dialog_ = true;
            ImGui::OpenPopup("Replace");
          }

          if (!show_replace_dialog_) {
            auto run = [this]() {
              auto callback = [this](double now, double total) {
                if (total > 0.0) {
                  download_progress_ = static_cast<float>(now / total);
                }
              };

              if (stable_toggle_) {
                download_response_code_ = updater_->DownloadLatestRelease(
                    std::string(windows_artifact_name_),
                    downloaded_file_path_.string(), callback);
              } else {
                download_response_code_ =
                    updater_->DownloadLatestNightlyArtifact(
                        "Windows_build", XE_BUILD_BRANCH,
                        std::string(windows_artifact_name_),
                        downloaded_file_path_.string(), callback);
              }

              if (download_response_code_ == HTTP_STATUS_CODE::HTTP_OK) {
                downloaded_ = true;
              } else {
                // If download failed show download button again to retry
                downloaded_ = false;
                downloaded_failed_ = true;
                hide_download_button_ = false;
                downloaded_file_path_ = "";
              }

              downloading_ = false;
              download_progress_ = 0.0f;
            };

            std::thread download = std::thread(run);
            download.detach();

            hide_download_button_ = true;
            downloaded_failed_ = false;
            downloading_ = true;
          }
        }
      }

      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(300, -1), ImVec2(300, -1));
      if (ImGui::BeginPopupModal("Replace", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar)) {
        float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                          (ImGui::GetStyle().ItemSpacing.x * 0.5f);

        const std::string desc =
            std::format("Replace existing {}?", windows_artifact_name_);

        ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
        ImGui::Text(desc.c_str());
        ImGui::Separator();

        std::string yes_lbl = "Yes";
        ImVec2 yes_lbl_size = ImGui::CalcTextSize(yes_lbl.c_str());
        ImVec2 yes_btn_size =
            ImVec2(btn_width, yes_lbl_size.y + btn_height_padding);

        if (ImGui::Button(yes_lbl.c_str(), yes_btn_size)) {
          replace_file_ = true;
          show_replace_dialog_ = false;
          ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        std::string cancel_lbl = "Cancel";
        ImVec2 cancel_lbl_size = ImGui::CalcTextSize(cancel_lbl.c_str());
        ImVec2 cancel_btn_size =
            ImVec2(btn_width, cancel_lbl_size.y + btn_height_padding);

        if (ImGui::Button(cancel_lbl.c_str(), cancel_btn_size)) {
          downloaded_file_path_ = "";
          show_replace_dialog_ = false;

          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      if (downloaded_) {
        if (!changelog_.empty()) {
          ImGui::Separator();
        }

        std::string apply_lbl = "Apply Update and Restart";
        ImVec2 apply_lbl_size = ImGui::CalcTextSize(apply_lbl.c_str());
        ImVec2 apply_btn_size = ImVec2(apply_lbl_size.x + btn_width_padding,
                                       apply_lbl_size.y + btn_height_padding);

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - apply_btn_size.x) *
                             0.5f);

        if (ImGui::Button(apply_lbl.c_str(), apply_btn_size)) {
          applying_update_failed_ =
              !updater_->UpdateAndRestart(downloaded_file_path_);

          if (!applying_update_failed_) {
            XELOGI("Applying update...");
            exit(0);
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Xenia will restart and apply the update.");
        }

        if (applying_update_failed_) {
          ImGui::Spacing();

          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
          ImGui::Text("Failed to apply update. Please try again.");
          ImGui::PopStyleColor();

          ImGui::Spacing();
        }
      }
    } else if (checked_for_updates_ && !update_available_) {
      switch (update_response_code_) {
        case HTTP_STATUS_CODE::HTTP_OK: {
          ImGui::Spacing();
          ImGui::Text("You're using latest build.");
          ImGui::Spacing();

          ImGui::Spacing();
          ImGui::Text("Build Details:");
          ImGui::Text(fmt::format("Branch: {}", XE_BUILD_BRANCH).c_str());
          ImGui::Text(fmt::format("Date: {}", XE_BUILD_DATE).c_str());
          ImGui::Text(fmt::format("Commit: {}", XE_BUILD_COMMIT_SHORT).c_str());

          ImGui::Spacing();
        } break;
        case HTTP_STATUS_CODE::HTTP_FORBIDDEN: {
          ImGui::Spacing();
          ImGui::Text("Failed to check for updates!");
          ImGui::Text("You're rate limited from GitHub, try again later.");
          ImGui::Spacing();
        } break;
        case HTTP_STATUS_CODE::HTTP_NOT_FOUND: {
          ImGui::Spacing();
          ImGui::Text("Failed to check for updates!");
          ImGui::Text(fmt::format("Branch '{}' doesn't exist.", XE_BUILD_BRANCH)
                          .c_str());
          ImGui::Spacing();
        } break;
        case -1: {
          ImGui::Spacing();
          ImGui::Text("Failed to check for updates!");
          ImGui::Text("Try Again!");
          ImGui::Spacing();
        } break;
        default: {
          std::string error_code = fmt::format(
              "Error Code: {}", static_cast<int32_t>(update_response_code_));

          ImGui::Spacing();
          ImGui::Text("Failed to check for updates!");
          ImGui::Text(error_code.c_str());
          ImGui::Spacing();
        } break;
      }
    }

    ImGui::SetWindowFontScale(1.0f);

    ImGui::EndPopup();
  }
#endif  //  DEBUG

  if (!updater_opened_) {
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleUpdaterDialog();
  }
}

bool UpdaterCompletionDialog::CopyFilePathToClipboard(
    const std::wstring& file_path) {
  std::u16string file_to_copy_path(file_path.begin(), file_path.end());

#ifdef XE_PLATFORM_WIN32
  size_t path_size =
      string_util::size_in_bytes(file_to_copy_path.c_str(), true);
  size_t buffer_size = path_size + sizeof(DROPFILES);

  HGLOBAL dropped_files_data_ptr = GlobalAlloc(GHND, buffer_size);

  if (!dropped_files_data_ptr) {
    return false;
  }

  DROPFILES* drop_files_ptr =
      reinterpret_cast<DROPFILES*>(GlobalLock(dropped_files_data_ptr));

  if (!drop_files_ptr) {
    GlobalFree(dropped_files_data_ptr);
    return false;
  }

  drop_files_ptr->pFiles = sizeof(DROPFILES);
  drop_files_ptr->fWide = TRUE;

  char16_t* files_ptr = reinterpret_cast<char16_t*>(drop_files_ptr + 1);
  memcpy(files_ptr, file_to_copy_path.c_str(), path_size);

  bool copied = false;

  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    copied = SetClipboardData(CF_HDROP, dropped_files_data_ptr);
  }

  CloseClipboard();
  GlobalUnlock(dropped_files_data_ptr);
  GlobalFree(dropped_files_data_ptr);

  return copied;
#else
  return false;
#endif
}

void UpdaterCompletionDialog::OnDraw(ImGuiIO& io) {
  if (!updater_completion_opened_) {
    updater_completion_opened_ = true;
    ImGui::OpenPopup("Update Failed");
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height_padding = ImGui::GetStyle().FramePadding.x * 4.0f;

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(
          "Update Failed", &updater_completion_opened_,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                      (ImGui::GetStyle().ItemSpacing.x * 0.5f);

    ImGui::SetWindowFontScale(1.05f);

    if (!updated_) {
      const std::string desc = "Automatic update failed.";
      ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);

      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
      ImGui::Text(desc.c_str());
      ImGui::PopStyleColor();

      ImGui::Separator();

      ImGui::Spacing();
      ImGui::Spacing();

      std::string update_lbl = "Try updating again?";

      ImVec2 update_lbl_size = ImGui::CalcTextSize(update_lbl.c_str());
      ImVec2 update_btn_size =
          ImVec2(btn_width, update_lbl_size.y + btn_height_padding);

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - update_btn_size.x) *
                           0.5f);
      if (ImGui::Button(update_lbl.c_str(), update_btn_size)) {
        updater_completion_opened_ = false;
        emulator_window_->ToggleUpdaterDialog();
      }

      ImGui::Spacing();
      ImGui::Spacing();

      std::string update_log_lbl = "View update log";

      ImVec2 update_log_lbl_size = ImGui::CalcTextSize(update_log_lbl.c_str());
      ImVec2 update_log_btn_size =
          ImVec2(btn_width, update_log_lbl_size.y + btn_height_padding);

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - update_log_btn_size.x) *
                           0.5f);
      if (ImGui::Button(update_log_lbl.c_str(), update_log_btn_size)) {
        show_update_log_ = true;
        ImGui::OpenPopup("Update Log");
      }

      // SetWindowFontScale causes vertical scroll bar to show so we use
      // ImGuiWindowFlags_NoScrollbar

      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(550, -1), ImVec2(550, -1));
      if (ImGui::BeginPopupModal("Update Log", &show_update_log_,
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar)) {
        const uint32_t lines = 20;
        float height = ImGui::GetTextLineHeight() * lines;

        const auto update_log_path =
            xe::filesystem::GetExecutableFolder() / update_log_filename_;

        std::stringstream buffer;
        std::stringstream::pos_type size;

        std::error_code ec;

        if (std::filesystem::exists(update_log_path, ec) && !ec) {
          std::ifstream log(update_log_path);

          buffer << log.rdbuf();
          size = log.tellg();
          log.close();
        } else {
          ImGui::Text(
              fmt::format("{} not found.", update_log_filename_).c_str());
          ImGui::Separator();
          ImGui::Spacing();
        }

        const ImVec2 muli_input_text_pos = ImGui::GetCursorScreenPos();

        ImGui::BeginChild("##UpdatelogChild", ImVec2(-1, height),
                          ImGuiChildFlags_Borders);
        ImGui::TextWrapped(buffer.str().c_str());
        ImGui::EndChild();
        const ImVec2 item_size = ImGui::GetItemRectSize();
        const ImVec2 end_pos = ImVec2(muli_input_text_pos.x + item_size.x,
                                      muli_input_text_pos.y + item_size.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRect(muli_input_text_pos, end_pos,
                           IM_COL32(50, 96, 168, 200), 0.0f, 0, 3.0f);

        std::string copy_btn = "";

#ifdef XE_PLATFORM_WIN32
        copy_btn = "Copy Log File";
#else
        copy_btn = "Copy Log Text";
#endif

        ImVec2 copy_log_lbl_size = ImGui::CalcTextSize(copy_btn.c_str());
        ImVec2 copy_log_btn_size =
            ImVec2(-1, copy_log_lbl_size.y + btn_height_padding);

        if (ImGui::Button(copy_btn.c_str(), copy_log_btn_size)) {
#ifdef XE_PLATFORM_WIN32
          bool copy_success =
              CopyFilePathToClipboard(update_log_path.wstring());

          if (!copy_success) {
            XELOGE(
                "Failed to copy file to clipboard. Copying the text instead.");
            ImGui::SetClipboardText(buffer.str().c_str());
          }
#else
          ImGui::SetClipboardText(buffer.str().c_str());
#endif
        }

        ImGui::EndPopup();
      }

      ImGui::Spacing();
      ImGui::Spacing();

      ImGui::Separator();

      ImGui::Text("To update Xenia Canary manually:");
      ImGui::Text(
          fmt::format("1. Extract the zip file: {}", windows_artifact_name_)
              .c_str());
      ImGui::Text("2. Replace the current Xenia executable with the new one.");
      ImGui::Text("3. Delete the zip file.");
    }

    ImGui::SetWindowFontScale(1.0f);

    ImGui::EndPopup();
  }

  if (!updater_completion_opened_) {
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleCompletionDialog();
  }
}

}  // namespace app
}  // namespace xe
