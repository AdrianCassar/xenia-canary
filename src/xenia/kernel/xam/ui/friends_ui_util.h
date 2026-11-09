/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_FRIENDS_UI_UTIL_H_
#define XENIA_KERNEL_XAM_UI_FRIENDS_UI_UTIL_H_

#include <cstdint>
#include <string>

#include "third_party/imgui/imgui.h"

namespace xe {
namespace ui {
class ImGuiDrawer;
}  // namespace ui

namespace kernel {
namespace xam {
namespace ui {

constexpr float kFriendsButtonHeight = 25.0f;

ImVec2 HalfWidthButtonSize();

std::string FriendsCountString(uint64_t xuid);

void ShowFriendNotification(xe::ui::ImGuiDrawer* imgui_drawer,
                            std::string title, std::string description);

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UI_FRIENDS_UI_UTIL_H_
