/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNET_H_
#define XENIA_KERNEL_XNET_H_

#include "xenia/base/byte_order.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <winsock2.h>                    // NOLINT(build/include_order)
#endif

namespace xe {
namespace kernel {

struct XNKID {
  uint8_t ab[8];
  uint64_t as_uint64() { return *reinterpret_cast<uint64_t*>(&ab); }
};

struct XNKEY {
  uint8_t ab[16];
};

struct XNADDR {
  in_addr ina;
  in_addr inaOnline;
  xe::be<uint16_t> wPortOnline;
  uint8_t abEnet[6];
  uint8_t abOnline[20];
};

}  // namespace kernel
}  // namespace xe
#endif  // XENIA_KERNEL_XNET_H_