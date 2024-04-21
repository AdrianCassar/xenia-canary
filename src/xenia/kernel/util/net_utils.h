/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_NET_UTILS_H_
#define XENIA_KERNEL_UTIL_NET_UTILS_H_

#include "xenia/kernel/kernel_state.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <winsock2.h>                    // NOLINT(build/include_order)
#endif

namespace xe {
namespace kernel {

const uint32_t LOOPBACK = 0x7F000001;

struct response_data {
  char* response;
  size_t size;
  uint64_t http_code;
};

enum HTTP_STATUS_CODE {
  HTTP_OK = 200,
  HTTP_CREATED = 201,
  HTTP_NO_CONTENT = 204,

  HTTP_BAD_REQUEST = 400,
  HTTP_UNAUTHORIZED = 401,
  HTTP_NOT_FOUND = 404,

  HTTP_INTERNAL_SERVER_ERROR = 500,
  HTTP_NOT_IMPLEMENTED = 501,
  HTTP_BAD_GATEWAY = 502
};

class MacAddress {
 public:
  static const uint8_t MacAddressSize = 6;

  MacAddress(const uint8_t* macaddress);
  MacAddress(std::string macaddress);
  MacAddress(uint64_t macaddress);
  ~MacAddress();

  const uint8_t* raw() const;
  std::vector<uint8_t> to_array() const;
  uint64_t to_uint64() const;
  std::string to_string() const;

  // "00:1A:2B:3C:4D:5E" <- Example printable form
  std::string to_printable_form() const;

 private:
  uint8_t mac_address_[MacAddressSize];
};

const std::string ip_to_string(in_addr addr);
const std::string ip_to_string(sockaddr_in sockaddr);
const sockaddr_in ip_to_sockaddr(std::string ip_str);
const in_addr ip_to_in_addr(std::string ip_str);

}  // namespace kernel
}  // namespace xe

#endif
