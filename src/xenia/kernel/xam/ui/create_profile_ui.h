/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_CREATE_PROFILE_UI_H_
#define XENIA_KERNEL_XAM_UI_CREATE_PROFILE_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

#include <future>

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

struct CreateProfileUIArgs {
  bool dialog_open = false;
  bool migration = false;
  char gamertag[16] = {};
  bool live_enabled = true;
  bool valid_gamertag = false;
  std::shared_future<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
      downloaded_gamerpics;
  std::unique_ptr<xe::ui::ImmediateTexture> big_gamerpic_texture;
  std::optional<GamerPictureKey> gamerpic_key;
};

class CreateProfileUI final : public XamDialog {
 public:
  CreateProfileUI(xe::ui::ImGuiDrawer* imgui_drawer, Emulator* emulator,
                  bool with_migration = false)
      : XamDialog(imgui_drawer), emulator_(emulator) {
    create_profile_args_.migration = with_migration;
    Initalize();
  }

  ~CreateProfileUI() = default;

  static std::optional<GamerPictureKey> GetDefaultGamerPictureKey();

 private:
  void OnDraw(ImGuiIO& io) override;

  void Initalize();

  Emulator* emulator_;
  CreateProfileUIArgs create_profile_args_ = {};
};

bool xeDrawCreateProfile(xe::ui::ImGuiDrawer* imgui_drawer, Emulator* emulator,
                         CreateProfileUIArgs& args);

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
