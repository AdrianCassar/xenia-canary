/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/upnp.h"

#define RAPIDJSON_HAS_STDSTRING 1

#include <third_party/libcurl/include/curl/curl.h>
#include <third_party/rapidjson/include/rapidjson/document.h>
#include <third_party/rapidjson/include/rapidjson/prettywriter.h>
#include <third_party/rapidjson/include/rapidjson/stringbuffer.h>

namespace xe {
namespace kernel {

class XLiveAPI {
 public:
  struct memory {
    char* response{};
    size_t size = 0;
    uint64_t http_code;
  };

  struct Player {
    std::string xuid;
    // xe::be<uint64_t> xuid;
    std::string hostAddress;
    xe::be<uint64_t> machineId;
    uint16_t port;
    xe::be<uint64_t> macAddress;  // 6 Bytes
    xe::be<uint64_t> sessionId;
  };

  struct SessionJSON {
    std::string sessionid;
    xe::be<uint16_t> port;
    xe::be<uint32_t> flags;
    std::string hostAddress;
    std::string macAddress;
    xe::be<uint32_t> publicSlotsCount;
    xe::be<uint32_t> privateSlotsCount;
    xe::be<uint32_t> openPublicSlotsCount;
    xe::be<uint32_t> openPrivateSlotsCount;
    xe::be<uint32_t> filledPublicSlotsCount;
    xe::be<uint32_t> filledPrivateSlotsCount;
    std::vector<Player> players;
  };

  struct XSessionArbitrationJSON {
    xe::be<uint32_t> totalPlayers;
    std::vector<std::vector<Player>> machines;
  };

#pragma region XSession Structs
  struct XNKID {
    uint8_t ab[8];
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

  struct XSESSION_INFO {
    XNKID sessionID;
    XNADDR hostAddress;
    XNKEY keyExchangeKey;
  };

  struct XSessionModify {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> flags;
    xe::be<uint32_t> maxPublicSlots;
    xe::be<uint32_t> maxPrivateSlots;
    xe::be<uint32_t> xoverlapped;
  };

  struct XSessionSearchEx {
    xe::be<uint32_t> proc_index;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> num_results;
    // xe::be<uint32_t> num_users; will break struct
    xe::be<uint16_t> num_props;
    xe::be<uint16_t> num_ctx;
    xe::be<uint32_t> props_ptr;
    xe::be<uint32_t> ctx_ptr;
    xe::be<uint32_t> results_buffer;
    xe::be<uint32_t> search_results;
    xe::be<uint32_t> xoverlapped;
  };

  struct XSessionSearch {
    xe::be<uint32_t> proc_index;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> num_results;
    xe::be<uint16_t> num_props;
    xe::be<uint16_t> num_ctx;
    xe::be<uint32_t> props_ptr;
    xe::be<uint32_t> ctx_ptr;
    xe::be<uint32_t> results_buffer;
    xe::be<uint32_t> search_results;
    xe::be<uint32_t> xoverlapped;
  };

  struct XSessionDetails {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> details_buffer_size;
    xe::be<uint32_t> details_buffer;
    xe::be<uint32_t> pXOverlapped;
  };

  struct XSessionMigate {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> session_info_ptr;
    xe::be<uint32_t> pXOverlapped;
  };

  struct XSessionArbitrationData {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> flags;
    xe::be<uint32_t> unk1;
    xe::be<uint32_t> unk2;
    xe::be<uint32_t> session_nonce;
    xe::be<uint32_t> results_buffer_size;
    xe::be<uint32_t> results;
    xe::be<uint32_t> pXOverlapped;
  };

  struct XSesion {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> flags;
    xe::be<uint32_t> num_slots_public;
    xe::be<uint32_t> num_slots_private;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> session_info_ptr;
    xe::be<uint32_t> nonce_ptr;
  };

  struct XSessionWriteStats {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> unk1;
    xe::be<uint64_t> xuid;
    xe::be<uint32_t> number_of_leaderboards;
    xe::be<uint32_t> leaderboards_guest_address;
    xe::be<uint32_t> xoverlapped;
  };

  struct XSessionViewProperties {
    xe::be<uint32_t> leaderboard_id;
    xe::be<uint32_t> properties_count;
    xe::be<uint32_t> properties_guest_address;
  };

  struct XSessionJoin {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> array_count;
    xe::be<uint32_t> xuid_array_ptr;
    xe::be<uint32_t> indices_array_ptr;
    xe::be<uint32_t> private_slots_array_ptr;
  };

  struct XSessionLeave {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> array_count;
    xe::be<uint32_t> xuid_array_ptr;
    xe::be<uint32_t> indices_array_ptr;
    xe::be<uint32_t> unused;
  };

  struct XONLINE_SERVICE_INFO {
    xe::be<uint32_t> id;
    in_addr ip;
    xe::be<uint16_t> port;
    xe::be<uint16_t> reserved;
  };
  static_assert_size(XONLINE_SERVICE_INFO, 12);

  struct XUSER_DATA {
    uint8_t type;

