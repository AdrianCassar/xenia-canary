/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
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
  XAccountGetUserInfoUnmarshaller(uint32_t marshaller_buffer);

  ~XAccountGetUserInfoUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint64_t XUID() const { return xuid_; };

  const uint64_t MachineID() const { return machine_id_; };

  const uint32_t TitleId() const { return title_id_; };

 private:
  uint64_t xuid_;
  uint64_t machine_id_;
  uint32_t title_id_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XACCOUNT_GETUSERINFO_UNMARSHALLER_H_