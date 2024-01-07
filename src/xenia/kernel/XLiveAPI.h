/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XLIVEAPI_H_
#define XENIA_KERNEL_XLIVEAPI_H_

#include "xenia/kernel/upnp.h"

#define RAPIDJSON_HAS_STDSTRING 1

#include <third_party/libcurl/include/curl/curl.h>
#include <third_party/rapidjson/include/rapidjson/document.h>
#include <third_party/rapidjson/include/rapidjson/prettywriter.h>
#include <third_party/rapidjson/include/rapidjson/stringbuffer.h>
#include "xenia/base/byte_order.h"
#include "xenia/kernel/util/net_utils.h"
#include "xenia/kernel/xnet.h"
#include "xenia/kernel/xsession.h"

namespace xe {
namespace kernel {

struct XONLINE_SERVICE_INFO {
  xe::be<uint32_t> id;
  in_addr ip;
  xe::be<uint16_t> port;
  xe::be<uint16_t> reserved;
};
static_assert_size(XONLINE_SERVICE_INFO, 12);

struct XTitleServer {
  in_addr server_address;
  uint32_t flags;
  char server_description[200];
};
static_assert_size(XTitleServer, 208);

class XLiveAPI {
 public:
  struct memory {
    char* response{};
    size_t size = 0;
    uint64_t http_code;
  };

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

  static void DownloadPortMappings();

  static const uint64_t GetMachineId();

  static memory RegisterPlayer();

  static Player FindPlayer(std::string ip);

  static void QoSPost(uint64_t sessionId, uint8_t* qosData, size_t qosLength);

  static memory QoSGet(uint64_t sessionId);

  static void SessionModify(uint64_t sessionId, XSessionModify* data);

  static const std::vector<SessionJSON> SessionSearchEx(XSessionSearchEx* data);

  static const std::vector<SessionJSON> SessionSearch(XSessionSearch* data);

  static void SessionContextSet(uint64_t session_id,
                                std::map<uint32_t, uint32_t> contexts);

  static const std::map<uint32_t, uint32_t> SessionContextGet(
      uint64_t session_id);

  static const SessionJSON SessionDetails(uint64_t sessionId);

  static SessionJSON XSessionMigration(uint64_t sessionId);

  static XSessionArbitrationJSON XSessionArbitration(uint64_t sessionId);

  static void SessionWriteStats(uint64_t sessionId, XSessionWriteStats* stats,
                                XSessionViewProperties* probs);

  static memory LeaderboardsFind(const uint8_t* data);

  static void DeleteSession(uint64_t sessionId);

  static void DeleteAllSessionsByMac();

  static void DeleteAllSessions();

  static void XSessionCreate(uint64_t sessionId, XSessionData* data);

  static SessionJSON XSessionGet(uint64_t sessionId);

  static std::vector<XTitleServer> GetServers();

  static XONLINE_SERVICE_INFO GetServiceInfoById(uint32_t serviceId);

  static void SessionJoinRemote(uint64_t sessionId,
                                const std::vector<std::string> xuids);

  static void SessionLeaveRemote(uint64_t sessionId,
                                 std::vector<std::string> xuids);

  static const uint8_t* GenerateMacAddress();

  static const uint8_t* GetMACaddress();

  static bool UpdateQoSCache(const uint64_t sessionId,
                             const std::vector<uint8_t> qos_payload,
                             const uint32_t payload_size);

  static const sockaddr_in LocalIP() { return local_ip_; };
  static const sockaddr_in OnlineIP() { return online_ip_; };

  static const std::string LocalIP_str() { return ip_to_string(local_ip_); };
  static const std::string OnlineIP_str() { return ip_to_string(online_ip_); };

  inline static upnp upnp_handler;

  inline static MacAddress* mac_address_ = nullptr;

  inline static std::map<uint32_t, uint64_t> machineIdCache{};
  inline static std::map<uint32_t, uint64_t> sessionIdCache{};
  inline static std::map<uint32_t, uint64_t> macAddressCache{};
  inline static std::map<uint64_t, std::vector<uint8_t>> qos_payload_cache{};

  inline static int8_t version_status;

 private:
  inline static bool active_ = false;
  inline static bool initialized_ = false;

  // std::shared_mutex mutex_;

  static memory Get(std::string endpoint);

  static memory Post(std::string endpoint, const uint8_t* data,
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

#endif  // XENIA_KERNEL_XLIVEAPI_H_