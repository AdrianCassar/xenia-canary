/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_GENERIC_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_GENERIC_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class GenericUnmarshaller : public Unmarshaller {
 public:
  GenericUnmarshaller(uint32_t marshaller_buffer);

  ~GenericUnmarshaller() {};

  virtual X_HRESULT Deserialize();

 private:
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_GENERIC_UNMARSHALLER_H_