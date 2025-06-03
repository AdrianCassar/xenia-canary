/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>

#include "build/version.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/profile_dialogs.h"
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

void UpdaterDialog::OnDraw(ImGuiIO& io) {
  if (!updater_opened_) {
    updater_opened_ = true;
    ImGui::OpenPopup("Updater");
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height = 25;
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(350, -1), ImVec2(350, -1));
  if (ImGui::BeginPopupModal("Updater", &updater_opened_,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImVec2 popup_size = ImGui::GetWindowSize();
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImVec2 pos(center.x - popup_size.x * 0.5f, center.y - popup_size.y * 0.5f);
    ImGui::SetWindowPos(pos);
#ifdef DEBUG
    ImGui::Text("This is a debug build, therefore updates are unavailable.");
    ImGui::EndPopup();
  }
#else
    std::string update_desc = "Check for Nightly Updates";
    ImVec2 update_desc_size = ImGui::CalcTextSize(update_desc.c_str());

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - update_desc_size.x) * 0.5f);

    if (ImGui::Button(update_desc.c_str())) {
      checked_for_updates_ = true;

      update_available_ =
          updater_->CheckForUpdates(XE_BUILD_BRANCH, &latest_commit_hash_,
                                    &latest_commit_date_, &response_code_);

      if (response_code_ != HTTP_STATUS_CODE::HTTP_OK) {
        update_available_ = false;
      }

      if (update_available_) {
        commit_messages_.clear();

        const uint32_t result = updater_->GetChangelogBetweenCommits(
            XE_BUILD_COMMIT, latest_commit_hash_, commit_messages_);

        if (result == HTTP_STATUS_CODE::HTTP_OK) {
          if (!commit_messages_.empty()) {
            changelog_.clear();
          }

          for (const auto& message : commit_messages_) {
            changelog_.append(fmt::format("- {}\n", message));
          }
        }
      }

      checked_for_updates_ = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (checked_for_updates_ && update_available_) {
      const uint32_t lines = 10;
      float height = ImGui::GetTextLineHeight() * lines;

      if (!changelog_.empty()) {
        ImGui::Text("Changelog:");

        const ImVec2 muli_input_text_pos = ImGui::GetCursorScreenPos();

        ImGui::InputTextMultiline("##Changelog",
                                  const_cast<char*>(changelog_.c_str()),
                                  changelog_.size() + 1, ImVec2(-1, height),
                                  ImGuiInputTextFlags_ReadOnly);

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

      ImGui::BeginDisabled(true);
      if (downloading_) {
        ImGui::Button("Downloading...");
      }
      ImGui::EndDisabled();

      if (!hide_download_button_) {
        if (ImGui::Button("Download Nightly")) {
          auto file_picker = xe::ui::FilePicker::Create();
          file_picker->set_mode(xe::ui::FilePicker::Mode::kOpen);
          file_picker->set_type(xe::ui::FilePicker::Type::kDirectory);
          file_picker->set_multi_selection(false);
          file_picker->set_title("Download Directory");

          if (file_picker->Show(emulator_window_->window())) {
            auto selected_files = file_picker->selected_files();
            if (!selected_files.empty()) {
              downloaded_file_path_ = selected_files[0];
            }
          }

          if (!downloaded_file_path_.empty()) {
            downloaded_file_path_ =
                downloaded_file_path_ / windows_artifact_name_;
          }
        }

        if (!downloaded_file_path_.empty()) {
          if (std::filesystem::exists(downloaded_file_path_) &&
              !replace_file_ && !show_replace_dialog_) {
            show_replace_dialog_ = true;
            ImGui::OpenPopup("Replace");
          }

          if (!show_replace_dialog_) {
            auto run = [this]() {
              uint32_t response = updater_->DownloadLatestNightlyArtifact(
                  "Windows_build", XE_BUILD_BRANCH, windows_artifact_name_,
                  downloaded_file_path_.string());

              downloading_ = false;

              if (response == HTTP_STATUS_CODE::HTTP_OK) {
                downloaded_ = true;
              } else {
                downloaded_failed_ = true;
                downloaded_ = false;
              }
            };

            std::thread download = std::thread(run);
            download.detach();

            hide_download_button_ = true;
            downloading_ = true;
          }
        }
      }

      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(300, 90), ImVec2(300, 90));
      if (ImGui::BeginPopupModal("Replace", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                          (ImGui::GetStyle().ItemSpacing.x * 0.5f);
        ImVec2 btn_size = ImVec2(btn_width, btn_height);

        const std::string desc =
            std::format("Replace existing {}?", windows_artifact_name_);

        ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
        ImGui::Text(desc.c_str());
        ImGui::Separator();

        if (ImGui::Button("Yes", btn_size)) {
          replace_file_ = true;
          show_replace_dialog_ = false;
          ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", btn_size)) {
          downloaded_file_path_ = "";
          show_replace_dialog_ = false;

          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      if (downloaded_) {
        ImGui::Separator();

#ifdef XE_PLATFORM_WIN32
        if (ImGui::Button("Open downloaded zip")) {
          LPCWSTR path = reinterpret_cast<LPCWSTR>(
              downloaded_file_path_.u16string().data());

          ShellExecute(0, 0, path, 0, 0, SW_SHOW);
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          ImGui::SetTooltip("Extract archive and replace to complete update.");
        }
#else
        ImGui::Text("Download Complete!");
        ImGui::Text("Manually extract archive and replace to complete update.");
#endif  // DEBUG
      }

    } else if (checked_for_updates_ && !update_available_) {
      switch (response_code_) {
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
          ImGui::Spacing();
          ImGui::Text("Failed to check for updates!");
          ImGui::Text(fmt::format("Error Code: {}", response_code_).c_str());
          ImGui::Spacing();
        } break;
      }
    } else if (downloaded_failed_ && !downloading_) {
      ImGui::Spacing();
      ImGui::Text("Failed to check for updates!");
      ImGui::Text("Try Again!");
      ImGui::Spacing();
    }

    ImGui::EndPopup();
  }
#endif  //  DEBUG

  if (!updater_opened_) {
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleUpdaterDialog();
  }
}

}  // namespace app
}  // namespace xe