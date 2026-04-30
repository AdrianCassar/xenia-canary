/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_NET_UTILS_H_
#define XENIA_KERNEL_UTIL_NET_UTILS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "xenia/base/platform.h"

#ifdef XE_PLATFORM_WIN32
#include <WS2tcpip.h>
#elif XE_PLATFORM_LINUX
#include <arpa/inet.h>
#endif

namespace xe {
namespace kernel {

constexpr uint32_t BROADCAST = 0xFFFFFFFF;
constexpr uint32_t LOOPBACK = 0x7F000001;

// https://macaddress.io/statistics/company/9398
constexpr uint8_t kCoronaOUI[3] = {0x7C, 0x1E, 0x52};

struct response_data {
  char* response;
  size_t size;
  uint64_t http_code;
};

class MacAddress {
 public:
  static constexpr uint8_t MacAddressSize = 6;

  MacAddress(const uint8_t* macaddress);
  MacAddress(std::string macaddress);
  MacAddress(uint64_t macaddress);
  ~MacAddress();

  bool operator==(const MacAddress& lhs) const {
    return std::equal(std::begin(mac_address_), std::end(mac_address_),
                      std::begin(lhs.mac_address_), std::end(lhs.mac_address_));
  };

  const uint8_t* raw() const;
  std::vector<uint8_t> to_array() const;
  uint64_t to_uint64() const;
  std::string to_string() const;

  // "00:1A:2B:3C:4D:5E" <- Example printable form
  std::string to_printable_form() const;

 private:
  uint8_t mac_address_[MacAddressSize] = {};
};

std::string ip_to_string(in_addr addr);
std::string ip_to_string(sockaddr_in sockaddr);
sockaddr_in ip_to_sockaddr(std::string ip_str);
in_addr ip_to_in_addr(std::string ip_str);

void* GetOptValueWithProperEndianness(void* ptr, uint32_t optValue,
                                      uint32_t length);

uint64_t GetMachineId(const uint64_t mac_address);

uint64_t GetLocalMachineId(const MacAddress mac_address);

MacAddress GetConsoleMacAddress();

MacAddress GenerateMacAddress();

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_NET_UTILS_H_
