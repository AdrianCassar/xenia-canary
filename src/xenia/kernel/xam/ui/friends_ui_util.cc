/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/friends_ui_util.h"

#include "third_party/fmt/include/fmt/format.h"

#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xnet.h"
#include "xenia/ui/imgui_host_notification.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

ImVec2 HalfWidthButtonSize() {
  return ImVec2((ImGui::GetContentRegionAvail().x * 0.5f) -
                    (ImGui::GetStyle().ItemSpacing.x * 0.5f),
                kFriendsButtonHeight);
}

std::string FriendsCountString(uint64_t xuid) {
  return fmt::format("{}/{}",
                     kernel_state()->friends_manager()->GetFriendsCount(xuid),
                     X_ONLINE_MAX_FRIENDS);
}

void ShowFriendNotification(xe::ui::ImGuiDrawer* imgui_drawer,
                            std::string title, std::string description) {
  kernel_state()->emulator()->display_window()->app_context().CallInUIThread(
      [imgui_drawer, title = std::move(title),
       description = std::move(description)]() {
        new xe::ui::HostNotificationWindow(imgui_drawer, title, description, 0);
      });
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
