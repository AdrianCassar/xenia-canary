/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
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

// clang-format off

#define X_ERROR_LOGON_NO_NETWORK_CONNECTION             X_RESULT_FROM_WIN32(0x00151000L)
#define X_ERROR_SERVICE_NOT_FOUND                       X_RESULT_FROM_WIN32(0x00151100L)
#define X_ERROR_CONNECTION_INVALID                      X_RESULT_FROM_WIN32(0x00151802L)
#define X_ERROR_SESSION_INSUFFICIENT_BUFFER             X_RESULT_FROM_WIN32(0x00155207L)

#define X_ONLINE_E_LOGON_NO_NETWORK_CONNECTION          X_HRESULT_FROM_WIN32(X_ERROR_LOGON_NO_NETWORK_CONNECTION)
#define X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED         static_cast<X_HRESULT>(0x001510F0L)
#define X_ONLINE_S_LOGON_DISCONNECTED                   static_cast<X_HRESULT>(0x001510F1L)
#define X_ONLINE_E_SESSION_INSUFFICIENT_BUFFER          X_HRESULT_FROM_WIN32(X_ERROR_SESSION_INSUFFICIENT_BUFFER)

#define X_CONTEXT_PRESENCE 0x00008001
#define X_CONTEXT_GAME_TYPE 0x0000800A
#define X_CONTEXT_GAME_MODE 0x0000800B

#define X_CONTEXT_GAME_TYPE_RANKED 0x0
#define X_CONTEXT_GAME_TYPE_STANDARD 0x1

// clang-format on

namespace kernel {

struct XNKID {
  uint8_t ab[8];
  uint64_t as_uint64() { return *reinterpret_cast<uint64_t*>(&ab); }
};

struct XNKEY {
  uint8_t ab[16];
};

struct XNADDR {
  // FYI: IN_ADDR should be in network-byte order.
  in_addr ina;        // IP address (zero if not static/DHCP) - Local IP
  in_addr inaOnline;  // Online IP address (zero if not online) - Public IP
  xe::be<uint16_t> wPortOnline;  // Online port
  uint8_t abEnet[6];             // Ethernet MAC address
  uint8_t abOnline[20];          // Online identification
};

struct X_ONLINE_SERVICE_INFO {
  xe::be<uint32_t> id;
  in_addr ip;
  xe::be<uint16_t> port;
  xe::be<uint16_t> reserved;
};
static_assert_size(X_ONLINE_SERVICE_INFO, 0xC);

struct X_TITLE_SERVER {
  in_addr server_address;
  uint32_t flags;
  char server_description[200];
};
static_assert_size(X_TITLE_SERVER, 0xD0);

#pragma region XLiveBase

// TODO(Gliniak): Find better names for these structures!
struct X_ARGUEMENT_ENTRY {
  xe::be<uint32_t> magic_number;
  xe::be<uint32_t> unk_1;
  xe::be<uint32_t> unk_2;
  xe::be<uint32_t> object_ptr;
};
static_assert_size(X_ARGUEMENT_ENTRY, 0x10);

struct X_ARGUMENT_LIST {
  X_ARGUEMENT_ENTRY entry[32];
  xe::be<uint32_t> argument_count;
};
static_assert_size(X_ARGUMENT_LIST, 0x204);

struct X_STORAGE_BUILD_SERVER_PATH {
  xe::be<uint32_t> user_index;
  char unk[12];
  xe::be<uint32_t> storage_location;  // 2 means title specific storage,
                                      // something like developers storage.
  xe::be<uint32_t> storage_location_info_ptr;
  xe::be<uint32_t> storage_location_info_size;
  xe::be<uint32_t> file_name_ptr;
  xe::be<uint32_t> server_path_ptr;
  xe::be<uint32_t> server_path_length_ptr;
};
static_assert_size(X_STORAGE_BUILD_SERVER_PATH, 0x28);

struct X_MUTE_LIST_SET_STATE {
  xe::be<uint32_t> user_index;
  xe::be<uint64_t> remote_xuid;
  bool set_muted;
};

struct X_DATA_58024 {
  X_ARGUEMENT_ENTRY xuid;
  X_ARGUEMENT_ENTRY ukn2;  // 125
  X_ARGUEMENT_ENTRY ukn3;  // 0
};
static_assert_size(X_DATA_58024, 0x30);

struct X_DATA_5801C {
  X_ARGUEMENT_ENTRY xuid;
  X_ARGUEMENT_ENTRY ukn2;
  X_ARGUEMENT_ENTRY ukn3;
};
static_assert_size(X_DATA_5801C, 0x30);

#pragma endregion

}  // namespace kernel
}  // namespace xe
#endif  // XENIA_KERNEL_XNET_H_