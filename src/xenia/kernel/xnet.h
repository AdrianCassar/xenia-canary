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
#include "xenia/kernel/util/xfiletime.h"
#include "xenia/kernel/xam/user_data.h"

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
#define X_ONLINE_E_SESSION_REQUIRES_ARBITRATION             static_cast<X_HRESULT>(0x80155205L)
#define X_ONLINE_E_SESSION_NOT_LOGGED_ON                    static_cast<X_HRESULT>(0x80155209L)
#define X_ONLINE_E_SESSION_FULL                             static_cast<X_HRESULT>(0x80155202L)
#define X_ONLINE_STRING_TOO_LONG                            static_cast<X_HRESULT>(0x80157101L)
#define X_ONLINE_STRING_OFFENSIVE_TEXT                      static_cast<X_HRESULT>(0x80157102L)
#define X_ONLINE_STRING_NO_DEFAULT_STRING                   static_cast<X_HRESULT>(0x80157103L)
#define X_ONLINE_STRING_INVALID_LANGUAGE                    static_cast<X_HRESULT>(0x80157104L)
#define X_ONLINE_E_STORAGE_INVALID_FACILITY                 static_cast<X_HRESULT>(0x8015C009L)
#define X_ONLINE_E_STORAGE_FILE_NOT_FOUND                   static_cast<X_HRESULT>(0x8015C004L)
#define X_ONLINE_E_STORAGE_INVALID_STORAGE_PATH             static_cast<X_HRESULT>(0x8015C008L)
#define X_ONLINE_S_STORAGE_FILE_NOT_MODIFIED                static_cast<X_HRESULT>(0x0015C013L)
#define X_ONLINE_E_STORAGE_FILE_IS_TOO_BIG                  static_cast<X_HRESULT>(0x8015C003L)
#define X_ONLINE_E_ACCESS_DENIED                            static_cast<X_HRESULT>(0x80150016L)
#define X_ONLINE_E_ACCOUNTS_USER_OPTED_OUT                  static_cast<X_HRESULT>(0x80154099L)
#define X_ONLINE_E_ACCOUNTS_USER_GET_ACCOUNT_INFO_ERROR     static_cast<X_HRESULT>(0x80154098L)

#define X_PARTY_E_NOT_IN_PARTY                              static_cast<X_HRESULT>(0x807D0003L)

#define XHTTP_ERROR_BASE                                    12000

#define XHTTP_ERROR_TIMEOUT                                 (XHTTP_ERROR_BASE + 2)
#define XHTTP_ERROR_INTERNAL_ERROR                          (XHTTP_ERROR_BASE + 4)
#define XHTTP_ERROR_UNRECOGNIZED_SCHEME                     (XHTTP_ERROR_BASE + 6)
#define XHTTP_ERROR_NAME_NOT_RESOLVED                       (XHTTP_ERROR_BASE + 7)
#define XHTTP_ERROR_INVALID_OPTION                          (XHTTP_ERROR_BASE + 9)
#define XHTTP_ERROR_OPTION_NOT_SETTABLE                     (XHTTP_ERROR_BASE + 11)
#define XHTTP_ERROR_INCORRECT_HANDLE_TYPE                   (XHTTP_ERROR_BASE + 18)
#define XHTTP_ERROR_INCORRECT_HANDLE_STATE                  (XHTTP_ERROR_BASE + 19)
#define XHTTP_ERROR_CONNECTION_ERROR                        (XHTTP_ERROR_BASE + 30)
#define XHTTP_ERROR_HEADER_NOT_FOUND                        (XHTTP_ERROR_BASE + 150)
#define XHTTP_ERROR_INVALID_SERVER_RESPONSE                 (XHTTP_ERROR_BASE + 152)
#define XHTTP_ERROR_REDIRECT_FAILED                         (XHTTP_ERROR_BASE + 156)
#define XHTTP_ERROR_NOT_INITIALIZED                         (XHTTP_ERROR_BASE + 172)
#define XHTTP_ERROR_SECURE_FAILURE                          (XHTTP_ERROR_BASE + 175)

#define X_ONLINE_FRIENDSTATE_FLAG_NONE                      0x00000000
#define X_ONLINE_FRIENDSTATE_FLAG_ONLINE                    0x00000001
#define X_ONLINE_FRIENDSTATE_FLAG_PLAYING                   0x00000002
#define X_ONLINE_FRIENDSTATE_FLAG_JOINABLE                  0x00000010

#define X_ONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED            0x10000000
#define X_ONLINE_FRIENDSTATE_FLAG_SENTINVITE                0x04000000

#define X_ONLINE_FRIENDSTATE_ENUM_ONLINE                    0x00000000
#define X_ONLINE_FRIENDSTATE_ENUM_AWAY                      0x00010000
#define X_ONLINE_FRIENDSTATE_ENUM_BUSY                      0x00020000
#define X_ONLINE_FRIENDSTATE_MASK_USER_STATE                0x000F0000

#define X_ONLINE_MAX_FRIENDS                                100
#define X_ONLINE_PEER_SUBSCRIPTIONS                         400
#define X_MAX_RICHPRESENCE_SIZE                             64
#define X_ONLINE_MAX_PATHNAME_LENGTH                        255
#define X_STORAGE_MAX_MEMORY_BUFFER_SIZE                    100000000
#define X_STORAGE_MAX_RESULTS_TO_RETURN                     256
#define X_ONLINE_MAX_XSTRING_VERIFY_LOCALE                  512
#define X_ONLINE_MAX_XSTRING_VERIFY_STRING_DATA             10
#define X_MAX_RICHPRESENCE_SIZE_EXTRA                       100 // 4D5308AB uses rich presence string > 64
#define X_ONLINE_MAX_XINVITE_DISPLAY_STRING                 255
#define X_ONLINE_MAX_STATS_ESTIMATE_RATING_COUNT            101

#define X_PARTY_MAX_USERS                                   32

#define X_MARKETPLACE_CONTENT_ID_LEN                        20
#define X_MARKETPLACE_ASSET_SIGNATURE_SIZE                  256
    
#define X_PROPERTY_TYPE_MASK                                0xF0000000
#define X_PROPERTY_SCOPE_MASK                               0x00008000
#define X_PROPERTY_ID_MASK                                  0x00007FFF

#define X_CONTEXT_GAME_TYPE_RANKED                          0x0
#define X_CONTEXT_GAME_TYPE_STANDARD                        0x1

