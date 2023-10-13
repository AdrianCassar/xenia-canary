/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_Portal2_H_
#define XENIA_HID_WINKEY_Portal2_H_

#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class Portal2Game : public HookableGame {
 public:
  enum class GameBuild {
    Unknown,
    Portal2
  };

  Portal2Game();
  ~Portal2Game() override;

  bool IsGameSupported();
  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);
  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

 private:
  GameBuild game_build_ = GameBuild::Unknown;

  struct QAngle {
    xe::be<float> pitchY;
    xe::be<float> pitchX;
    xe::be<float> yaw;
  };

  bool engine_360;

  double original_sensitivity;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_Portal2_H_
