/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/imgui/imgui_internal.h"

#include "version.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/updater_dialog.h"
#include "xenia/base/logging.h"

#ifdef XE_PLATFORM_WIN32
#include <shlobj.h>
#include <windows.h>
#else
// TODO: Cross-platform alternatives for clipboard
#endif

namespace xe {
namespace app {

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
  if (gg.LastActiveId == gg.CurrentWindow->GetID(
                             str_id)) {  // && g.LastActiveIdTimer < ANIM_SPEED)
    float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
  }
  if (ImGui::IsItemHovered()) {
    draw_list->AddRectFilled(
        p, ImVec2(p.x + width, p.y + height),
        ImGui::GetColorU32(*v ? colors[ImGuiCol_ButtonActive]
                              : ImVec4(0.78f, 0.78f, 0.78f, 1.0f)),
        height * 0.5f);
  } else {
    draw_list->AddRectFilled(
        p, ImVec2(p.x + width, p.y + height),
        ImGui::GetColorU32(*v ? colors[ImGuiCol_Button]
                              : ImVec4(0.85f, 0.85f, 0.85f, 1.0f)),
        height * 0.50f);
  }
  draw_list->AddCircleFilled(
      ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f),
             p.y + radius),
      radius - 1.5f, IM_COL32(255, 255, 255, 255));

  return clicked;
}

void UpdaterDialog::ToggleStableState() {
  // Reset current data if toggled
  update_check_result_ = {};
  changelog_result_ = {};
  download_response_code_ = 0;
  checked_for_updates_ = false;
  replace_file_ = false;
  compare_status_ = COMPARE_STATE::IDENTICAL;
  changelog_.clear();

  // Download state reset
  downloaded_ = false;
  downloaded_failed_ = false;
  download_startup_pending = false;
  applying_update_failed_ = false;
  hide_download_button_ = false;
  download_progress_ = 0.0f;
  downloaded_file_path_.clear();
}

void UpdaterDialog::Initialize() {
  if (auto_check_update_) {
    auto_check_update_ = false;  // Check once

    update_available_future_ = updater_->CheckForUpdatesAsync(
        stable_toggle_, XE_BUILD_BRANCH, cancel_request);
  }
}