#define MAX_FIRSTNAME_SIZE                                  64
#define MAX_LASTNAME_SIZE                                   64
#define MAX_EMAIL_SIZE                                      129
#define MAX_STREET_SIZE                                     128
#define MAX_CITY_SIZE                                       64
#define MAX_DISTRICT_SIZE                                   64
#define MAX_STATE_SIZE                                      64
#define MAX_POSTALCODE_SIZE                                 16

// XOnlineQuerySearch
#define X_ATTRIBUTE_DATATYPE_MASK                           0x00F00000
#define X_ATTRIBUTE_DATATYPE_INTEGER                        0x00000000
#define X_ATTRIBUTE_DATATYPE_STRING                         0x00100000
#define X_ATTRIBUTE_DATATYPE_BLOB                           0x00200000

#define X_ONLINE_QUERY_MAX_PAGE                             255
#define X_ONLINE_QUERY_MAX_PAGE_SIZE                        255
#define X_ONLINE_QUERY_MAX_ATTRIBUTES                       255
#define X_MAX_STRING_ATTRIBUTE_LENGTH                       400
#define X_MAX_BLOB_ATTRIBUTE_LENGTH                         800

#define X_ONLINE_LSP_ATTRIBUTE_TSADDR                       0x80200001
#define X_ONLINE_LSP_ATTRIBUTE_XNKID                        0x80200002
#define X_ONLINE_LSP_ATTRIBUTE_KEY                          0x80200003
#define X_ONLINE_LSP_ATTRIBUTE_USER                         0x80100004 // LSP Filter?
#define X_ONLINE_LSP_ATTRIBUTE_PARAM_USER                   0x02100004

#define X_ONLINE_LSP_DEFAULT_DATASET_ID                     0xAAAA

constexpr uint32_t PropertyID(bool system_property,
                              kernel::xam::X_USER_DATA_TYPE type, uint16_t id) {
  return ((system_property ? X_PROPERTY_SCOPE_MASK : 0) |
          ((static_cast<uint8_t>(type) << 28) & X_PROPERTY_TYPE_MASK) |
          (id & X_PROPERTY_ID_MASK));
}

constexpr uint32_t ContextID(bool system_property, uint16_t id) {
  return PropertyID(system_property, kernel::xam::X_USER_DATA_TYPE::CONTEXT,
                    id);
}

enum PropertyID : uint32_t {
  XPROPERTY_ATTACHMENT_SIZE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x011), // 0x10008011
  XPROPERTY_PLAYER_PARTIAL_PLAY_PERCENTAGE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x00C), // 0x1000800C
  XPROPERTY_PLAYER_SKILL_UPDATE_WEIGHTING_FACTOR =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x00D), // 0x1000800D
  XPROPERTY_SESSION_SKILL_BETA =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::DOUBLE, 0x00E), // 0x3000800E
  XPROPERTY_SESSION_SKILL_TAU =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::DOUBLE, 0x00F), // 0x3000800F
  XPROPERTY_SESSION_SKILL_DRAW_PROBABILITY =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x010), // 0x10008010
  XPROPERTY_RELATIVE_SCORE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x00A), // 0x1000800A
  XPROPERTY_SESSION_TEAM =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x00B), // 0x1000800B
  XPROPERTY_RANK =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x001), // 0x10008001
  XPROPERTY_GAMERNAME =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::WSTRING, 0x002), // 0x40008002
  XPROPERTY_SESSION_ID =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT64, 0x003), // 0x20008003
  XPROPERTY_GAMER_ZONE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x101), // 0x10008101
  XPROPERTY_GAMER_COUNTRY =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x102), // 0x10008102
  XPROPERTY_GAMER_LANGUAGE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x103), // 0x10008103
  XPROPERTY_GAMER_RATING =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::FLOAT, 0x104), // 0x50008104
  XPROPERTY_GAMER_MU =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::DOUBLE, 0x105), // 0x30008105
  XPROPERTY_GAMER_SIGMA =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::DOUBLE, 0x106), // 0x30008106
  XPROPERTY_GAMER_PUID =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT64, 0x107), // 0x20008107
  XPROPERTY_AFFILIATE_VALUE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT64, 0x108), // 0x20008108
  XPROPERTY_GAMER_HOSTNAME =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::WSTRING, 0x109), // 0x40008109
  XPROPERTY_PLATFORM_TYPE =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x201), // 0x10008201
  XPROPERTY_PLATFORM_LOCK =
      PropertyID(true, kernel::xam::X_USER_DATA_TYPE::INT32, 0x202), // 0x10008202
};

enum ContextID : uint32_t {
  XCONTEXT_PRESENCE = ContextID(true, 0x001), // 0x00008001
  XCONTEXT_GAME_TYPE = ContextID(true, 0x00A), // 0x0000800A
  XCONTEXT_GAME_MODE = ContextID(true, 0x00B), // 0x0000800B
  XCONTEXT_SESSION_JOINABLE = ContextID(true, 0x00C), // 0x0000800C
};

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

enum HTTP_STATUS_CODE {
  HTTP_OK = 200,
  HTTP_CREATED = 201,
  HTTP_NO_CONTENT = 204,

  HTTP_BAD_REQUEST = 400,
  HTTP_UNAUTHORIZED = 401,
  HTTP_FORBIDDEN = 403,
  HTTP_NOT_FOUND = 404,
  HTTP_PAYLOAD_TOO_LARGE = 413,

  HTTP_INTERNAL_SERVER_ERROR = 500,
  HTTP_NOT_IMPLEMENTED = 501,
  HTTP_BAD_GATEWAY = 502
};

// clang-format on

