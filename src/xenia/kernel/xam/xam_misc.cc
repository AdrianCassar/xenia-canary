/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

static uint32_t dash_context_ = 0;  // don't know what this does right now

void XamSetDashContext_entry(dword_t context) {
  dash_context_ = context.value();
}
DECLARE_XAM_EXPORT1(XamSetDashContext, kNone, kStub);

dword_result_t XamGetDashContext_entry() { return dash_context_; }
DECLARE_XAM_EXPORT1(XamGetDashContext, kNone, kStub);

dword_result_t XamFitnessClearBodyProfileRecords_entry(
    unknown_t r3, unknown_t r4, unknown_t r5, unknown_t r6, unknown_t r7,
    unknown_t r8, unknown_t r9) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamFitnessClearBodyProfileRecords, kNone, kStub);
dword_result_t XamSetLastActiveUserData_entry(unknown_t r3, unknown_t r4,
                                              unknown_t r5, unknown_t r6,
                                              unknown_t r7, unknown_t r8,
                                              unknown_t r9) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSetLastActiveUserData, kNone, kStub);
dword_result_t XamGetLastActiveUserData_entry(unknown_t r3, unknown_t r4,
                                              unknown_t r5, unknown_t r6,
                                              unknown_t r7, unknown_t r8,
                                              unknown_t r9) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLastActiveUserData, kNone, kStub);
dword_result_t XamPngDecode_entry(unknown_t r3, unknown_t r4, unknown_t r5,
                                  unknown_t r6, unknown_t r7, unknown_t r8,
                                  unknown_t r9) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPngDecode, kNone, kStub);
dword_result_t XamPackageManagerGetExperienceMode_entry(
    unknown_t r3, unknown_t r4, unknown_t r5, unknown_t r6, unknown_t r7,
    unknown_t r8, unknown_t r9) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPackageManagerGetExperienceMode, kNone, kStub);
dword_result_t XamGetLiveHiveValueW_entry(lpstring_t name, lpstring_t value,
                                          dword_t ch_value, dword_t unk,
                                          lpvoid_t overlapped_ptr) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLiveHiveValueW, kNone, kStub);
}  // namespace xam
}  // namespace kernel
}  // namespace xe