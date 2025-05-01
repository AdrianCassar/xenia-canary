/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/generic_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

GenericUnmarshaller::GenericUnmarshaller(uint32_t marshaller_address)
    : Unmarshaller(marshaller_address) {}

X_HRESULT GenericUnmarshaller::Deserialize() {
  if (!GetXLiveBaseAsyncMessage()->xlive_async_task_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_ptr) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
