/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_PORT_PERMISSION_DIALOG_H_
#define XENIA_APP_PORT_PERMISSION_DIALOG_H_

#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX

#include <future>

#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class PortPermissionDialog final : public ui::ImGuiDialog {
 public:
  explicit PortPermissionDialog(ui::ImGuiDrawer* imgui_drawer);

  static void ShowIfNeeded(ui::ImGuiDrawer* imgui_drawer);

  static bool CanBindRequiredPorts();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void RunFix(bool persistent);
  // Returns true if the fix succeeded and the dialog is closing.
  bool PollFix();

  std::future<int> fix_result_;
  std::string status_message_;
};

}  // namespace app
}  // namespace xe

#endif  // XE_PLATFORM_LINUX

#endif  // XENIA_APP_PORT_PERMISSION_DIALOG_H_
