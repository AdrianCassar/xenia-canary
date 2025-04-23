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

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