namespace kernel {

constexpr uint16_t XNET_SYSTEMLINK_PORT = 3074;

constexpr uint32_t XEX_PRIVILEGE_PII_ACCESS = 13;
constexpr uint32_t XEX_PRIVILEGE_CROSSPLATFORM_SYSTEM_LINK = 14;

constexpr uint8_t kXUserMaxStatsRows = 100;

constexpr uint8_t kXUserMaxStatsAttributes = 64;

constexpr uint32_t kTMSUserMaxSize = 8192;          // 8 KB
constexpr uint32_t kTMSTitleMaxSize = 1048576 * 5;  // 5 MB
constexpr uint32_t kTMSClipMaxSize = 1048576 * 11;  // 11 MB
constexpr uint32_t kTMSFileMaxSize = 1048576 * 20;  // 20 MB (Custom)

enum NETWORK_MODE : uint32_t { OFFLINE, LAN, XBOXLIVE };

enum X_USER_AGE_GROUP : uint32_t { CHILD, TEEN, ADULT };

enum class P_MSG_TYPES : uint32_t { FIND_USERS = 1065 };

enum X_STATS_ENUMERATOR_TYPE : uint32_t {
  XUID,
  RANK,
  RANK_PER_SPEC,
  BY_RATING
};

enum PLATFORM_TYPE : uint32_t { Xbox1, Xbox360, PC };

enum X_NAT_TYPE { NAT_OPEN = 1, NAT_MODERATE, NAT_STRICT };

enum X_STORAGE_BUILD_SERVER_PATH_RESULT : int32_t {
  Invalid = -1,
  Created = 0,
  Found = 1,
};

enum X_STORAGE_UPLOAD_RESULT : int32_t {
  UPLOAD_ERROR = -1,
  UPLOADED = 0,
  NOT_MODIFIED = 1,
  PAYLOAD_TOO_LARGE = 2,
};

enum GET_POINTS_BALANCE_RESPONSE_FLAGS : uint32_t { ABOVE_LOW_BALANCE = 1 };

enum DMP_STATUS_TYPE : uint32_t {
  DMP_STATUS_ACTIVE = 0,
  DMP_STATUS_DISABLED = 1,
  DMP_STATUS_CLOSED = 2
};

struct XNKID {
  uint8_t ab[8];
  uint64_t as_uint64() { return *reinterpret_cast<uint64_t*>(&ab); }
  uint64_t as_uintBE64() { return xe::byte_swap(as_uint64()); }
};
static_assert_size(XNKID, 0x8);

struct XNKEY {
  uint8_t ab[16];
};
static_assert_size(XNKEY, 0x10);

#pragma pack(push, 4)

// clang-format off

// Security Gateway Address
struct SGADDR {
  in_addr ina;                                  // IP address of the SG for the client
  xe::be<uint32_t> security_parameter_index;    // Pseudo-random identifier assigned by the SG
  xe::be<uint64_t> xbox_id;                     // Unique identifier of client machine account - machine id?
  uint8_t reserved[4];
};
static_assert_size(SGADDR, 0x14);

// clang-format on

#pragma pack(pop)

struct XNADDR {
  // FYI: IN_ADDR should be in network-byte order.
  in_addr ina;        // IP address (zero if not static/DHCP) - Local IP
  in_addr inaOnline;  // Online IP address (zero if not online) - Public IP
  xe::be<uint16_t> wPortOnline;  // Online port
  uint8_t abEnet[6];             // Ethernet MAC address
  SGADDR abOnline;               // Online identification
};
static_assert_size(XNADDR, 0x24);

typedef XNADDR TSADDR;

struct XSESSION_INFO {
  XNKID sessionID;
  XNADDR hostAddress;
  XNKEY keyExchangeKey;
};
static_assert_size(XSESSION_INFO, 0x3C);

struct XSESSION_REGISTRANT {
  xe::be<uint64_t> machine_id;
  xe::be<uint32_t> trustworthiness;
  xe::be<uint32_t> num_users;
  xe::be<uint32_t> users_ptr;
};
static_assert_size(XSESSION_REGISTRANT, 0x18);

struct XSESSION_REGISTRATION_RESULTS {
  xe::be<uint32_t> registrants_count;
  xe::be<uint32_t> registrants_ptr;
};
static_assert_size(XSESSION_REGISTRATION_RESULTS, 0x8);

struct XSESSION_SEARCHRESULT {
  XSESSION_INFO info;
  xe::be<uint32_t> open_public_slots;
  xe::be<uint32_t> open_private_slots;
  xe::be<uint32_t> filled_public_slots;
  xe::be<uint32_t> filled_private_slots;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> contexts_count;
  xe::be<uint32_t> properties_ptr;
  xe::be<uint32_t> contexts_ptr;
};
static_assert_size(XSESSION_SEARCHRESULT, 0x5C);

struct XSESSION_SEARCHRESULT_HEADER {
  xe::be<uint32_t> search_results_count;
  xe::be<uint32_t> search_results_ptr;
};
static_assert_size(XSESSION_SEARCHRESULT_HEADER, 0x8);

enum class XSESSION_STATE : uint32_t {
  LOBBY,
  REGISTRATION,
  INGAME,
  REPORTING,
  DELETED
};

struct XSESSION_LOCAL_DETAILS {
  xe::be<uint32_t> UserIndexHost;
  xe::be<uint32_t> GameType;
  xe::be<uint32_t> GameMode;
  xe::be<uint32_t> Flags;
  xe::be<uint32_t> MaxPublicSlots;
  xe::be<uint32_t> MaxPrivateSlots;
  xe::be<uint32_t> AvailablePublicSlots;
  xe::be<uint32_t> AvailablePrivateSlots;
  xe::be<uint32_t> ActualMemberCount;
  xe::be<uint32_t> ReturnedMemberCount;
  XSESSION_STATE eState;
  xe::be<uint64_t> Nonce;
  XSESSION_INFO sessionInfo;
  XNKID xnkidArbitration;
  xe::be<uint32_t> SessionMembers_ptr;
};
static_assert_size(XSESSION_LOCAL_DETAILS, 0x80);

struct XSESSION_VIEW_PROPERTIES {
  xe::be<uint32_t> view_id;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> properties_ptr;
};
static_assert_size(XSESSION_VIEW_PROPERTIES, 0xC);

enum class MEMBER_FLAGS : uint32_t { PRIVATE_SLOT = 0x01, ZOMBIE = 0x2 };

struct XSESSION_MEMBER {
  xe::be<uint64_t> OnlineXUID;
  xe::be<uint32_t> UserIndex;
  xe::be<uint32_t> Flags;

  void SetPrivate() {
    Flags |= static_cast<uint32_t>(MEMBER_FLAGS::PRIVATE_SLOT);
  }

  void SetZombie() { Flags |= static_cast<uint32_t>(MEMBER_FLAGS::ZOMBIE); }

  const bool IsPrivate() const {
    return (Flags & static_cast<uint32_t>(MEMBER_FLAGS::PRIVATE_SLOT)) ==
           static_cast<uint32_t>(MEMBER_FLAGS::PRIVATE_SLOT);
  }

