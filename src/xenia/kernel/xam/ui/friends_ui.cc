/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/friends_ui.h"
#include "xenia/kernel/XLiveAPI.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

FriendsUI::FriendsUI(xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile)
    : XamDialog(imgui_drawer), profile_(profile) {}

void FriendsUI::OnDraw(ImGuiIO& io) {
  if (!args.friends_open) {
    args.first_draw = true;
    args.refresh_presence_sync = true;
    args.friends_open = true;

    ImGui::OpenPopup("Friends");

    if (XLiveAPI::IsConnectedToServer()) {
      args.filter_offline = true;
    }
  }

  xeDrawFriendsContent(imgui_drawer(), profile_, args, &presences);

  if (!args.friends_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
