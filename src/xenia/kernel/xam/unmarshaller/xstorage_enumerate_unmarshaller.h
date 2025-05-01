/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XSTORAGE_ENUMERATE_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XSTORAGE_ENUMERATE_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XStorageEnumerateUnmarshaller : public Unmarshaller {
 public:
  XStorageEnumerateUnmarshaller(uint32_t marshaller_buffer);

  ~XStorageEnumerateUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint32_t UserIndex() const { return user_index_; };

  const uint32_t ServerPathLength() const { return server_path_len_; };

  const std::u16string ServerPath() const { return server_path_; };

  const uint32_t StartingIndex() const { return starting_index_; };

  const uint32_t MaxResultsToReturn() const { return max_results_to_return_; };

 private:
  uint32_t user_index_;
  uint32_t server_path_len_;
  std::u16string server_path_;
  uint32_t starting_index_;
  uint32_t max_results_to_return_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XSTORAGE_ENUMERATE_UNMARSHALLER_H_