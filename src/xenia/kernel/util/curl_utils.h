/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_CURL_UTILS_H_
#define XENIA_KERNEL_UTIL_CURL_UTILS_H_

// clang-format off
// platform.h must come first to define NOMINMAX before curl pulls in windows.h.
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
// clang-format on

namespace xe {
namespace kernel {
namespace util {

// Applies the bundled Mozilla CA store to a curl handle so HTTPS peers can be
// verified. Required on Linux where libcurl is built against wolfSSL without a
// CA path, no-op on Windows where Schannel uses the system store.
CURLcode SetCurlCABundle(CURL* curl);

inline bool SetCurlOptions(CURL*) { return true; }

// Sets each option/value pair in order, stopping at the first failure.
template <typename Value, typename... Options>
bool SetCurlOptions(CURL* curl, CURLoption option, Value value,
                    Options... options) {
  return curl_easy_setopt(curl, option, value) == CURLE_OK &&
         SetCurlOptions(curl, options...);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_CURL_UTILS_H_
