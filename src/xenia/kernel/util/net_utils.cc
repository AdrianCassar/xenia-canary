/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {

MacAddress::MacAddress(const uint8_t* macaddress) {
  for (uint8_t i = 0; i < MacAddressSize; i++) {
    mac_address_[i] = macaddress[i];
  }
}

MacAddress::MacAddress(std::string macaddress) {
  const uint64_t mac =
      xe::byte_swap(string_util::from_string<uint64_t>(macaddress, true)) >>
      0x10;
  memcpy(mac_address_, &mac, MacAddressSize);
}

MacAddress::MacAddress(uint64_t macaddress) {
  xe::be<uint64_t> be_macaddress = (macaddress << 0x10);
  memcpy(mac_address_, &be_macaddress, MacAddressSize);
}
MacAddress::~MacAddress() {}

const uint8_t* MacAddress::raw() const { return mac_address_; }

std::vector<uint8_t> MacAddress::to_array() const {
  std::vector<uint8_t> result = {};
  result.resize(MacAddressSize);
  memcpy(result.data(), mac_address_, MacAddressSize);
  return result;
}

uint64_t MacAddress::to_uint64() const {
  return string_util::from_string<uint64_t>(to_string(), true);
}

std::string MacAddress::to_string() const {
  std::string result = fmt::format(
      "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", mac_address_[0], mac_address_[1],
      mac_address_[2], mac_address_[3], mac_address_[4], mac_address_[5]);

  return result;
}

std::string MacAddress::to_printable_form() const {
  std::string mac =
      fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", mac_address_[0],
                  mac_address_[1], mac_address_[2], mac_address_[3],
                  mac_address_[4], mac_address_[5]);
  return mac;
}

sockaddr_in WinsockGetLocalIP() {
  sockaddr_in localAddr{};

#ifdef XE_PLATFORM_WIN32
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (sock == INVALID_SOCKET) {
    return localAddr;
  }

  // Connect the socket to a remote address
  sockaddr_in remoteAddr{};
  remoteAddr.sin_family = AF_INET;
  remoteAddr.sin_port = htons(80);

  // Google DNS
  inet_pton(AF_INET, "8.8.8.8", &remoteAddr.sin_addr);

  sockaddr* remoteAddr_ptr = reinterpret_cast<sockaddr*>(&remoteAddr);

  if (connect(sock, remoteAddr_ptr, sizeof(remoteAddr)) == SOCKET_ERROR) {
    closesocket(sock);
    return localAddr;
  }

  sockaddr* localAddr_ptr = reinterpret_cast<sockaddr*>(&localAddr);
  int addrSize = sizeof(localAddr);

  if (getsockname(sock, localAddr_ptr, &addrSize) == SOCKET_ERROR) {
    closesocket(sock);
    return localAddr;
  }

  closesocket(sock);

  return localAddr;
#else
  return localAddr;
#endif  // XE_PLATFORM_WIN32
}

const std::string ip_to_string(in_addr addr) {
  char ip_str[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET, &addr.s_addr, ip_str, INET_ADDRSTRLEN);

  return ip_str;
}

const std::string ip_to_string(sockaddr_in sockaddr) {
  char ip_str[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET, &sockaddr.sin_addr, ip_str, INET_ADDRSTRLEN);

  return ip_str;
}

const sockaddr_in ip_to_sockaddr(std::string ip_str) {
  sockaddr_in addr{};
  inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr);

  return addr;
}

const in_addr ip_to_in_addr(std::string ip_str) {
  in_addr addr{};
  inet_pton(AF_INET, ip_str.c_str(), &addr.s_addr);

  return addr;
}

}  // namespace kernel
}  // namespace xe