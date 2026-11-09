/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/friends_ui_util.h"

#include <algorithm>
#include <cctype>

#include "third_party/fmt/include/fmt/format.h"

#include "xenia/base/string_util.h"
#include "xenia/base/utf8.h"
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

FriendStatus GetFriendStatus(uint32_t state) {
  if (!(state & X_ONLINE_FRIENDSTATE_FLAG_ONLINE)) {
    return {false, "Offline", IM_COL32(140, 140, 140, 255)};
  }

  switch (state & X_ONLINE_FRIENDSTATE_MASK_USER_STATE) {
    case X_ONLINE_FRIENDSTATE_ENUM_AWAY:
      return {true, "Away", IM_COL32(230, 180, 60, 255)};
    case X_ONLINE_FRIENDSTATE_ENUM_BUSY:
      return {true, "Busy", IM_COL32(220, 80, 70, 255)};
    default:
      return {true, "Online", IM_COL32(110, 200, 90, 255)};
  }
}

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

bool PasteXUID(char* buffer, size_t buffer_size, const char* text) {
  std::string xuid = string_util::trim(text);

  if (xuid.starts_with("0x") || xuid.starts_with("0X")) {
    xuid.erase(0, 2);
  }

  if (xuid.empty() || xuid.length() >= buffer_size ||
      !std::ranges::all_of(xuid,
                           [](unsigned char c) { return std::isxdigit(c); })) {
    return false;
  }

  string_util::copy_truncating(buffer, utf8::upper_ascii(xuid), buffer_size);
  return true;
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
