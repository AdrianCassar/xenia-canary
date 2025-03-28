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

#include <random>

#include "xenia/base/byte_order.h"

#ifdef XE_PLATFORM_WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
// clang-format off
#include "xenia/base/platform_win.h"
// clang-format on
#include <inaddr.h>
#include <winapifamily.h>
#endif

namespace xe {

// clang-format off

// https://github.com/davispuh/XLiveServices/blob/master/lib/xlive_services/hresult.rb

#define X_ONLINE_E_LOGON_NOT_LOGGED_ON                      static_cast<X_HRESULT>(0x80151802L) // ERROR_SERVICE_NOT_FOUND
#define X_ONLINE_E_LOGON_SERVICE_TEMPORARILY_UNAVAILABLE    static_cast<X_HRESULT>(0x80151102L) // ERROR_CONNECTION_INVALID
#define X_ONLINE_E_LOGON_SERVICE_NOT_REQUESTED              static_cast<X_HRESULT>(0x80151100L) // ERROR_SERVICE_SPECIFIC_ERROR
#define X_ONLINE_E_LOGON_LOGON_SERVICE_NOT_AUTHORIZED       static_cast<X_HRESULT>(0x80151101L) // ERROR_NOT_AUTHENTICATED
#define X_ONLINE_E_LOGON_NO_NETWORK_CONNECTION              static_cast<X_HRESULT>(0x80151000L)
#define X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED             static_cast<X_HRESULT>(0x001510F0L)
#define X_ONLINE_S_LOGON_DISCONNECTED                       static_cast<X_HRESULT>(0x001510F1L)
#define X_ONLINE_E_SESSION_WRONG_STATE                      static_cast<X_HRESULT>(0x80155206L)
#define X_ONLINE_E_SESSION_INSUFFICIENT_BUFFER              static_cast<X_HRESULT>(0x80155207L)
#define X_ONLINE_E_SESSION_JOIN_ILLEGAL                     static_cast<X_HRESULT>(0x8015520AL)
#define X_ONLINE_E_SESSION_NOT_FOUND                        static_cast<X_HRESULT>(0x80155200L)
#define X_ONLINE_E_SESSION_FULL                             static_cast<X_HRESULT>(0x80155202L)
#define X_ONLINE_E_STORAGE_INVALID_FACILITY                 static_cast<X_HRESULT>(0x8015C009L)
#define X_ONLINE_E_STORAGE_FILE_NOT_FOUND                   static_cast<X_HRESULT>(0x8015C004L)

#define X_PARTY_E_NOT_IN_PARTY                              static_cast<X_HRESULT>(0x807D0003L)

#define X_ONLINE_FRIENDSTATE_FLAG_NONE              0x00000000
#define X_ONLINE_FRIENDSTATE_FLAG_ONLINE            0x00000001
#define X_ONLINE_FRIENDSTATE_FLAG_PLAYING           0x00000002
#define X_ONLINE_FRIENDSTATE_FLAG_JOINABLE          0x00000010

#define X_ONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED    0x10000000
#define X_ONLINE_FRIENDSTATE_FLAG_SENTINVITE        0x04000000

#define X_ONLINE_FRIENDSTATE_ENUM_ONLINE            0x00000000
#define X_ONLINE_FRIENDSTATE_ENUM_AWAY              0x00010000
#define X_ONLINE_FRIENDSTATE_ENUM_BUSY              0x00020000
#define X_ONLINE_FRIENDSTATE_MASK_USER_STATE        0x000F0000

#define X_ONLINE_MAX_FRIENDS                        100
#define X_ONLINE_PEER_SUBSCRIPTIONS                 400
#define X_MAX_RICHPRESENCE_SIZE                     64
#define X_ONLINE_MAX_PATHNAME_LENGTH                255

#define X_CONTEXT_PRESENCE                          0x00008001
#define X_CONTEXT_GAME_TYPE                         0x0000800A
#define X_CONTEXT_GAME_MODE                         0x0000800B

#define X_CONTEXT_GAME_TYPE_RANKED                  0x0
#define X_CONTEXT_GAME_TYPE_STANDARD                0x1

#define X_PARTY_MAX_USERS                           32

enum XNADDR_STATUS : uint32_t {
  XNADDR_PENDING = 0x00000000,              // Address acquisition is not yet complete
  XNADDR_NONE = 0x00000001,                 // XNet is uninitialized or no debugger found
  XNADDR_ETHERNET = 0x00000002,             // Host has ethernet address (no IP address)
  XNADDR_STATIC = 0x00000004,               // Host has statically assigned IP address
  XNADDR_DHCP = 0x00000008,                 // Host has DHCP assigned IP address
  XNADDR_PPPOE = 0x00000010,                // Host has PPPoE assigned IP address
  XNADDR_GATEWAY = 0x00000020,              // Host has one or more gateways configured
  XNADDR_DNS = 0x00000040,                  // Host has one or more DNS servers configured
  XNADDR_ONLINE = 0x00000080,               // Host is currently connected to online service
  XNADDR_TROUBLESHOOT = 0x00008000          // Network configuration requires troubleshooting
};

enum ETHERNET_STATUS : uint32_t {
  ETHERNET_LINK_NONE = 0x00000000,          // Ethernet cable is not connected
  ETHERNET_LINK_ACTIVE = 0x00000001,        // Ethernet cable is connected and active
  ETHERNET_LINK_100MBPS = 0x00000002,       // Ethernet link is set to 100 Mbps
  ETHERNET_LINK_10MBPS = 0x00000004,        // Ethernet link is set to 10 Mbps
  ETHERNET_LINK_FULL_DUPLEX = 0x00000008,   // Ethernet link is in full duplex mode
  ETHERNET_LINK_HALF_DUPLEX = 0x00000010,   // Ethernet link is in half duplex mode
  ETHERNET_LINK_WIRELESS = 0x00000020       // Ethernet link is wireless (802.11 based)
};

// clang-format on

namespace kernel {

constexpr uint16_t XNET_SYSTEMLINK_PORT = 3074;

constexpr uint32_t XEX_PRIVILEGE_CROSSPLATFORM_SYSTEM_LINK = 14;

constexpr uint8_t kXUserMaxStatsRows = 100;

constexpr uint8_t kXUserMaxStatsAttributes = 64;

enum NETWORK_MODE : uint32_t { OFFLINE, LAN, XBOXLIVE };

enum X_USER_AGE_GROUP : uint32_t { CHILD, TEEN, ADULT };

enum X_STATS_ENUMERATOR_TYPE : uint32_t {
  XUID,
  RANK,
  RANK_PER_SPEC,
  BY_RATING
};

enum PLATFORM_TYPE : uint32_t { Xbox1, Xbox360, PC };

enum X_STORAGE_BUILD_SERVER_PATH_RESULT : int32_t {
  Invalid = -1,
  Created = 0,
  Found = 1,
};

struct XNKID {
  uint8_t ab[8];
  uint64_t as_uint64() { return *reinterpret_cast<uint64_t*>(&ab); }
  uint64_t as_uintBE64() { return xe::byte_swap(as_uint64()); }
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

struct XSESSION_INFO {
  XNKID sessionID;
  XNADDR hostAddress;
  XNKEY keyExchangeKey;
};

struct X_PARTY_CUSTOM_DATA {
  xe::be<uint64_t> First;
  xe::be<uint64_t> Second;
};

struct X_PARTY_USER_INFO {
  xe::be<uint64_t> Xuid;
  char GamerTag[16];
  xe::be<uint32_t> UserIndex;
  xe::be<uint32_t> NatType;
  xe::be<uint32_t> TitleId;
  xe::be<uint32_t> Flags;
  XSESSION_INFO SessionInfo;
  X_PARTY_CUSTOM_DATA CustomData;
};

struct X_PARTY_USER_LIST {
  xe::be<uint32_t> UserCount;
  X_PARTY_USER_INFO Users[X_PARTY_MAX_USERS];
};

struct X_USER_STATS_READ_RESULTS {
  xe::be<uint32_t> NumViews;
  xe::be<uint32_t> Views_ptr;
};

struct X_USER_STATS_SPEC {
  xe::be<uint32_t> view_id;
  xe::be<uint32_t> num_column_ids;
  xe::be<uint16_t> column_Ids[kXUserMaxStatsAttributes];
};
static_assert_size(X_USER_STATS_SPEC, 8 + kXUserMaxStatsAttributes * 2);

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

enum X_STORAGE_FACILITY : uint32_t {
  FACILITY_INVALID = 0,
  FACILITY_GAME_CLIP = 1,      // Read, Write
  FACILITY_PER_TITLE = 2,      // Read, Enumerate
  FACILITY_PER_USER_TITLE = 3  // Read, Write, Delete
};

struct X_STORAGE_BUILD_SERVER_PATH {
  xe::be<uint32_t> user_index;
  uint8_t unkn[4];
  xe::be<uint64_t> xuid;
  xe::be<X_STORAGE_FACILITY> storage_location;
  xe::be<uint32_t> storage_location_info_ptr;
  xe::be<uint32_t> storage_location_info_size;
  xe::be<uint32_t> file_name_ptr;
  xe::be<uint32_t> server_path_ptr;
  xe::be<uint32_t> server_path_length_ptr;
};
static_assert_size(X_STORAGE_BUILD_SERVER_PATH, 0x28);

struct X_MUTE_SET_STATE {
  xe::be<uint32_t> user_index;
  xe::be<uint64_t> remote_xuid;
  xe::be<uint32_t> set_muted;
};

struct X_PRESENCE_SUBSCRIBE {
  X_ARGUEMENT_ENTRY user_index;
  X_ARGUEMENT_ENTRY peers;
  X_ARGUEMENT_ENTRY peer_xuids_ptr;
};

struct X_PRESENCE_UNSUBSCRIBE {
  X_ARGUEMENT_ENTRY user_index;
  X_ARGUEMENT_ENTRY peers;
  X_ARGUEMENT_ENTRY peer_xuids_ptr;
};

struct X_PRESENCE_CREATE {
  X_ARGUEMENT_ENTRY user_index;
  X_ARGUEMENT_ENTRY num_peers;
  X_ARGUEMENT_ENTRY peer_xuids_ptr;
  X_ARGUEMENT_ENTRY starting_index;
  X_ARGUEMENT_ENTRY max_peers;
  X_ARGUEMENT_ENTRY buffer_length_ptr;      // output
  X_ARGUEMENT_ENTRY enumerator_handle_ptr;  // output
};

struct X_INVITE_GET_ACCEPTED_INFO {
  X_ARGUEMENT_ENTRY user_index;
  X_ARGUEMENT_ENTRY invite_info;
};

// struct FILETIME {
//   xe::be<uint32_t> dwHighDateTime;
//   xe::be<uint32_t> dwLowDateTime;
// };

#pragma pack(push, 4)

struct X_ONLINE_PRESENCE {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> state;
  XNKID session_id;
  xe::be<uint32_t> title_id;
  xe::be<uint64_t> state_change_time;  // filetime
  xe::be<uint32_t> cchRichPresence;
  xe::be<char16_t> wszRichPresence[64];
};
static_assert_size(X_ONLINE_PRESENCE, 0xA4);

struct X_ONLINE_FRIEND {
  xe::be<uint64_t> xuid;
  char Gamertag[16];
  xe::be<uint32_t> state;
  XNKID session_id;
  xe::be<uint32_t> title_id;
  xe::be<uint64_t> ftUserTime;
  XNKID xnkidInvite;
  xe::be<uint64_t> gameinviteTime;
  xe::be<uint32_t> cchRichPresence;
  xe::be<char16_t> wszRichPresence[X_MAX_RICHPRESENCE_SIZE];
};
static_assert_size(X_ONLINE_FRIEND, 0xC4);

struct X_INVITE_INFO {
  xe::be<uint64_t> xuid_invitee;
  xe::be<uint64_t> xuid_inviter;
  xe::be<uint32_t> title_id;
  XSESSION_INFO host_info;
  xe::be<uint32_t> from_game_invite;
};

#pragma pack(pop)

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

constexpr uint8_t XNKID_ONLINE = 0xAE;
constexpr uint8_t XNKID_SYSTEM_LINK = 0x00;

inline bool IsOnlinePeer(uint64_t session_id) {
  return ((session_id >> 56) & 0xFF) == XNKID_ONLINE;
}

inline bool IsSystemlink(uint64_t session_id) {
  return ((session_id >> 56) & 0xFF) == XNKID_SYSTEM_LINK;
}

inline bool IsValidXNKID(uint64_t session_id) {
  if (!IsOnlinePeer(session_id) && !IsSystemlink(session_id) ||
      session_id == 0) {
    assert_always();

    return false;
  }

  return true;
}

inline uint64_t GenerateSessionId(uint8_t mask) {
  std::random_device rnd;
  std::uniform_int_distribution<uint64_t> dist(0, -1);

  return (static_cast<uint64_t>(mask) << 56) | (dist(rnd) & 0x0000FFFFFFFFFFFF);
}

inline void Uint64toXNKID(uint64_t sessionID, XNKID* xnkid) {
  uint64_t session_id = xe::byte_swap(sessionID);
  memcpy(xnkid->ab, &session_id, sizeof(XNKID));
}

inline uint64_t XNKIDtoUint64(XNKID* sessionID) {
  uint64_t session_id = 0;
  memcpy(&session_id, sessionID->ab, sizeof(XNKID));
  return xe::byte_swap(session_id);
}

inline void GenerateIdentityExchangeKey(XNKEY* key) {
  for (uint8_t i = 0; i < sizeof(XNKEY); i++) {
    key->ab[i] = i;
  }
}

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_H_