/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XSTORAGE_UPLOAD_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XSTORAGE_UPLOAD_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XStorageUploadToMemoryUnmarshaller : public Unmarshaller {
 public:
  XStorageUploadToMemoryUnmarshaller(uint32_t marshaller_buffer);

  ~XStorageUploadToMemoryUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint32_t UserIndex() const { return user_index_; };

  const uint32_t ServerPathLength() const { return server_path_len_; };

  const std::u16string ServerPath() const { return server_path_; };

  const uint32_t BufferSize() const { return buffer_size_; };

  const uint32_t UploadBufferAddress() const { return upload_buffer_address_; };

  std::span<uint8_t> GetUploadBuffer() const {
    uint8_t* upload_buffer_ptr =
        kernel_state()->memory()->TranslateVirtual<uint8_t*>(
            upload_buffer_address_);

    return std::span<uint8_t>(upload_buffer_ptr, buffer_size_);
  };

 private:
  uint32_t user_index_;
  uint32_t server_path_len_;
  std::u16string server_path_;
  uint32_t buffer_size_;
  uint32_t upload_buffer_address_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XSTORAGE_UPLOAD_UNMARSHALLER_H_