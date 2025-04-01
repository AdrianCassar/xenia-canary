/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "xenia/kernel/xboxkrnl/xboxkrnl_hal.h"

DECLARE_int32(avpack);

namespace xe {
namespace kernel {
namespace xboxkrnl {

void HalReturnToFirmware_entry(dword_t routine) {
  // void
  // IN FIRMWARE_REENTRY  Routine

  // Routine must be 1 'HalRebootRoutine'
  assert_true(routine == 1);

  // TODO(benvank): diediedie much more gracefully
  // Not sure how to blast back up the stack in LLVM without exceptions, though.
  XELOGE("Game requested shutdown via HalReturnToFirmware");
  exit(0);
}
DECLARE_XBOXKRNL_EXPORT2(HalReturnToFirmware, kNone, kStub, kImportant);

dword_result_t HalGetCurrentAVPack_entry() { return cvars::avpack; }
DECLARE_XBOXKRNL_EXPORT1(HalGetCurrentAVPack, kNone, kImplemented);

void HalOpenCloseODDTray_entry(dword_t open_close) {
  SetTrayState(open_close ? TrayState::Play_Game : TrayState::Open_Tray);
}
DECLARE_XBOXKRNL_EXPORT1(HalOpenCloseODDTray, kNone, kStub);

// SMC = System Management Controller
// https://github.com/landaire/LaunchCode/blob/master/LaunchCode/smc.cpp
// https://free60.org/Hardware/Console/SMC/
void HalSendSMCMessage_entry(dword_t smcMsgBuffer_ptr,
                             dword_t smcOutBuffer_ptr) {
  xe::be<uint8_t> SMC_MESSAGE = 0;

  uint8_t* msgBuffer = nullptr;
  uint8_t* outBuffer = nullptr;

  if (!smcMsgBuffer_ptr) {
    return;
  }

  msgBuffer = kernel_state()->memory()->TranslateVirtual(smcMsgBuffer_ptr);
  SMC_MESSAGE = msgBuffer[0];

  if (smcOutBuffer_ptr) {
    outBuffer = kernel_state()->memory()->TranslateVirtual(smcOutBuffer_ptr);
    outBuffer[0] = SMC_MESSAGE;
  }

  // Handle Messages
  switch (SMC_MESSAGE) {
    case SET_DVD_TRAY: {
      switch (msgBuffer[1]) {
        case Tray::Open: {
          SetTrayState(X_XAM_LOADER_DVD_TRAY_STATE::Open);
        } break;
        case Tray::Close: {
          SetTrayState(X_XAM_LOADER_DVD_TRAY_STATE::Closed);
        } break;
      }
    } break;
    case TRAY_STATE: {
      if (outBuffer) {
        outBuffer[1] = GetTrayState();
      }
    } break;
    case TEMP: {
      if (outBuffer) {
        /*
            // Animate temps in Aurora

            std::random_device rnd;
            std::mt19937_64 gen(rnd());
            std::uniform_int_distribution<int> dist(0x00, 0xFF);

            outBuffer[CPU] = dist(gen);
            outBuffer[GPU] = dist(gen);
            outBuffer[MEM] = dist(gen);
            outBuffer[BRD] = dist(gen);
        */

        outBuffer[CPU] = 50;
        outBuffer[GPU] = 50;
        outBuffer[MEM] = 50;
        outBuffer[BRD] = 50;
      }
    } break;
    case SMC_VERSION: {
      if (outBuffer) {
        outBuffer[1] = 0x41;
        outBuffer[2] = 0x02;
        outBuffer[3] = 0x03;
      }
    } break;
    case QUERY_IR_ADDRESS: {
      if (outBuffer) {
        outBuffer[1] = 0x0F;
      }
    } break;
    default:
      XELOGE("Unhandled HalSendSMCMessage {}", SMC_MESSAGE.get());
      break;
  }
}
DECLARE_XBOXKRNL_EXPORT1(HalSendSMCMessage, kNone, kSketchy);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Hal);
