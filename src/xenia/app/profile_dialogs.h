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

#include "xenia/kernel/json/friend_presence_object_json.h"
#include "xenia/kernel/json/session_object_json.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {
struct AddFriendArgs {
  bool add_friend_open;
  bool add_friend_first_draw;
  bool added_friend;
  bool are_friends;
  bool valid_xuid;
  char add_xuid_[17];
};

struct FriendsContentArgs {
  bool first_draw;
  bool friends_open;
  bool filter_joinable;
  bool filter_title;
  bool filter_offline;
  bool refersh_presence;
  bool refersh_presence_sync;
  AddFriendArgs add_friend_args = {};
  ImGuiTextFilter filter = {};
};
struct SessionsContentArgs {
  bool first_draw;
  bool sessions_open;
  bool filter_own;
  bool refersh_sessions;
  bool refersh_sessions_sync;
};
}  // namespace xam
}  // namespace kernel
namespace app {

class EmulatorWindow;

class CreateProfileDialog final : public ui::ImGuiDialog {
 public:
  CreateProfileDialog(ui::ImGuiDrawer* imgui_drawer,
                      EmulatorWindow* emulator_window,
                      bool with_migration = false)
      : ui::ImGuiDialog(imgui_drawer),
        emulator_window_(emulator_window),
        migration_(with_migration) {
    memset(gamertag_, 0, sizeof(gamertag_));
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

  bool has_opened_ = false;
  bool migration_ = false;
  char gamertag_[16] = "";
  bool live_enabled = true;
  EmulatorWindow* emulator_window_;
};

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
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  uint64_t selected_xuid_ = 0;
  EmulatorWindow* emulator_window_;
};

class ManagerDialog final : public ui::ImGuiDialog {
 public:
  ManagerDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool manager_opened_ = false;
  uint64_t selected_xuid_ = 0;
  uint64_t removed_xuid_ = 0;
  xe::kernel::xam::FriendsContentArgs args = {};
  xe::kernel::xam::SessionsContentArgs sessions_args = {};
  std::vector<xe::kernel::FriendPresenceObjectJSON> presences;
  std::vector<std::unique_ptr<xe::kernel::SessionObjectJSON>> sessions;
  EmulatorWindow* emulator_window_;
};

}  // namespace app
}  // namespace xe

#endif
