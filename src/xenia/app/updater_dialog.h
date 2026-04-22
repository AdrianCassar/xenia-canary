/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_UPDATER_DIALOG_H_
#define XENIA_APP_UPDATER_DIALOG_H_

#include "xenia/app/updater.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;  // Forward declaration due to circular dependency

constexpr std::string_view windows_artifact_name_ =
    "xenia_canary_netplay_windows.zip";
constexpr std::string_view linux_artifact_name_ =
    "xenia_canary_netplay_linux.tar.xz";

class UpdaterDialog final : public ui::ImGuiDialog {
 public:
  UpdaterDialog(std::shared_ptr<Updater> updater, bool auto_check_update,
                ui::ImGuiDrawer* imgui_drawer, EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
    updater_ = updater;
    auto_check_update_ = auto_check_update;

#ifdef XE_PLATFORM_WIN32
    artifact_name_ = windows_artifact_name_;
#elif XE_PLATFORM_LINUX
    artifact_name_ = linux_artifact_name_;
#endif

    Initialize();
  }

  ~UpdaterDialog();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool ToggleButton(const char* str_id, bool* v);

  void ToggleStableState();

  void Initialize();

  enum class COMPARE_STATE { IDENTICAL, AHEAD, BEHIND, DIVERGED };

  bool updater_opened_ = false;
  bool auto_check_update_ = false;
  std::shared_ptr<Updater> updater_ = nullptr;
  std::future<CheckForUpdateInfo> update_available_future_ = {};
  CheckForUpdateInfo update_check_result_ = {};
  std::future<ChangelogInfo> changelog_info_future_ = {};
  ChangelogInfo changelog_result_ = {};
  std::future<uint32_t> download_future_ = {};
  uint32_t download_response_code_ = 0;
  std::atomic<bool> cancel_request = false;
  bool checked_for_updates_ = false;
  bool download_startup_pending = false;
  std::atomic<float> download_progress_ = 0.0f;
  bool downloaded_ = false;
  bool downloaded_failed_ = false;
  bool applying_update_failed_ = false;
  bool hide_download_button_ = false;
  bool show_replace_dialog_ = false;
  bool show_in_use_warning_dialog_ = false;
  bool replace_file_ = false;
  bool stable_toggle_ = false;
  std::filesystem::path downloaded_file_path_ = "";
  std::string artifact_name_ = "";
  std::string changelog_ = "";
  COMPARE_STATE compare_status_ = COMPARE_STATE::IDENTICAL;
  EmulatorWindow* emulator_window_ = nullptr;
};

class UpdaterCompletionDialog final : public ui::ImGuiDialog {
 public:
  UpdaterCompletionDialog(ui::ImGuiDrawer* imgui_drawer,
                          EmulatorWindow* emulator_window, bool updated)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
    updated_ = updated;

#ifdef XE_PLATFORM_WIN32
    artifact_name_ = windows_artifact_name_;
#elif XE_PLATFORM_LINUX
    artifact_name_ = linux_artifact_name_;
#endif
  }

 protected:
  bool CopyFilePathToClipboard(const std::wstring& file_path);
  void OnDraw(ImGuiIO& io) override;

  bool updater_completion_opened_ = false;
  bool show_update_log_ = false;
  bool updated_ = false;
  std::string artifact_name_ = "";
  std::string_view update_log_filename_ = "xenia_canary_update.log";
  EmulatorWindow* emulator_window_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_UPDATER_DIALOG_H_
