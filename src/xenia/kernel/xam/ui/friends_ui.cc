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
  friends_presence_ = RefreshFriendsPresence(profile);
}

std::future<std::vector<FriendPresenceObjectJSON>>
FriendsUI::RefreshFriendsPresence(UserProfile* profile) {
  return std::async(std::launch::async, [profile]() {
    const uint8_t user_index =
        kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
            profile->xuid());

    return XLiveAPI::GetAllFriendsPresence(user_index);
  });
}

// TODO(Adrian): Move into a separate function so draw can be reused with dialog
// manager to reduce duplication.
void FriendsUI::OnDraw(ImGuiIO& io) {
  if (!args.friends_open) {
    args.first_draw = true;
    args.friends_open = true;

    ImGui::OpenPopup("Friends");

    if (XLiveAPI::IsConnectedToServer()) {
      args.filter_offline = true;
    }
  }

  if (friends_presence_.valid()) {
    if (friends_presence_.wait_for(0s) == std::future_status::ready) {
      friends_presence_result_ = friends_presence_.get();
    }
  }

  if (args.refresh_presence) {
    friends_presence_ = RefreshFriendsPresence(profile_);
    args.refresh_presence = false;
  }

  xeDrawFriendsContent(imgui_drawer(), profile_, args,
                       &friends_presence_result_);

  if (!args.friends_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
