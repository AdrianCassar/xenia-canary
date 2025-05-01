/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xstorage_download_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XStorageDownloadToMemoryUnmarshaller::XStorageDownloadToMemoryUnmarshaller(
    uint32_t marshaller_address)
    : Unmarshaller(marshaller_address),
      user_index_(0),
      server_path_len_(0),
      server_path_(u""),
      buffer_size_(0),
      download_buffer_address_(0) {}

X_HRESULT XStorageDownloadToMemoryUnmarshaller::Deserialize() {
  if (!GetXLiveBaseAsyncMessage()->xlive_async_task_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->results_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->results_size) {
    return X_E_INVALIDARG;
  }

  user_index_ = ReadSwap<uint32_t>();
  server_path_len_ = ReadSwap<uint32_t>();
  server_path_ = ReadSwapUTF16String(server_path_len_);
  buffer_size_ = ReadSwap<uint32_t>();
  download_buffer_address_ = ReadSwap<uint32_t>();

  if (GetPosition() !=
      GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  if (ServerPathLength() > X_ONLINE_MAX_PATHNAME_LENGTH) {
    return X_E_INVALIDARG;
  }

  if (ServerPath().empty()) {
    return X_ONLINE_E_STORAGE_INVALID_STORAGE_PATH;
  }

  if (!DownloadBufferAddress()) {
    return X_E_INVALIDARG;
  }

  if (BufferSize() > kTMSFileMaxSize) {
    return X_ONLINE_E_STORAGE_FILE_IS_TOO_BIG;
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
