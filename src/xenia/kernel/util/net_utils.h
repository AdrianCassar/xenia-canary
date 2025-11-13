/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_NET_UTILS_H_
#define XENIA_KERNEL_UTIL_NET_UTILS_H_

#include "xenia/kernel/kernel_state.h"

#ifdef XE_PLATFORM_WIN32
// clang-format off
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <winsock2.h>                    // NOLINT(build/include_order)

#include <Iphlpapi.h>
// clang-format on
#endif

namespace xe {
namespace kernel {

const uint32_t BROADCAST = 0xFFFFFFFF;
const uint32_t LOOPBACK = 0x7F000001;

struct response_data {
  char* response;
  size_t size;
  uint64_t http_code;
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

sockaddr_in WinsockGetLocalIP();

std::string ip_to_string(in_addr addr);
std::string ip_to_string(sockaddr_in sockaddr);
sockaddr_in ip_to_sockaddr(std::string ip_str);
in_addr ip_to_in_addr(std::string ip_str);

void* GetOptValueWithProperEndianness(void* ptr, uint32_t optValue,
                                      uint32_t length);

uint64_t GetMachineId(const uint64_t mac_address);

uint64_t GetLocalMachineId(const MacAddress mac_address);

std::unique_ptr<MacAddress> GenerateMacAddress();

}  // namespace kernel
}  // namespace xe

#endif
