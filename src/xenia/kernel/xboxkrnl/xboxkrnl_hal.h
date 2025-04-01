/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_HAL_H_
#define XENIA_KERNEL_HAL_H_

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_hal.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

enum TrayState {
  Open_Tray,
  Play_Game,
  Play_Game2,
  Unrecognized_Disc,
  Play_DVD,
  Play_DVD2,
  Play_DVD3,
  Play_CD,
  Mixed_Media_Disc,
  Unrecognized_Disc2,
};

enum X_XAM_LOADER_DVD_TRAY_STATE {
  Closed = 0,
  Closing = 1,
  Open = 2,
  Opening = 3,
  Reading = 4,
};

static uint32_t TrayState_ = X_XAM_LOADER_DVD_TRAY_STATE::Closed;

namespace xe {
namespace kernel {
namespace xboxkrnl {

enum SMC_REQUEST {
  TEMP = 0x7,
  TRAY_STATE = 0xA,
  AV_PACK = 0x0F,
  SMC_VERSION = 0x12,
  QUERY_IR_ADDRESS = 0x16,
  SET_DVD_TRAY = 0x8B,
};

enum Tray { Open = 0x60, Close = 0x62 };
enum TEMP_INDEX { CPU = 0x2, GPU = 0x4, MEM = 0x6, BRD = 0x8 };

static uint32_t GetTrayState() { return TrayState_; }

static void SetTrayState(uint32_t state) {
  TrayState_ = state;
  kernel_state()->BroadcastNotification(kXNotificationDvdDriveTrayStateChanged,
                                        1);
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HAL_H_