  const bool IsZombie() const {
    return (Flags & static_cast<uint32_t>(MEMBER_FLAGS::ZOMBIE)) ==
           static_cast<uint32_t>(MEMBER_FLAGS::ZOMBIE);
  }
};
static_assert_size(XSESSION_MEMBER, 0x10);

struct X_PARTY_CUSTOM_DATA {
  xe::be<uint64_t> first;
  xe::be<uint64_t> second;
};
static_assert_size(X_PARTY_CUSTOM_DATA, 0x10);

struct X_PARTY_USER_INFO {
  xe::be<uint64_t> xuid;
  char gamertag[16];
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> nat_type;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> flags;
  XSESSION_INFO session_info;
  X_PARTY_CUSTOM_DATA custom_data;
};
static_assert_size(X_PARTY_USER_INFO, 0x78);

struct X_PARTY_USER_LIST {
  xe::be<uint32_t> user_count;
  X_PARTY_USER_INFO users[X_PARTY_MAX_USERS];
};
static_assert_size(X_PARTY_USER_LIST, 0xF08);

struct X_PARTY_USER_INFO_INTERNAL {
  xe::be<uint64_t> xuid;
  char gamertag[16];
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> nat_type;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> flags;
  XSESSION_INFO session_info;
  X_PARTY_CUSTOM_DATA custom_data;
  xe::be<uint32_t> peer_id;
  xe::be<uint32_t> mute_mask;
};
static_assert_size(X_PARTY_USER_INFO_INTERNAL, 0x80);

struct X_PARTY_USER_LIST_INTERNAL {
  xe::be<uint32_t> user_count;
  X_PARTY_USER_INFO_INTERNAL users[X_PARTY_MAX_USERS];
};
static_assert_size(X_PARTY_USER_LIST_INTERNAL, 0x1008);

struct X_USER_STATS_VIEW {
  xe::be<uint32_t> ViewId;
  xe::be<uint32_t> TotalViewRows;
  xe::be<uint32_t> NumRows;
  xe::be<uint32_t> pRows;
};
static_assert_size(X_USER_STATS_VIEW, 0x10);

struct X_USER_STATS_COLUMN {
  xe::be<uint16_t> ColumnId;
  xam::X_USER_DATA Value;
};
static_assert_size(X_USER_STATS_COLUMN, 0x18);

struct X_USER_STATS_ROW {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> Rank;
  xe::be<uint64_t> i64Rating;
  CHAR szGamertag[16];
  xe::be<uint32_t> NumColumns;
  xe::be<uint32_t> pColumns;
};
static_assert_size(X_USER_STATS_ROW, 0x30);

struct X_USER_STATS_READ_RESULTS {
  xe::be<uint32_t> num_views;
  xe::be<uint32_t> views_ptr;
};
static_assert_size(X_USER_STATS_READ_RESULTS, 0x8);

struct X_USER_STATS_SPEC {
  xe::be<uint32_t> view_id;
  xe::be<uint32_t> num_column_ids;
  xe::be<uint16_t> column_Ids[kXUserMaxStatsAttributes];
};
static_assert_size(X_USER_STATS_SPEC, 8 + kXUserMaxStatsAttributes * 2);

struct X_USER_ESTIMATE_RANK_RESULTS {
  xe::be<uint32_t> num_ranks;
  xe::be<uint32_t> ranks_ptr;  // uint32_t*
};
static_assert_size(X_USER_ESTIMATE_RANK_RESULTS, 0x8);

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

struct X_MARKETPLACE_CONTENTOFFER_INFO {
  xe::be<uint64_t> offer_id;
  xe::be<uint64_t> preview_offer_id;
  xe::be<uint32_t> offer_name_length;
  xe::be<uint32_t> offer_name_ptr;  // char16_t*
  xe::be<uint32_t> offer_type;
  uint8_t content_id[X_MARKETPLACE_CONTENT_ID_LEN];
  xe::be<uint32_t> is_unrestricted_license;
  xe::be<uint32_t> license_mask;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> content_category;
  xe::be<uint32_t> title_name_length;
  xe::be<uint32_t> title_name_ptr;  // char16_t*
  xe::be<uint32_t> user_has_purchased;
  xe::be<uint32_t> package_size;
  xe::be<uint32_t> install_size;
  xe::be<uint32_t> sell_text_length;
  xe::be<uint32_t> sell_text_ptr;  // char16_t*
  xe::be<uint32_t> asset_id;
  xe::be<uint32_t> purchase_quantity;
  xe::be<uint32_t> points_price;
};
static_assert_size(X_MARKETPLACE_CONTENTOFFER_INFO, 0x68);

struct X_MARKETPLACE_ASSET {
  xe::be<uint32_t> asset_id;
  xe::be<uint32_t> quantity;
};
static_assert_size(X_MARKETPLACE_ASSET, 0x8);

struct X_MARKETPLACE_ASSET_PACKAGE {
  X_FILETIME filetime_enumerate;
  xe::be<uint32_t> num_assets;
  xe::be<uint32_t> total_assets;
  X_MARKETPLACE_ASSET assets[1];
};
static_assert_size(X_MARKETPLACE_ASSET_PACKAGE, 0x18);

struct X_MARKETPLACE_ASSET_ENUMERATE_REPLY {
  uint8_t signature[X_MARKETPLACE_ASSET_SIGNATURE_SIZE];
  X_MARKETPLACE_ASSET_PACKAGE asset_package;
};
static_assert_size(X_MARKETPLACE_ASSET_ENUMERATE_REPLY, 0x118);

#pragma region XLiveBase

struct X_ARGUMENT_ENTRY {
  xe::be<uint32_t> native_size;  // 4
  xe::be<uint64_t> argument_value_ptr;
};
static_assert_size(X_ARGUMENT_ENTRY, 0x10);

struct __declspec(align(8)) X_ARGUMENT_LIST {
  X_ARGUMENT_ENTRY entry[32];
  xe::be<uint32_t> argument_count;
};
static_assert_size(X_ARGUMENT_LIST, 0x208);

enum X_STORAGE_FACILITY : uint32_t {
  FACILITY_INVALID = 0,
  FACILITY_GAME_CLIP = 1,      // Read, Write
  FACILITY_PER_TITLE = 2,      // Read, Enumerate
  FACILITY_PER_USER_TITLE = 3  // Read, Write, Delete
};

struct X_STORAGE_BUILD_SERVER_PATH {
  xe::be<uint32_t> user_index;
  xe::be<uint64_t> xuid;
  xe::be<X_STORAGE_FACILITY> storage_location;
  xe::be<uint32_t> storage_location_info_ptr;
  xe::be<uint32_t> storage_location_info_size;
  xe::be<uint32_t> file_name_ptr;
  xe::be<uint32_t> server_path_ptr;
  xe::be<uint32_t> server_path_length_ptr;
};
static_assert_size(X_STORAGE_BUILD_SERVER_PATH, 0x28);

struct X_STORAGE_FACILITY_INFO_GAME_CLIP {
  xe::be<uint32_t> leaderboard_id;
};
static_assert_size(X_STORAGE_FACILITY_INFO_GAME_CLIP, 0x4);

struct X_MUTE_SET_STATE {
  xe::be<uint32_t> user_index;
  xe::be<uint64_t> remote_xuid;
  xe::be<uint32_t> set_muted;
};

struct X_CREATE_FRIENDS_ENUMERATOR {
  X_ARGUMENT_ENTRY user_index;
  X_ARGUMENT_ENTRY friends_starting_index;
  X_ARGUMENT_ENTRY friends_amount;
  X_ARGUMENT_ENTRY buffer_ptr;
  X_ARGUMENT_ENTRY handle_ptr;
};

struct X_PRESENCE_INITIALIZE {
  X_ARGUMENT_ENTRY max_peer_subscriptions;
};

struct X_PRESENCE_SUBSCRIBE {
  X_ARGUMENT_ENTRY user_index;
  X_ARGUMENT_ENTRY peers;
  X_ARGUMENT_ENTRY peer_xuids_ptr;
};

struct X_PRESENCE_UNSUBSCRIBE {
  X_ARGUMENT_ENTRY user_index;
  X_ARGUMENT_ENTRY peers;
  X_ARGUMENT_ENTRY peer_xuids_ptr;
};

struct X_PRESENCE_CREATE {
  X_ARGUMENT_ENTRY user_index;
  X_ARGUMENT_ENTRY num_peers;
  X_ARGUMENT_ENTRY peer_xuids_ptr;
  X_ARGUMENT_ENTRY starting_index;
  X_ARGUMENT_ENTRY max_peers;
  X_ARGUMENT_ENTRY buffer_length_ptr;      // output
  X_ARGUMENT_ENTRY enumerator_handle_ptr;  // output
};

struct X_INVITE_GET_ACCEPTED_INFO {
  X_ARGUMENT_ENTRY user_index;
  X_ARGUMENT_ENTRY invite_info;
};

struct X_CONTENT_GET_MARKETPLACE_COUNTS {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> content_categories;
  xe::be<uint32_t> results_ptr;
};
static_assert_size(X_CONTENT_GET_MARKETPLACE_COUNTS, 0x10);

struct X_OFFERING_CONTENTAVAILABLE_RESULT {
  xe::be<uint32_t> new_offers;
  xe::be<uint32_t> total_offers;
};
static_assert_size(X_OFFERING_CONTENTAVAILABLE_RESULT, 0x8);

struct X_GET_TASK_PROGRESS {
  xe::be<uint32_t> overlapped_ptr;
  xe::be<uint32_t> percent_complete_ptr;
  xe::be<uint32_t> numerator_ptr;
  xe::be<uint32_t> denominator_ptr;
};
static_assert_size(X_GET_TASK_PROGRESS, 0x10);

#pragma pack(push, 4)

struct XLIVEBASE_GET_SEQUENCE {
  xe::be<uint32_t> seq_num;
  xe::be<uint32_t> msg_length;
};
static_assert_size(XLIVEBASE_GET_SEQUENCE, 0x8);

struct BASE_MSG_HEADER {
  P_MSG_TYPES msg_type;
  uint32_t msg_length;
  uint32_t seq_num;
  SGADDR sgaddr;  // XnpLogonGetStatus
};
static_assert_size(BASE_MSG_HEADER, 0x20);

struct X_ONLINE_PRESENCE {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> state;
  XNKID session_id;
  xe::be<uint32_t> title_id;
  X_FILETIME state_change_time;
  xe::be<uint32_t> cchRichPresence;
  xe::be<char16_t> wszRichPresence[X_MAX_RICHPRESENCE_SIZE];
};
static_assert_size(X_ONLINE_PRESENCE, 0xA4);

struct X_ONLINE_FRIEND {
  xe::be<uint64_t> xuid;
  char Gamertag[16];
  xe::be<uint32_t> state;
  XNKID session_id;
  xe::be<uint32_t> title_id;
  X_FILETIME ftUserTime;
  XNKID xnkidInvite;
  X_FILETIME gameinviteTime;
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
static_assert_size(X_INVITE_INFO, 0x54);

struct X_USER_RANK_REQUEST {
  uint32_t view_id;
  uint64_t i64Rating;
};
static_assert_size(X_USER_RANK_REQUEST, 0xC);

#pragma pack(pop)

#pragma pack(push, 2)

struct X_GET_POINTS_BALANCE_RESPONSE {
  xe::be<uint32_t> balance;
  uint8_t dmp_account_status;
  uint8_t response_flags;
};
static_assert_size(X_GET_POINTS_BALANCE_RESPONSE, 0x6);

struct CONTENT_ENUMERATE_RESPONSE {
  xe::be<uint16_t> content_returned;
  xe::be<uint32_t> enumerate_content_info_ptr;
  xe::be<uint32_t> content_total;
};
static_assert_size(CONTENT_ENUMERATE_RESPONSE, 0xA);

struct SUBSCRIPTION_ENUMERATE_RESPONSE {
  xe::be<uint16_t> offers_returned;
  xe::be<uint32_t> subscription_info_ptr;
  xe::be<uint32_t> offers_total;
};
static_assert_size(SUBSCRIPTION_ENUMERATE_RESPONSE, 0xA);

#pragma pack(pop)

#pragma pack(push, 1)

struct X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS {
  xe::be<uint32_t> bytes_total;
  xe::be<uint64_t> xuid_owner;
  X_FILETIME ft_created;
};
static_assert_size(X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS, 0x14);

struct X_STORAGE_FILE_INFO {
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> title_version;
  xe::be<uint64_t> owner_puid;
  xe::be<uint8_t> country_id;
  xe::be<uint64_t> reserved;
  xe::be<uint32_t> content_type;
  xe::be<uint32_t> storage_size;
  xe::be<uint32_t> installed_size;
  X_FILETIME ft_created;
  X_FILETIME ft_last_modified;
  xe::be<uint16_t> attributes_size;
  xe::be<uint16_t> path_name;
  xe::be<uint32_t> path_name_ptr;
  xe::be<uint32_t> attributes_ptr;  // Reserved
};
static_assert_size(X_STORAGE_FILE_INFO, 0x41);

struct X_STORAGE_ENUMERATE_RESULTS {
  xe::be<uint32_t> total_num_items;
  xe::be<uint32_t> num_items_returned;
  xe::be<uint32_t> items_ptr;
};
static_assert_size(X_STORAGE_ENUMERATE_RESULTS, 0xC);

struct STRING_VERIFY_RESPONSE {
  xe::be<uint16_t> num_strings;
  xe::be<uint32_t> string_result_ptr;
};
static_assert_size(STRING_VERIFY_RESPONSE, 0x6);

struct FIND_USER_INFO {
  xe::be<uint64_t> xuid;
  char gamertag[16];
};
static_assert_size(FIND_USER_INFO, 0x18);

struct FIND_USERS_RESPONSE {
  xe::be<uint32_t> results_size;
  xe::be<uint32_t> users_address;
};
static_assert_size(FIND_USERS_RESPONSE, 0x8);

struct X_ADDRESS_INFO {
  xe::be<uint16_t> street_1_length;
  xe::be<uint32_t> street_1;  // uint16_t*
  xe::be<uint16_t> street_2_length;
  xe::be<uint32_t> street_2;  // uint16_t*
  xe::be<uint16_t> city_length;
  xe::be<uint32_t> city;  // uint16_t*
  xe::be<uint16_t> district_length;
  xe::be<uint32_t> district;  // uint16_t*
  xe::be<uint16_t> state_length;
  xe::be<uint32_t> state;  // uint16_t*
  xe::be<uint16_t> postal_code_length;
  xe::be<uint32_t> postal_code;  // uint16_t*
};
static_assert_size(X_ADDRESS_INFO, 0x24);

struct X_GET_USER_INFO_RESPONSE {
  xe::be<uint16_t> first_name_length;
  xe::be<uint32_t> first_name;  // uint16_t*
  xe::be<uint16_t> last_name_length;
  xe::be<uint32_t> last_name;  // uint16_t*
  X_ADDRESS_INFO address_info;
  xe::be<uint16_t> email_length;
  xe::be<uint32_t> email;  // uint16_t*
  xe::be<uint16_t> language_id;
  xe::be<uint8_t> country_id;
  xe::be<uint8_t> msft_optin;
  xe::be<uint8_t> parter_optin;
  xe::be<uint8_t> age;
};
static_assert_size(X_GET_USER_INFO_RESPONSE, 0x3C);

struct X_ONLINE_QUERY_ATTRIBUTE_INTEGER {
  xe::be<uint32_t> length;
  xe::be<uint64_t> value;
};
static_assert_size(X_ONLINE_QUERY_ATTRIBUTE_INTEGER, 0xC);

struct X_ONLINE_QUERY_ATTRIBUTE_STRING {
  xe::be<uint32_t> length;
  xe::be<uint32_t> value_ptr;  // char16_t*
};
static_assert_size(X_ONLINE_QUERY_ATTRIBUTE_STRING, 0x8);

struct X_ONLINE_QUERY_ATTRIBUTE_BLOB {
  xe::be<uint32_t> length;
  xe::be<uint32_t> value_ptr;  // uint8_t*
};
static_assert_size(X_ONLINE_QUERY_ATTRIBUTE_BLOB, 0x8);

union X_ONLINE_QUERY_ATTRIBUTE_DATA {
  X_ONLINE_QUERY_ATTRIBUTE_INTEGER integer;
  X_ONLINE_QUERY_ATTRIBUTE_STRING string;
  X_ONLINE_QUERY_ATTRIBUTE_BLOB blob;
};
static_assert_size(X_ONLINE_QUERY_ATTRIBUTE_DATA, 0xC);

struct X_ONLINE_QUERY_ATTRIBUTE {
  xe::be<uint32_t> attribute_id;
  X_ONLINE_QUERY_ATTRIBUTE_DATA info;
};
static_assert_size(X_ONLINE_QUERY_ATTRIBUTE, 0x10);

struct X_ONLINE_QUERY_ATTRIBUTE_SPEC {
  uint32_t type;
  uint32_t length;
};
static_assert_size(X_ONLINE_QUERY_ATTRIBUTE_SPEC, 0x8);

struct QUERY_SEARCH_RESULT {
  xe::be<uint32_t> total_results;
  xe::be<uint32_t> returned_results;
  xe::be<uint32_t> num_result_attributes;
  xe::be<uint32_t> attributes_ptr;  // X_ONLINE_QUERY_ATTRIBUTE
};
static_assert_size(QUERY_SEARCH_RESULT, 0x10);

struct XACCOUNT_GET_POINTS_BALANCE_REQUEST {
  uint64_t xuid;
  uint64_t machine_id;  // XNetLogonGetMachineID
};
static_assert_size(XACCOUNT_GET_POINTS_BALANCE_REQUEST, 0x10);

struct GENRES_ENUMERATE_REQUEST {
  uint8_t user_country;  // XamUserGetOnlineCountryFromXUID
  uint16_t language;     // XLanguage
  uint32_t start_index;
  uint32_t max_count;
  uint16_t game_rating;
  uint8_t tier_required;
  uint32_t offer_type;
  uint32_t parent_genreid;
};
static_assert_size(GENRES_ENUMERATE_REQUEST, 0x16);

struct GENRES_ENUMERATE_RESPONSE {
  xe::be<uint16_t> geners_returned;
  xe::be<uint32_t> enumerate_genre_info_ptr;
  xe::be<uint32_t> geners_total;
};
static_assert_size(GENRES_ENUMERATE_RESPONSE, 0xA);

struct GENRE_INFO {
  xe::be<uint32_t> genre_id;
  xe::be<uint16_t> localized_genre_length;
  xe::be<uint32_t> localized_genre_name;
};
static_assert_size(GENRE_INFO, 0xA);

enum class SUBSCRIPTION_ENUMERATE_FLAGS : uint16_t {
  New = 1,
  Renewals = 2,
  Current = 4,
  Expired = 8,
  Suspended = 16,
};

struct SUBSCRIPTION_ENUMERATE_REQUEST {
  uint64_t xuid;
  uint64_t machine_id;  // XNetLogonGetMachineID
  uint8_t user_tier;
  uint8_t country_id;
  uint16_t language_id;
  uint16_t game_rating;
  uint32_t offer_type;
  uint32_t payment_type;
  uint32_t title_id;
  uint32_t title_categories;
  uint16_t request_flags;
  uint32_t starting_index;
  uint32_t max_results;
};
static_assert_size(SUBSCRIPTION_ENUMERATE_REQUEST, 0x30);

struct SUBSCRIPTION_INFO {
  xe::be<uint64_t> offer_id;
  xe::be<uint16_t> offer_name_length;
  xe::be<uint32_t> offer_name;  // char16_t*
  xe::be<uint32_t> offer_type;
  xe::be<uint8_t> relation_type;
  xe::be<uint8_t> convert_mode;
  xe::be<uint16_t> instance_id_length;
  xe::be<uint32_t> instance_id;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> title_category;
  xe::be<uint16_t> title_name_length;
  xe::be<uint32_t> title_name;  // char16_t*
  xe::be<uint16_t> game_rating;
  xe::be<uint8_t> duration;
  xe::be<uint8_t> frequency;
  xe::be<uint8_t> tier_provided;
  xe::be<uint8_t> tier_required;
  xe::be<uint32_t> sell_text_length;
  xe::be<uint32_t> sell_text;  // char16_t*
  xe::be<uint64_t> related_offer_id;
  xe::be<uint16_t> response_flags;
  xe::be<uint8_t> prices_length;
  xe::be<uint32_t> prices;  // OFFER_PRICE*
};
static_assert_size(SUBSCRIPTION_INFO, 0x45);

enum class ENUMERATE_TITLES_BY_FILTER_FLAGS : uint16_t {
  New = 1,
  Played = 2,
};

struct ENUMERATE_TITLES_BY_FILTER {
  uint64_t xuid;
  uint8_t user_country;  // XamUserGetOnlineCountryFromXUID
  uint16_t language;     // XLanguage
  uint32_t start_index;
  uint32_t max_count;
  uint16_t game_rating;
  uint8_t tier_required;
  uint32_t genre_id;
  uint32_t offer_type;
  uint16_t request_flags;
};
static_assert_size(ENUMERATE_TITLES_BY_FILTER, 0x20);

struct ENUMERATE_TITLES_BY_FILTER_RESPONSE {
  xe::be<uint32_t> titles_returned;
  xe::be<uint32_t> enumerate_title_info_ptr;
  xe::be<uint32_t> total_titles_count;
};
static_assert_size(ENUMERATE_TITLES_BY_FILTER_RESPONSE, 0xC);

struct ENUMERATE_TITLES_INFO {
  xe::be<uint16_t> title_name_length;
  xe::be<uint32_t> title_name;  // char16_t*
  xe::be<uint32_t> title_id;
  xe::be<uint8_t> played;
  xe::be<uint32_t> purchased_content_count;
  xe::be<uint32_t> total_content_count;
  xe::be<uint8_t> new_content_exists;
};
static_assert_size(ENUMERATE_TITLES_INFO, 0x14);

struct CONTENT_ENUMERATE_REQUEST {
  uint64_t xuid;
  uint8_t user_country;  // XamUserGetOnlineCountryFromXUID
  uint16_t language;     // XLanguage
  uint16_t game_rating;
  uint32_t offer_type;
  uint32_t payment_type;
  uint8_t tier_required;
  uint32_t title_id;
  uint32_t title_categories;
  uint8_t request_flags;
  uint32_t starting_index;
  uint32_t max_results;
};
static_assert_size(CONTENT_ENUMERATE_REQUEST, 0x27);

struct CONTENT_INFO {
  xe::be<uint64_t> offer_id;
  xe::be<uint16_t> offer_name_length;
  xe::be<uint32_t> offer_name;  // char16_t*
  xe::be<uint32_t> offer_type;
  xe::be<uint8_t> content_id;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> title_category;
  xe::be<uint16_t> title_name_length;
  xe::be<uint32_t> title_name;  // char16_t*
  xe::be<uint8_t> tier_required;
  xe::be<uint16_t> game_rating;
  xe::be<uint16_t> response_flags;
  xe::be<uint32_t> package_size;
  xe::be<uint32_t> install_size;
  xe::be<uint32_t> sell_text_length;
  xe::be<uint32_t> sell_text;  // char16_t*
  xe::be<uint8_t> prices_length;
  xe::be<uint32_t> prices;  // OFFER_PRICE*
  xe::be<uint32_t> unkn1;
  xe::be<uint32_t> unkn2;
  xe::be<uint32_t> unkn3;
  xe::be<uint32_t> unkn4;
  xe::be<uint16_t> unkn5;
  xe::be<uint8_t> unkn6;
};
static_assert_size(CONTENT_INFO, 0x4E);

enum class BANNER_LEVEL : uint8_t { BannerOnly = 1, HotList = 2 };

struct GET_BANNER_LIST_REQUEST {
  uint64_t xuid;
  uint32_t language;  // XLanguage
  uint8_t level;
  uint32_t starting_index;
  uint32_t max_results;
};
static_assert_size(GET_BANNER_LIST_REQUEST, 0x15);

struct GET_BANNER_LIST_RESPONSE {
  xe::be<uint64_t> expires;
  xe::be<uint32_t> culture_id;
  xe::be<uint16_t> banner_count_total;
  xe::be<uint16_t> banner_count;
  xe::be<uint32_t> banner_list;  // BANNER_LIST_ENTRY*
};
static_assert_size(GET_BANNER_LIST_RESPONSE, 0x14);

struct BANNER_LIST_ENTRY {
  xe::be<uint8_t> banner_type;
  xe::be<uint32_t> is_my_game;
  xe::be<uint16_t> width;
  xe::be<uint16_t> height;
  xe::be<uint16_t> path_length;
  xe::be<uint32_t> path;
};
static_assert_size(BANNER_LIST_ENTRY, 0xF);

struct BANNER_LIST_HOT_ENTRY {
  xe::be<uint8_t> banner_type;
  xe::be<uint32_t> is_my_game;
  xe::be<uint16_t> width;
  xe::be<uint16_t> height;
  xe::be<uint16_t> path_length;
  xe::be<uint32_t> path;
  xe::be<uint32_t> title_id;
  xe::be<uint16_t> title_name_length;
  xe::be<uint32_t> title_name;
  xe::be<uint64_t> offer_id;
  xe::be<uint16_t> offer_name_length;
  xe::be<uint32_t> offer_name;
  xe::be<uint32_t> price;  // OFFER_PRICE*
  xe::be<uint64_t> date_approved;
};
static_assert_size(BANNER_LIST_HOT_ENTRY, 0x33);

struct OFFER_PRICE {
  xe::be<uint32_t> payment_type;
  xe::be<uint8_t> tax_type;
  xe::be<uint32_t> whole_price;
  xe::be<uint32_t> fractional_price;
  xe::be<uint16_t> price_text_length;
  xe::be<uint32_t> price_text;  // char16_t*
};
static_assert_size(OFFER_PRICE, 0x13);

#pragma pack(pop)

struct SCHEMA_HEADER {
  xe::be<uint16_t> SchemaVersionMajor;
  xe::be<uint16_t> SchemaVersionMinor;
  xe::be<uint32_t> ToolVersion;
  xe::be<uint32_t> Flags;
  xe::be<uint32_t> CompressedSize;
  xe::be<uint32_t> UncompressedSize;
  xe::be<uint32_t> ConstantsTableOffset;
  xe::be<uint16_t> ConstantsTableSize;
  xe::be<uint16_t> ConstantSize;
  xe::be<uint32_t> UrlTableOffset;
  xe::be<uint16_t> UrlTableSize;
  xe::be<uint16_t> UrlTableDataSize;
  xe::be<uint16_t> HeaderSize;
  xe::be<uint16_t> ExtensionDataSize;
  xe::be<uint16_t> SchemaTableEntries;
  xe::be<uint16_t> SchemaTableEntrySize;
};
static_assert_size(SCHEMA_HEADER, 0x2C);

struct ORDINAL_TO_INDEX {
  xe::be<uint16_t> Ordinal;
  xe::be<uint16_t> Index;
};
static_assert_size(ORDINAL_TO_INDEX, 0x4);

struct SCHEMA_TABLE_ENTRY {
  xe::be<uint16_t> RequestSchemaSize;
  xe::be<uint16_t> ResponseSchemaSize;
  xe::be<uint32_t> RequestSchemaOffset;
  xe::be<uint32_t> ResponseSchemaOffset;
  xe::be<uint32_t> MaxRequestAggregateSize;
  xe::be<uint32_t> MaxResponseAggregateSize;
  xe::be<uint16_t> ServiceIDIndex;
  xe::be<uint16_t> RequestUrlIndex;
};
static_assert_size(SCHEMA_TABLE_ENTRY, 0x18);

struct SCHEMA_DATA {
  SCHEMA_HEADER Header;
  xe::be<uint32_t> OrdinalToIndexPtr;
  xe::be<uint32_t> TableEntriesPtr;
  xe::be<uint32_t> SchemaDataPtr;
  xe::be<uint32_t> SchemaDataSize;
  xe::be<uint32_t> ExtensionDataPtr;
  xe::be<uint32_t> ConstantListPtr;
  xe::be<uint32_t> UrlOffsetsPtr;
  xe::be<uint32_t> UrlDataPtr;
};
static_assert_size(SCHEMA_DATA, 0x4C);

struct BASE_ENDIAN_BUFFER {
  xe::be<uint32_t> BufferPtr;
  xe::be<uint32_t> BufferSize;
  xe::be<uint32_t> AvailableSize;
  xe::be<uint32_t> ConsumedSize;
  xe::be<int32_t> ReverseEndian;
};
static_assert_size(BASE_ENDIAN_BUFFER, 0x14);

struct XLIVE_ASYNC_TASK {
  xe::be<uint32_t> ordinal;
  xe::be<uint32_t> schema_data_ptr;  // SCHEMA_DATA*
  xe::be<uint32_t> schema_index;
  xe::be<uint32_t> task_flags;
  xe::be<uint32_t> live_async_task_internal_ptr;  // XLiveAsyncTaskInternal*
  xe::be<uint32_t> internal_task_size;
  xe::be<uint32_t> marshalled_request_ptr;
  xe::be<uint32_t> marshalled_request_size;
  xe::be<uint32_t> total_wire_buffe_size;
  xe::be<uint32_t> counter;
  xe::be<uint32_t> logon_id;
  xe::be<uint32_t> results_ptr;  // STRUCT*
  xe::be<uint32_t> results_size;
  BASE_ENDIAN_BUFFER wire_buffer;
  xe::be<uint32_t> overlapped_ptr;
};
static_assert_size(XLIVE_ASYNC_TASK, 0x4C);

struct XLIVEBASE_ASYNC_MESSAGE {
  xe::be<uint32_t> xlive_async_task_ptr;
  xe::be<uint64_t> current_numerator;
  xe::be<uint64_t> current_denominator;
  xe::be<uint64_t> last_numerator;
  xe::be<uint64_t> last_denominator;
};
static_assert_size(XLIVEBASE_ASYNC_MESSAGE, 0x28);

struct XLIVEBASE_UPDATE_ACCESS_TIMES {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> content_categories;
};

struct XLIVEBASE_MESSAGES_ENUMERATOR {
  X_ARGUMENT_ENTRY xuid;
  X_ARGUMENT_ENTRY messages_count_ptr;
  X_ARGUMENT_ENTRY message_summaries_ptr;
};
static_assert_size(XLIVEBASE_MESSAGES_ENUMERATOR, 0x30);

struct XLIVEBASE_PRESENCE_GET_STATE {
  X_ARGUMENT_ENTRY xuid;
  X_ARGUMENT_ENTRY state_flags_ptr;
  X_ARGUMENT_ENTRY session_id_ptr;
};
static_assert_size(XLIVEBASE_PRESENCE_GET_STATE, 0x30);

struct X_MESSAGE_SUMMARY {
  xe::be<uint64_t> sender_id;
  xe::be<uint64_t> message_context;
  X_FILETIME send_time;
  xe::be<uint32_t> message_id;
  xe::be<uint32_t> message_flags;
  xe::be<uint32_t> sender_title_id;
  xe::be<uint16_t> expire_minutes;
  xe::be<uint16_t> details_size;
  uint8_t msg_type;
  char sender_name[15];
  xe::be<char16_t> subject[20];
};
static_assert_size(X_MESSAGE_SUMMARY, 0x60);

inline uint32_t XAccountGetUserInfoResponseSize() {
  return sizeof(X_GET_USER_INFO_RESPONSE) +
         (MAX_FIRSTNAME_SIZE * sizeof(char16_t)) +
         (MAX_LASTNAME_SIZE * sizeof(char16_t)) +
         (MAX_EMAIL_SIZE * sizeof(char16_t)) +
         (MAX_STREET_SIZE * sizeof(char16_t)) +
         (MAX_STREET_SIZE * sizeof(char16_t)) +
         (MAX_CITY_SIZE * sizeof(char16_t)) +
         (MAX_DISTRICT_SIZE * sizeof(char16_t)) +
         (MAX_STATE_SIZE * sizeof(char16_t)) +
         (MAX_POSTALCODE_SIZE * sizeof(char16_t));
}

#pragma endregion

constexpr uint8_t XNKID_ONLINE = 0xAE;
constexpr uint8_t XNKID_SYSTEM_LINK = 0x00;
constexpr uint8_t XNKID_SERVER = 0xC0;

inline bool IsOnlinePeer(uint64_t session_id) {
  return ((session_id >> 56) & 0xFF) == XNKID_ONLINE;
}

inline bool IsSystemlink(uint64_t session_id) {
  return ((session_id >> 56) & 0xFF) == XNKID_SYSTEM_LINK;
}

inline bool IsServer(uint64_t session_id) {
  return ((session_id >> 56) & 0xFF) == XNKID_SERVER;
}

inline bool IsValidXNKID(uint64_t session_id) {
  if (!IsOnlinePeer(session_id) && !IsSystemlink(session_id) &&
          !IsServer(session_id) ||
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

inline bool IsDeadSg(SGADDR sgaddr) {
  return (sgaddr.security_parameter_index == 0) && (sgaddr.xbox_id == 0);
}

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_H_