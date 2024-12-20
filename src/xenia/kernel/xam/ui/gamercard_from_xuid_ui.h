/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_GAMERCARD_FROM_XUID_UI_H_
#define XENIA_KERNEL_XAM_UI_GAMERCARD_FROM_XUID_UI_H_

#include "xenia/kernel/json/friend_presence_object_json.h"
#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

class GamercardFromXUIDUI : public XamDialog {
 public:
  GamercardFromXUIDUI(xe::ui::ImGuiDrawer* imgui_drawer, const uint64_t xuid,
                      UserProfile* profile);

 private:
  void OnDraw(ImGuiIO& io) override;

  bool has_opened_ = false;
  bool is_self = false;
  bool are_friends = false;
  std::string title_;
  const uint64_t xuid_;
  UserProfile* profile_;
  FriendPresenceObjectJSON presence_;
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
