/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_NETPLAY_MANAGER_UI_H_
#define XENIA_KERNEL_XAM_UI_NETPLAY_MANAGER_UI_H_

#include "third_party/imgui/imgui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

struct AddFriendArgs {
  bool add_friend_open = false;
  bool add_friend_first_draw = false;
  bool search_filter_context_open = false;
  bool add_friend_context_open = false;
  bool added_friend = false;
  bool are_friends = false;
  bool valid_xuid = false;
  char add_xuid_[17] = {};
};

struct FriendsContentArgs {
  bool first_draw = false;
  bool friends_open = false;
  bool filter_joinable = false;
  bool filter_title = false;
  bool filter_offline = false;
  bool refresh_presence = false;
  bool queue_refresh_presence = false;
  uint64_t refocus_xuid = 0;
  AddFriendArgs add_friend_args = {};
  ImGuiTextFilter filter = {};
};

struct SessionsContentArgs {
  bool first_draw = false;
  bool sessions_open = false;
  bool filter_own = false;
  bool refresh_sessions = false;
  bool refresh_sessions_sync = false;
};

struct MyDeletedProfilesArgs {
  bool first_draw = false;
  bool deleted_profiles_open = false;
};

struct UPnPAndPortsArgs {
  bool first_draw = false;
  bool dialog_open = false;
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
