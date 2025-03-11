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
#define X_STORAGE_MAX_MEMORY_BUFFER_SIZE            100000000
#define X_STORAGE_MAX_RESULTS_TO_RETURN             256
#define X_ONLINE_MAX_XSTRING_VERIFY_LOCALE          512
#define X_ONLINE_MAX_XSTRING_VERIFY_STRING_DATA     10

#define X_CONTEXT_PRESENCE                          0x00008001
#define X_CONTEXT_GAME_TYPE                         0x0000800A
#define X_CONTEXT_GAME_MODE                         0x0000800B

#define X_CONTEXT_GAME_TYPE_RANKED                  0x0
#define X_CONTEXT_GAME_TYPE_STANDARD                0x1

#define X_PARTY_MAX_USERS                           32

#define MAX_FIRSTNAME_SIZE                          64
#define MAX_LASTNAME_SIZE                           64
#define MAX_EMAIL_SIZE                              129
#define MAX_STREET_SIZE                             128
#define MAX_CITY_SIZE                               64
#define MAX_DISTRICT_SIZE                           64
#define MAX_STATE_SIZE                              64
#define MAX_POSTALCODE_SIZE                         16

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

constexpr uint32_t XEX_PRIVILEGE_PII_ACCESS = 13;
constexpr uint32_t XEX_PRIVILEGE_CROSSPLATFORM_SYSTEM_LINK = 14;

constexpr uint8_t kXUserMaxStatsRows = 100;

constexpr uint8_t kXUserMaxStatsAttributes = 64;

constexpr uint32_t kTMSUserMaxSize = 8192;          // 8 KB
constexpr uint32_t kTMSTitleMaxSize = 1048576 * 5;  // 5 MB
constexpr uint32_t kTMSClipMaxSize = 1048576 * 11;  // 11 MB

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

enum X_STORAGE_UPLOAD_RESULT : int32_t {
  UPLOAD_ERROR = -1,
  UPLOADED = 0,
  NOT_MODIFIED = 1,
  PAYLOAD_TOO_LARGE = 2,
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

struct X_CONTENT_GET_MARKETPLACE_COUNTS {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> content_categories;
  xe::be<uint32_t> results_ptr;
};

struct X_OFFERING_CONTENTAVAILABLE_RESULT {
  xe::be<uint32_t> new_offers;
  xe::be<uint32_t> total_offers;
};

struct X_GET_TASK_PROGRESS {
  xe::be<uint32_t> overlapped_ptr;
  xe::be<uint32_t> percent_complete_ptr;
  xe::be<uint32_t> numerator_ptr;
  xe::be<uint32_t> denominator_ptr;
};

// struct FILETIME {
//   xe::be<uint32_t> dwHighDateTime;
//   xe::be<uint32_t> dwLowDateTime;
// };

#pragma pack(push, 4)

// Security Gateway Address
struct SGADDR {
  in_addr ina_security_gateway;
  xe::be<uint32_t> security_parameter_index;
  xe::be<uint64_t> xbox_id;
  uint8_t reserved[4];
};

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

#pragma pack(push, 1)

struct X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS {
  xe::be<uint32_t> bytes_total;
  xe::be<uint64_t> xuid_owner;
  xe::be<uint64_t> ft_created;
};

struct X_STORAGE_FILE_INFO {
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> title_version;
  xe::be<uint64_t> owner_puid;
  xe::be<uint8_t> country_id;
  xe::be<uint64_t> reserved;
  xe::be<uint32_t> content_type;
  xe::be<uint32_t> storage_size;
  xe::be<uint32_t> installed_size;
  xe::be<uint64_t> ft_created;
  xe::be<uint64_t> ft_last_modified;
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
struct STRING_VERIFY_RESPONSE {
  xe::be<uint16_t> num_strings;
  xe::be<uint32_t> string_result_ptr;
};
static_assert_size(STRING_VERIFY_RESPONSE, 0x6);

struct FIND_USER_INFO {
  xe::be<uint64_t> xuid;
  char gamertag[16];
};

struct FIND_USERS_RESPONSE {
  xe::be<uint32_t> results_size;
  xe::be<uint32_t> users_address;
};

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

struct X_ONLINE_QUERY_ATTRIBUTE_INTEGER {
  xe::be<uint32_t> length;
  xe::be<uint64_t> value;
};

struct X_ONLINE_QUERY_ATTRIBUTE_STRING {
  xe::be<uint32_t> length;
  xe::be<uint32_t> value_ptr;
};

struct X_ONLINE_QUERY_ATTRIBUTE_BLOB {
  xe::be<uint32_t> length;
  xe::be<uint32_t> value_ptr;
};

union X_ONLINE_QUERY_ATTRIBUTE_DATA {
  X_ONLINE_QUERY_ATTRIBUTE_INTEGER integer;
  X_ONLINE_QUERY_ATTRIBUTE_STRING string;
  X_ONLINE_QUERY_ATTRIBUTE_BLOB blob;
};

struct X_ONLINE_QUERY_ATTRIBUTE {
  xe::be<uint32_t> attribute_id;
  X_ONLINE_QUERY_ATTRIBUTE_DATA info;
};

struct X_ONLINE_QUERY_ATTRIBUTE_SPEC {
  xe::be<uint32_t> type;
  xe::be<uint32_t> length;
};

struct QUERY_SEARCH_RESULT {
  xe::be<uint32_t> total_results;
  xe::be<uint32_t> returned_results;
  xe::be<uint32_t> num_result_attributes;
  xe::be<uint32_t> attributes_ptr;  // X_ONLINE_QUERY_ATTRIBUTE
};

struct X_GET_POINTS_BALANCE_RESPONSE {
  xe::be<uint32_t> balance;
  xe::be<uint16_t> unkn;
};

struct X_GET_FEATURED_DOWNLOADS_RESPONSE {
  uint8_t data[12];
  xe::be<uint16_t> entries;
  xe::be<uint32_t> flags;  // 0xFFFFFFFF = Free
};

struct X_DATA_ARGS_5008C {
  uint64_t xuid;
  uint32_t unkn;
  uint8_t value_const_1;   // 1
  uint32_t value_const_2;  // 0
  uint32_t value_const_3;  // 256
};

struct X_DATA_ARGS_50077 {
  uint64_t xuid;
  uint64_t machine_id;  // XNetLogonGetMachineID
};

struct X_DATA_ARGS_5008B {
  uint64_t xuid;
  uint32_t language;    // XLanguage
  uint8_t value_const;  // 2
  uint32_t unkn1;
  uint32_t unkn2;
};

struct X_DATA_ARGS_50090 {
  uint8_t user_country;  // XamUserGetOnlineCountryFromXUID
  uint16_t language;     // XLanguage
  uint32_t unkn1;
  uint32_t unkn2;
  uint16_t unkn3;
  uint8_t unkn4;
  uint32_t unkn5;
  uint32_t unkn6;
  uint32_t unkn7;
};

struct X_DATA_ARGS_50091 {
  uint64_t xuid;
  uint8_t user_country;  // XamUserGetOnlineCountryFromXUID
  uint16_t language;     // XLanguage
  uint32_t unkn1;
  uint32_t unkn2;
  uint16_t unkn3;
  uint8_t unkn4;
  uint32_t unkn5;
  uint32_t unkn6;
  uint32_t unkn7;
};

struct X_DATA_ARGS_5008F {
  uint64_t xuid;
  uint8_t user_country;  // XamUserGetOnlineCountryFromXUID
  uint16_t language;     // XLanguage
  uint16_t unkn1;
  uint32_t unkn2;
  uint32_t unkn3;
  uint8_t unkn4;
  uint32_t unkn5;
  uint32_t unkn6;
  uint16_t unkn7;
  xe::be<uint32_t> unkn8;
};

struct X_DATA_ARGS_50097 {
  uint64_t xuid;
  uint64_t machine_id;  // XNetLogonGetMachineID
  uint8_t unkn1;
  uint8_t unkn2;
  uint16_t unkn3;
  uint16_t unkn4;
  uint32_t unkn5;
  uint32_t unkn6;
  uint32_t unkn7;
  uint32_t unkn8;
  uint16_t unkn9;
  uint32_t unkn10;
  uint32_t unkn11;
};

#pragma pack(pop)

struct Internal_Marshalled_Data {
  uint8_t unkn1_data[22];
  xe::be<uint32_t> start_args_ptr;  // CArgumentList*
  uint8_t unkn2_data[14];
  xe::be<uint32_t> results_ptr;  // STRUCT*
  xe::be<uint32_t> results_size;
};

struct Generic_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> unkn2_ptr;
  uint8_t unkn3_data[12];
  xe::be<uint32_t> unkn3_ptr;
  uint8_t unkn4_data[12];
  xe::be<uint32_t> unkn4_ptr;
};

struct XStorageDelete_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> serialized_server_path_ptr;  // Entry 1
};