UpdaterDialog::~UpdaterDialog() {
  if (download_future_.valid() && !downloaded_) {
    cancel_request = true;
  }

  if (cancel_request) {
    // Wait for the partially downloaded file to no longer be in use.
    download_future_.wait();

    // Delete the partially downloaded file if download was cancelled
    std::error_code ec;

    if (!downloaded_file_path_.empty() &&
        std::filesystem::exists(downloaded_file_path_, ec)) {
      std::filesystem::remove(downloaded_file_path_, ec);
    }

    if (ec) {
      XELOGE(
          "Failed to delete partially downloaded file after "
          "cancellation {}: {}",
          artifact_name_, ec.message());
    }
  }
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
  if (changelog_.empty() ||
      (checked_for_updates_ && !update_check_result_.update_available)) {
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

    ImGui::BeginGroup();

    std::string update_desc = stable_toggle_ ? "Check for Stable Updates"
                                             : "Check for Nightly Updates";
    ImVec2 update_lbl_size = ImGui::CalcTextSize(update_desc.c_str());
    ImVec2 update_btn_size = ImVec2(update_lbl_size.x + btn_width_padding,
                                    update_lbl_size.y + btn_height_padding);

    ImGui::BeginDisabled(update_available_future_.valid() ||
                         changelog_info_future_.valid());
    if (ImGui::Button(update_desc.c_str(), update_btn_size)) {
      update_available_future_ = updater_->CheckForUpdatesAsync(
          stable_toggle_, XE_BUILD_BRANCH, cancel_request);
    }
    ImGui::EndDisabled();

    if (update_available_future_.valid()) {
      if (update_available_future_.wait_for(0ms) == std::future_status::ready) {
        checked_for_updates_ = true;

        update_check_result_ = update_available_future_.get();

        if (update_check_result_.update_available) {
          if (stable_toggle_) {
            changelog_info_future_ = updater_->GetChangelogBetweenCommitsAsync(
                XE_BUILD_COMMIT, update_check_result_.metadata.tag,
                cancel_request);
          } else {
            changelog_info_future_ = updater_->GetChangelogBetweenCommitsAsync(
                XE_BUILD_COMMIT, update_check_result_.metadata.commit_hash,
                cancel_request);
          }
        }
      }
    }

    if (changelog_info_future_.valid()) {
      if (changelog_info_future_.wait_for(0ms) == std::future_status::ready) {
        changelog_result_ = changelog_info_future_.get();

        const std::vector<std::string> commit_messages =
            changelog_result_.messages.messages;

        if (changelog_result_.messages.status == "identical") {
          compare_status_ = COMPARE_STATE::IDENTICAL;
        } else if (changelog_result_.messages.status == "ahead") {
          compare_status_ = COMPARE_STATE::AHEAD;
        } else if (changelog_result_.messages.status == "behind") {
          compare_status_ = COMPARE_STATE::BEHIND;
        } else if (changelog_result_.messages.status == "diverged") {
          compare_status_ = COMPARE_STATE::DIVERGED;
        }

        if (!commit_messages.empty()) {
          changelog_.clear();
          for (const auto& message : commit_messages) {
            changelog_.append(fmt::format("- {}\n", message));
          }
        }
      }
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

    auto btn_toggle_start_cursor = ImGui::GetCursorPos();

    ImGui::BeginGroup();
    if (ToggleButton("ToggleStable", &stable_toggle_)) {
      ToggleStableState();
    }
    ImGui::EndGroup();

    auto btn_toggle_end_cursor = ImGui::GetCursorPos();

    ImGui::SetCursorPos(btn_toggle_start_cursor);

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(50, 100, 200, 50));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    if (ImGui::Selectable("##ToggleStableState", false,
                          ImGuiSelectableFlags_NoAutoClosePopups,
                          ImGui::GetItemRectSize())) {
      stable_toggle_ = !stable_toggle_;
      ToggleStableState();
    }
    ImGui::PopStyleColor(2);

    ImGui::SetCursorPos(btn_toggle_end_cursor);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (update_available_future_.valid() || changelog_info_future_.valid()) {
      ImGui::Text("Checking for updates...");
      ImGui::Spacing();
    }

    if (checked_for_updates_ && update_check_result_.update_available &&
        !update_available_future_.valid() && !changelog_info_future_.valid()) {
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

      if (stable_toggle_) {
        if (!changelog_.empty()) {
          ImGui::Spacing();
        }

        ImGui::TextLinkOpenURL(
            fmt::format("Release {} details", update_check_result_.metadata.tag)
                .c_str(),
            "https://github.com/AdrianCassar/xenia-canary/releases/latest");

        ImGui::Spacing();
      }

      if (!update_check_result_.metadata.commit_date.empty()) {
        ImGui::Text(fmt::format("Build Date: {}",
                                update_check_result_.metadata.commit_date)
                        .c_str());
      }

      ImGui::Spacing();

      if (download_future_.valid()) {
        ImGui::Separator();

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
        ImGui::Separator();

        std::string dl_failed_desc = "Download failed try again!";
        std::string error_code = fmt::format(
            "Error Code: {}", static_cast<int32_t>(download_response_code_));

        ImGui::Text(dl_failed_desc.c_str());
        ImGui::Text(error_code.c_str());
      }

      if (!hide_download_button_) {
        ImGui::Separator();

        std::string dl_lbl =
            stable_toggle_
                ? fmt::format("Download {}", update_check_result_.metadata.tag)
                : "Download";

        ImVec2 dl_lbl_size = ImGui::CalcTextSize(dl_lbl.c_str());
        ImVec2 dl_btn_size = ImVec2(dl_lbl_size.x + btn_width_padding,
                                    dl_lbl_size.y + btn_height_padding);

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - dl_btn_size.x) * 0.5f);

        if (ImGui::Button(dl_lbl.c_str(), dl_btn_size)) {
          downloaded_file_path_ =
              xe::filesystem::GetExecutableFolder() / artifact_name_;

          if (std::filesystem::exists(downloaded_file_path_) &&
              !replace_file_) {
            show_replace_dialog_ = true;
            ImGui::OpenPopup("Replace");
          }

          download_startup_pending = true;
        }
      }

      if (download_startup_pending && !downloaded_ &&
          !download_future_.valid() && !show_replace_dialog_ &&
          !downloaded_file_path_.empty()) {
        const bool exists = std::filesystem::exists(downloaded_file_path_);

        if (!exists || replace_file_) {
          auto callback = [this](double now, double total) {
            if (total > 0.0) {
              download_progress_ = static_cast<float>(now / total);
            }
          };

          if (stable_toggle_) {
            download_future_ = updater_->DownloadLatestReleaseAsync(
                artifact_name_, downloaded_file_path_.string(), cancel_request,
                callback);
          } else {
            download_future_ = updater_->DownloadLatestNightlyArtifactAsync(
                "Orchestrator", XE_BUILD_BRANCH, artifact_name_,
                downloaded_file_path_.string(), cancel_request, callback);
          }

          hide_download_button_ = true;
          downloaded_ = false;
          downloaded_failed_ = false;
        } else {
          replace_file_ = false;
        }

        download_startup_pending = false;
      }

      if (download_future_.valid()) {
        if (download_future_.wait_for(0ms) == std::future_status::ready) {
          download_response_code_ = download_future_.get();

          if (download_response_code_ == HTTP_STATUS_CODE::HTTP_OK) {
            downloaded_ = true;
          } else {
            // If download failed show download button again to retry
            downloaded_ = false;
            downloaded_failed_ = true;
            hide_download_button_ = false;
            downloaded_file_path_.clear();
            download_progress_ = 0.0f;
            replace_file_ = false;
          }
        }
      }

      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(300, -1), ImVec2(300, -1));
      if (ImGui::BeginPopupModal("Replace", &show_replace_dialog_,
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar)) {
        float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                          (ImGui::GetStyle().ItemSpacing.x * 0.5f);

        const std::string desc =
            std::format("Replace existing {}?", artifact_name_);

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
          ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        std::string cancel_lbl = "Cancel";
        ImVec2 cancel_lbl_size = ImGui::CalcTextSize(cancel_lbl.c_str());
        ImVec2 cancel_btn_size =
            ImVec2(btn_width, cancel_lbl_size.y + btn_height_padding);

        if (ImGui::Button(cancel_lbl.c_str(), cancel_btn_size)) {
          downloaded_file_path_.clear();
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      if (downloaded_) {
        ImGui::Separator();

        std::string apply_lbl = "Apply Update and Restart";
        ImVec2 apply_lbl_size = ImGui::CalcTextSize(apply_lbl.c_str());
        ImVec2 apply_btn_size = ImVec2(apply_lbl_size.x + btn_width_padding,
                                       apply_lbl_size.y + btn_height_padding);

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - apply_btn_size.x) *
                             0.5f);

        if (ImGui::Button(apply_lbl.c_str(), apply_btn_size)) {
          bool try_update = false;

#ifdef XE_PLATFORM_WIN32
          try_update = updater_->IsAnotherInstanceRunning();
#endif

          if (try_update) {
            show_in_use_warning_dialog_ = true;
            ImGui::OpenPopup("Multiple Instances Detected");
          } else {
            applying_update_failed_ =
                !updater_->UpdateAndRestart(downloaded_file_path_);

            if (!applying_update_failed_) {
              XELOGI("Applying update...");
              exit(0);
            }
          }
        }

        if (show_in_use_warning_dialog_) {
          ImGuiViewport* viewport = ImGui::GetMainViewport();
          ImVec2 center = viewport->GetCenter();
          ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
          ImGui::SetNextWindowSizeConstraints(ImVec2(300, 110),
                                              ImVec2(400, 300));
        }

        if (ImGui::BeginPopupModal(
                "Multiple Instances Detected", &show_in_use_warning_dialog_,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar)) {
          float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                            (ImGui::GetStyle().ItemSpacing.x * 0.5f);

          ImGui::Text("Multiple instances of Xenia Canary are running.");
          ImGui::Spacing();
          ImGui::TextWrapped(
              "Please close all other instances before applying the update.");
          ImGui::Spacing();

          std::string ok_lbl = "OK";

          ImVec2 ok_lbl_size = ImGui::CalcTextSize(ok_lbl.c_str());
          ImVec2 ok_btn_size =
              ImVec2(btn_width, ok_lbl_size.y + btn_height_padding);

          ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ok_btn_size.x) *
                               0.5f);

          if (ImGui::Button(ok_lbl.c_str(), ok_btn_size)) {
            ImGui::CloseCurrentPopup();
            show_in_use_warning_dialog_ = false;
          }

          ImGui::EndPopup();
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
    } else if (checked_for_updates_ && !update_check_result_.update_available) {
      switch (update_check_result_.metadata.response_code) {
        case HTTP_STATUS_CODE::HTTP_OK: {
          ImGui::Spacing();
          ImGui::Text("You're using latest build.");
          ImGui::Spacing();

          ImGui::Spacing();
          ImGui::Text("Build Details:");
          ImGui::TextWrapped(
              fmt::format("Branch: {}", XE_BUILD_BRANCH).c_str());
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
        case static_cast<uint32_t>(-1): {
          ImGui::Spacing();
          ImGui::Text("Failed to check for updates!");
          ImGui::Text("Try Again!");
          ImGui::Spacing();
        } break;
        default: {
          std::string error_code =
              fmt::format("Error Code: {}",
                          static_cast<int32_t>(
                              update_check_result_.metadata.response_code));

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
    Close();
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
          fmt::format("1. Extract the zip file: {}", artifact_name_).c_str());
      ImGui::Text("2. Replace the current Xenia executable with the new one.");
      ImGui::Text("3. Delete the zip file.");
    }

    ImGui::SetWindowFontScale(1.0f);

    ImGui::EndPopup();
  }

  if (!updater_completion_opened_) {
    Close();
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleCompletionDialog();
  }
}

}  // namespace app
}  // namespace xe
