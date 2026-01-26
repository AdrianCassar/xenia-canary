/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/util/net_utils.h"

DECLARE_string(network_guid);

DECLARE_bool(logging);

// TODO(Adrian): This should be moved to XConfig once supported.
DEFINE_string(mac_address, "", "Console MAC address. (Do not touch!)", "Live");

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

std::string ip_to_string(in_addr addr) {
  char ip_str[INET_ADDRSTRLEN]{};
  const char* result =
      inet_ntop(AF_INET, &addr.s_addr, ip_str, INET_ADDRSTRLEN);

  return ip_str;
}

std::string ip_to_string(sockaddr_in sockaddr) {
  char ip_str[INET_ADDRSTRLEN]{};
  const char* result =
      inet_ntop(AF_INET, &sockaddr.sin_addr, ip_str, INET_ADDRSTRLEN);

  return ip_str;
}

sockaddr_in ip_to_sockaddr(std::string ip_str) {
  sockaddr_in addr{};
  int32_t result = inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr);

  return addr;
}

in_addr ip_to_in_addr(std::string ip_str) {
  in_addr addr{};
  int32_t result = inet_pton(AF_INET, ip_str.c_str(), &addr.s_addr);

  return addr;
}

void* GetOptValueWithProperEndianness(void* ptr, uint32_t optValue,
                                      uint32_t length) {
  if (length == 1) {
    // No need to do anything as it is 1 byte long anyway
    return ptr;
  }

  // Check if endianness matches for byte type options that use 4 bytes
  // 494707E4 Uses SO_BROADCAST but writes only 1 byte instead of 4 which causes
  // it to be in correct endianness from start.

  // Check for all possible BOOLEAN type options
  if (optValue == 0x0004 || optValue == (int)(~0x0004) || optValue == 0x0020 ||
      optValue == (int)(~0x0080)) {
    // Check if we have correct endianness out of the box
    uint32_t* value = reinterpret_cast<uint32_t*>(ptr);
    if (*value == 1) {
      return ptr;
    }
  }

  void* optval_ptr_le = calloc(1, length);

  switch (length) {
    case 4:
      xe::copy_and_swap((uint32_t*)optval_ptr_le, (uint32_t*)ptr, 1);
      break;
    case 8:
      xe::copy_and_swap((uint64_t*)optval_ptr_le, (uint64_t*)ptr, 1);
      break;
    default:
      XELOGE(
          "GetOptValueWithProperEndianness - Unhandled length: {} for option: "
          "{:08X}",
          length, optValue);
      break;
  }

  return optval_ptr_le;
}

uint64_t GetMachineId(const uint64_t mac_address) {
  const uint64_t machine_id_mask = 0xFA00000000000000;

  return machine_id_mask | mac_address;
}

uint64_t GetLocalMachineId(const MacAddress mac_address) {
  return GetMachineId(mac_address.to_uint64());
}

void CreateConsoleMacAddress() {
  if (cvars::mac_address.empty()) {
    const MacAddress mac_address = GenerateMacAddress();
    OVERRIDE_string(mac_address, mac_address.to_string());
  }
}

MacAddress GetConsoleMacAddress() { return MacAddress(cvars::mac_address); }

MacAddress GenerateMacAddress() {
  uint8_t mac_address[MacAddress::MacAddressSize] = {};

  // MAC OUI part for MS devices.
  mac_address[0] = kCoronaOUI[0];
  mac_address[1] = kCoronaOUI[1];
  mac_address[2] = kCoronaOUI[2];

  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<uint16_t> dist(
      0, std::numeric_limits<uint16_t>::max());

  for (uint8_t i = 3; i < MacAddress::MacAddressSize; i++) {
    mac_address[i] = static_cast<uint8_t>(dist(rnd));
  }

  return MacAddress(mac_address);
}

}  // namespace kernel
}  // namespace xe
