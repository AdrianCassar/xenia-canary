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
    : XamDialog(imgui_drawer), profile_(profile) {
  friends_ui_args_.friends_presence =
      kernel_state()->xam_state()->presence_manager()->GetFriendsPresenceSorted(
          profile_->xuid());

  friends_ui_args_.immediate_gamerpics =
      kernel_state()->GetXboxLiveAPI()->GetFriendsGamerpicsAsync(
          profile->xuid(), imgui_drawer);
}

void FriendsUI::OnDraw(ImGuiIO& io) {
  if (!friends_ui_args_.content_args.friends_open) {
    friends_ui_args_.content_args.first_draw = true;
    friends_ui_args_.content_args.friends_open = true;

    if (kernel_state()->GetXboxLiveAPI()->IsConnectedToServer()) {
      friends_ui_args_.content_args.filter_offline = true;
    }

    ImGui::OpenPopup("Friends");
  }

  if (!xeDrawFriendsUI(imgui_drawer(), profile_, friends_ui_args_)) {
    friends_ui_args_.content_args.first_draw = false;
    Close();
  }
}

bool xeDrawFriendsUI(xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
                     FriendsUIArgs& friends_ui_args) {
  if (!profile) {
    return false;
  }

  // Automatically sync/update friends presence information.
  friends_ui_args.friends_presence =
      kernel_state()->xam_state()->presence_manager()->GetFriendsPresenceSorted(
          profile->xuid());

  if (friends_ui_args.content_args.refresh_presence) {
    friends_ui_args.content_args.refresh_presence = false;

    friends_ui_args.friends_presence_sync =
        kernel_state()->presence_manager()->SyncPresenceAsync(profile->xuid());

    friends_ui_args.immediate_gamerpics =
        kernel_state()->GetXboxLiveAPI()->GetFriendsGamerpicsAsync(
            profile->xuid(), imgui_drawer);
  }

  if (friends_ui_args.immediate_gamerpics.valid()) {
    if (friends_ui_args.immediate_gamerpics.wait_for(0s) ==
        std::future_status::ready) {
      friends_ui_args.immediate_gamerpics_result =
          friends_ui_args.immediate_gamerpics.get();
    }
  }

  xeDrawFriendsContent(imgui_drawer, profile, friends_ui_args.content_args,
                       friends_ui_args.friends_presence,
                       friends_ui_args.immediate_gamerpics_result);

  return friends_ui_args.content_args.friends_open;
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
