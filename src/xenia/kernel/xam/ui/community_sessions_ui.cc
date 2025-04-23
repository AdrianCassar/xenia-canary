/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/community_sessions_ui.h"
#include "xenia/kernel/XLiveAPI.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

ShowCommunitySessionsUI::ShowCommunitySessionsUI(
    xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile)
    : XamDialog(imgui_drawer), profile_(profile) {
  sessions_ = XLiveAPI::GetTitleSessions();
}

void ShowCommunitySessionsUI::OnDraw(ImGuiIO& io) {
  if (!sessions_args.sessions_open) {
    sessions_args.sessions_open = true;
    sessions_args.filter_own = true;
    ImGui::OpenPopup("Sessions");
  }

  xeDrawSessionsContent(imgui_drawer(), profile_, sessions_args, &sessions_);

  if (!sessions_args.sessions_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
