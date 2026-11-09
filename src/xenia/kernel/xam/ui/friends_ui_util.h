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

#include <cstddef>
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
constexpr float kFriendCardRounding = 4.0f;
constexpr ImVec2 kFriendCardPadding = ImVec2(6.0f, 4.0f);

constexpr ImU32 kFriendCardColor = IM_COL32(255, 255, 255, 12);
constexpr ImU32 kFriendCardHoveredColor = IM_COL32(50, 100, 200, 60);
constexpr ImU32 kFriendCardActiveColor = IM_COL32(50, 100, 200, 90);

constexpr ImU32 kAddFriendErrorColor = IM_COL32(240, 80, 70, 255);
constexpr ImU32 kAddFriendWarningColor = IM_COL32(230, 180, 60, 255);
constexpr ImU32 kAddFriendValidColor = IM_COL32(110, 200, 90, 255);

struct FriendStatus {
  bool online;
  const char* label;
  ImU32 color;
};

FriendStatus GetFriendStatus(uint32_t state);

ImVec2 HalfWidthButtonSize();

std::string FriendsCountString(uint64_t xuid);

bool PasteXUID(char* buffer, size_t buffer_size, const char* text);

void ShowFriendNotification(xe::ui::ImGuiDrawer* imgui_drawer,
                            std::string title, std::string description);

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UI_FRIENDS_UI_UTIL_H_