    union {
      xe::be<uint32_t> dword_data;  // XUSER_DATA_TYPE_INT32
      xe::be<uint64_t> qword_data;  // XUSER_DATA_TYPE_INT64
      xe::be<double> double_data;   // XUSER_DATA_TYPE_DOUBLE
      struct                        // XUSER_DATA_TYPE_UNICODE
      {
        xe::be<uint32_t> string_length;
        xe::be<uint32_t> string_ptr;
      } string;
      xe::be<float> float_data;
      struct {
        xe::be<uint32_t> data_length;
        xe::be<uint32_t> data_ptr;
      } binary;
      FILETIME filetime_data;
    };
  };

  struct XUSER_PROPERTY {
    xe::be<uint32_t> property_id;
    XUSER_DATA value;
  };

  struct XTitleServer {
    in_addr server_address;
    uint32_t flags;
    char server_description[200];
  };
  static_assert_size(XTitleServer, 208);

#pragma endregion

  //~XLiveAPI() {
  //  // upnp_handler.~upnp();
  //}

  static bool is_active();

  static bool is_initialized();
  
  static std::string GetApiAddress();

  static uint32_t GetNatType();

  static bool IsOnline();

  static uint16_t GetPlayerPort();

  static int8_t GetVersionStatus();

  static void Init();

  static void clearXnaddrCache();

  static sockaddr_in Getwhoami();

  static sockaddr_in GetLocalIP();

  static const std::string ip_to_string(in_addr addr);

  static const std::string ip_to_string(sockaddr_in sockaddr);

  static void DownloadPortMappings();

  static xe::be<uint64_t> MacAddresstoUint64(const unsigned char* macAddress);

  static void Uint64toSessionId(xe::be<uint64_t> sessionID,
                                unsigned char* sessionIdOut);

  static void Uint64toMacAddress(xe::be<uint64_t> macAddress,
                                 unsigned char* macAddressOut);

  static uint64_t GetMachineId();

  static XLiveAPI::memory RegisterPlayer();

  static uint64_t hex_to_uint64(const char* hex);

  static Player FindPlayers();

  static void QoSPost(xe::be<uint64_t> sessionId, char* qosData,
                      size_t qosLength);

  static memory QoSGet(xe::be<uint64_t> sessionId);

  static void SessionModify(xe::be<uint64_t> sessionId, XSessionModify* data);

  static const std::vector<SessionJSON> SessionSearchEx(
      XSessionSearchEx* data);

  static const std::vector<SessionJSON> SessionSearch(
      XSessionSearch* data);

  static const SessionJSON SessionDetails(xe::be<uint64_t> sessionId);

  static SessionJSON XSessionMigration(xe::be<uint64_t> sessionId);

  static char* XSessionArbitration(xe::be<uint64_t> sessionId);

  static void SessionWriteStats(xe::be<uint64_t> sessionId,
                                          XSessionWriteStats* stats,
                                          XSessionViewProperties* probs);

  static memory LeaderboardsFind(const char* data);

  static void DeleteSession(xe::be<uint64_t> sessionId);

  static void DeleteAllSessionsByMac();

  static void DeleteAllSessions();

  static void XSessionCreate(xe::be<uint64_t> sessionId, XSesion* data);

  static SessionJSON XSessionGet(xe::be<uint64_t> sessionId);

  static std::vector<XTitleServer> GetServers();

  static XONLINE_SERVICE_INFO GetServiceInfoById(
      xe::be<uint32_t> serviceId);

  static void SessionJoinRemote(xe::be<uint64_t> sessionId,
                                const std::vector<std::string> xuids);

  static void SessionLeaveRemote(xe::be<uint64_t> sessionId,
                                 std::vector<std::string> xuids);

  static unsigned char* GenerateMacAddress();

  static unsigned char* GetMACaddress();

  static bool UpdateQoSCache(const xe::be<uint64_t> sessionId,
                             const std::vector<char> qos_payload,
                             const uint32_t payload_size);

  static const sockaddr_in LocalIP() { return local_ip_; };
  static const sockaddr_in OnlineIP() { return online_ip_; };

  static const std::string LocalIP_str() { return ip_to_string(local_ip_); };
  static const std::string OnlineIP_str() { return ip_to_string(online_ip_); };

  inline static upnp upnp_handler;

  inline static unsigned char* mac_address = new unsigned char[6];

  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> sessionHandleMap{};

  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> machineIdCache{};
  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> sessionIdCache{};
  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> macAddressCache{};
  inline static std::map<xe::be<uint64_t>, std::vector<char>>
      qos_payload_cache{};

  inline static int8_t version_status;

 private:
  inline static bool active_ = false;
  inline static bool initialized_ = false;

  // std::shared_mutex mutex_;

  static memory Get(std::string endpoint);

  static memory Post(std::string endpoint, const char* data,
                     size_t data_size = 0);

  static memory Delete(std::string endpoint);

  // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
  static size_t callback(void* data, size_t size, size_t nmemb, void* clientp) {
    size_t realsize = size * nmemb;
    struct memory* mem = (struct memory*)clientp;

    char* ptr = (char*)realloc(mem->response, mem->size + realsize + 1);
    if (ptr == NULL) return 0; /* out of memory! */

    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;

    return realsize;
  };

  inline static sockaddr_in online_ip_{};

  inline static sockaddr_in local_ip_{};
};
}  // namespace kernel
}  // namespace xe
