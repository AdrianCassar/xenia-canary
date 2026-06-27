/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_PROFILE_DIALOGS_H_
#define XENIA_APP_PROFILE_DIALOGS_H_

#include <future>

#include "xenia/kernel/json/friend_presence_object_json.h"
#include "xenia/kernel/json/session_object_json.h"
#include "xenia/kernel/xam/ui/netplay_manager_util.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

class EmulatorWindow;

class NoProfileDialog final : public ui::ImGuiDialog {
 public:
  NoProfileDialog(ui::ImGuiDrawer* imgui_drawer,
                  EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

  EmulatorWindow* emulator_window_;
};

class ProfileConfigDialog final : public ui::ImGuiDialog {
 public:
  ProfileConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                      EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
    LoadProfileIcon();
    LoadSignedInProfilesCount();
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void LoadProfileIcon();
  void LoadProfileIcon(const uint64_t xuid);

  void LoadSignedInProfilesCount();

  std::map<uint64_t, std::unique_ptr<ui::ImmediateTexture>> profile_icon_;
  std::map<uint64_t, kernel::xam::GamerPictureKey> profile_gamerpic_key_;

  uint64_t selected_xuid_ = 0;
  uint8_t signed_in_profiles_count_ = 0;
  EmulatorWindow* emulator_window_;
};

class ManagerDialog final : public ui::ImGuiDialog {
 public:
  ManagerDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
    Initalize(imgui_drawer, 0);
  }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void Initalize(ui::ImGuiDrawer* imgui_drawer, uint32_t user_index);

 private:
  bool manager_opened_ = false;
  uint64_t selected_xuid_ = 0;
  uint64_t removed_xuid_ = 0;
  Emulator* emulator_;
  xe::kernel::xam::ui::FriendsContentArgs friends_args = {};
  xe::kernel::xam::ui::SessionsContentArgs sessions_args = {};
  xe::kernel::xam::ui::MyDeletedProfilesArgs deletion_args = {};
  xe::kernel::xam::ui::UPnPAndPortsArgs upnp_and_ports_args = {};
  std::vector<std::unique_ptr<xe::kernel::SessionObjectJSON>> sessions;
  std::map<uint64_t, std::string> deleted_profiles;
  EmulatorWindow* emulator_window_;
  std::future<std::vector<xe::kernel::FriendPresenceObjectJSON>>
      friends_presence_;
  std::vector<xe::kernel::FriendPresenceObjectJSON> friends_presence_result_;
  std::future<std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>>
      immediate_gamerpics_;
  std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>
      immediate_gamerpics_result_;
};

}  // namespace app
}  // namespace xe

#endif
