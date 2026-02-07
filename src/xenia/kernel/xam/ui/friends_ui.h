/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_FRIENDS_UI_H_
#define XENIA_KERNEL_XAM_UI_FRIENDS_UI_H_

#include <future>

#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

class FriendsUI : public XamDialog {
 public:
  FriendsUI(xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile);

 private:
  void OnDraw(ImGuiIO& io) override;

  UserProfile* profile_;
  ui::FriendsContentArgs args = {};
  std::future<std::vector<FriendPresenceObjectJSON>> friends_presence_;
  std::vector<FriendPresenceObjectJSON> friends_presence_result_;
  std::future<std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>>
      immediate_gamerpics_;
  std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>
      immediate_gamerpics_result_;
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