struct XStringVerify_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> locale_size_ptr;
  uint8_t unkn3_data[12];
  xe::be<uint32_t> num_strings_ptr;
  uint8_t unkn4_data[12];
  xe::be<uint32_t> last_entry_ptr;
};

struct XStorageDownloadToMemory_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> serialized_server_path_ptr;  // Entry 1
  uint8_t unkn3_data[12];
  xe::be<uint32_t> serialized_buffer_ptr;  // Entry 2
};

struct XStorageUploadFromMemory_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> serialized_server_path_ptr;  // Entry 1
  uint8_t unkn3_data[12];
  xe::be<uint32_t> serialized_buffer_ptr;  // Entry 2
};

struct XStorageEnumerate_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> serialized_server_path_ptr;  // Entry 1
  xe::be<uint32_t> locale_size_ptr;
  uint8_t unkn3_data[12];
  xe::be<uint32_t> num_strings_ptr;
  uint8_t unkn4_data[12];
  xe::be<uint32_t> last_entry_ptr;
};

struct XUserFindUsers_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> empty;  // Entry 1
  uint8_t unkn3_data[12];
  xe::be<uint32_t> serialized_users_info_ptr;  // Entry 2
};

struct XAccountGetUserInfo_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
};

struct XOnlineQuerySearch_Marshalled_Data {
  xe::be<uint32_t> internal_data_ptr;
  uint8_t unkn1_data[44];
  xe::be<uint32_t> unkn1_ptr;
  uint8_t unkn2_data[24];
  xe::be<uint32_t> serialized_num_result_specs_ptr;  // Entry 1
  uint8_t unkn3_data[24];
  xe::be<uint32_t> serialized_attribute_specs_ptr;  // Entry 2
};

struct XOnlineQuerySearch_Args {
  uint32_t title_id;
  uint32_t dataset_id;
  uint32_t proc_index;
  uint32_t page;
  uint32_t results_pre_page;
  uint32_t num_result_specs;
  uint32_t attribute_specs_address;
  uint32_t num_attributes;
  uint32_t attributes_address;
};

struct X_DATA_5800A {
  xe::be<uint32_t> unkn1;
  xe::be<uint32_t> unkn2;
  xe::be<uint32_t> unkn3;
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