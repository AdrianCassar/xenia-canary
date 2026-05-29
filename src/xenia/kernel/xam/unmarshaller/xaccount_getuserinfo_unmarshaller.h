/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XACCOUNT_GETUSERINFO_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XACCOUNT_GETUSERINFO_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XAccountGetUserInfoUnmarshaller : public Unmarshaller {
 public:
  XAccountGetUserInfoUnmarshaller(KernelState* kernel_state,
                                  uint32_t marshaller_address);

  ~XAccountGetUserInfoUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint64_t XUID() const { return xuid_; };

  const uint64_t MachineID() const { return machine_id_; };

  const uint32_t TitleId() const { return title_id_; };

 private:
  uint64_t xuid_ = 0;
  uint64_t machine_id_ = 0;
  uint32_t title_id_ = 0;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XACCOUNT_GETUSERINFO_UNMARSHALLER_H_
