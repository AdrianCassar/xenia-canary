/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/curl_utils.h"

#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX
// Generated from third_party/cacert/cacert.pem (third_party/CMakeLists.txt).
extern "C" const unsigned char xe_cacert_pem[];
extern "C" const size_t xe_cacert_pem_size;
#endif

namespace xe {
namespace kernel {
namespace util {

CURLcode SetCurlCABundle(CURL* curl) {
#if XE_PLATFORM_LINUX
  curl_blob blob = {};
  blob.data = const_cast<unsigned char*>(xe_cacert_pem);
  blob.len = xe_cacert_pem_size;
  blob.flags = CURL_BLOB_NOCOPY;

  return curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
#else
  return CURLE_OK;
#endif
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
