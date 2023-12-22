/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "util/shim_utils.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"

#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/xam/xam_net.h"

#ifdef WIN32
#include <IPTypes.h>
#include <iphlpapi.h>
#endif  // WIN32

DEFINE_string(api_address, "127.0.0.1:36000", "Xenia Master Server Address",
              "Live");

DEFINE_bool(logging, false, "Log Network Activity & Stats", "Live");

DEFINE_bool(log_mask_ips, true, "Do not include P2P IPs inside the log",
            "Live");

DEFINE_bool(offline_mode, false, "Offline Mode", "Live");

DECLARE_bool(upnp);

using namespace xe::string_util;
using namespace rapidjson;

// TODO:
// LeaderboardsFind
// XSessionArbitration
//
// libcurl + wolfssl + TLS Support
//
// JSON deserialization instead of structs
// XSession Object
// Fix ObDereferenceObject_entry and XamSessionRefObjByHandle_entry
// Asynchronous UPnP
// Use the overlapped task for asynchronous curl requests.
// Improve GetMACaddress()
// API endpoint lookup table
//
// How is systemlink state determined?
// Extract stat descriptions from XDBF.
// Profiles have offline and online XUIDs we only use online.

// https://patents.google.com/patent/US20060287099A1
namespace xe {
namespace kernel {

bool XLiveAPI::is_active() { return active_; }

bool XLiveAPI::is_initialized() { return initialized_; }

std::string XLiveAPI::GetApiAddress() {
  // Add forward slash if not already added
  if (cvars::api_address.back() != '/') {
    cvars::api_address = cvars::api_address + '/';
  }

  return cvars::api_address;
}

// If online NAT open, otherwise strict.
uint32_t XLiveAPI::GetNatType() { return IsOnline() ? 1 : 3; }

bool XLiveAPI::IsOnline() { return OnlineIP().sin_addr.s_addr != 0; }

uint16_t XLiveAPI::GetPlayerPort() { return 36000; }

int8_t XLiveAPI::GetVersionStatus() { return version_status; }

void XLiveAPI::Init() {
  // Only initialize once
  if (is_initialized()) {
    return;
  }

  if (cvars::logging) {
    curl_version_info_data* vinfo = curl_version_info(CURLVERSION_NOW);

    XELOGI("libcurl version {}.{}.{}\n", (vinfo->version_num >> 16) & 0xFF,
           (vinfo->version_num >> 8) & 0xFF, vinfo->version_num & 0xFF);

    if (vinfo->features & CURL_VERSION_SSL) {
      XELOGI("SSL support enabled");
    } else {
      assert_always();
      XELOGI("No SSL");
    }
  }

  if (cvars::offline_mode) {
    XELOGI("Offline mode enabled!");
    initialized_ = true;
    return;
  }

  GetLocalIP();
  mac_address = GetMACaddress();

  Getwhoami();

  if (!IsOnline()) {
    XELOGI("Cannot access API server.");

    // Automatically enable offline mode?
    // cvars::offline_mode = true;
    // XELOGI("Offline mode enabled!");

    initialized_ = true;

    return;
  }

  // Download ports mappings before initializing UPnP.
  DownloadPortMappings();

  if (cvars::upnp) {
    upnp_handler.upnp_init();
  }

  // Must get mac address and IP before registering.
  auto reg_result = RegisterPlayer();

  // If player already exists on server then no need to post it again?
  auto player = FindPlayers();

  if (reg_result.http_code == 201 && player.xuid != "") {
    active_ = true;
  }

  initialized_ = true;

  // Delete sessions on start-up.
  XLiveAPI::DeleteAllSessions();
}

void XLiveAPI::clearXnaddrCache() {
  sessionIdCache.clear();
  macAddressCache.clear();
}

// Request data from the server
XLiveAPI::memory XLiveAPI::Get(std::string endpoint) {
  memory chunk = {0};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (!curl_handle) {
    XELOGE("GET Failed!");
    return chunk;
  }

  std::string endpoint_API = fmt::format("{}{}", GetApiAddress(), endpoint);

  if (cvars::logging) {
    XELOGI("cURL: {}", endpoint_API);

    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl_handle, CURLOPT_STDERR, stderr);
  }

  curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");

  if (headers == NULL) {
    return chunk;
  }

  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint_API.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "GET");
  curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, callback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);

  result = curl_easy_perform(curl_handle);

  if (CURLE_OK != result) {
    XELOGE("GET Failed!");
    return chunk;
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (CURLE_OK == result && chunk.http_code == 200) {
    return chunk;
  }

  XELOGE("GET Failed!");
  return chunk;
}

// Send data to the server
XLiveAPI::memory XLiveAPI::Post(std::string endpoint, const char* data,
                                size_t data_size) {
  memory chunk = {0};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (!curl_handle) {
    XELOGE("POST Failed.");
    return chunk;
  }

  std::string endpoint_API = fmt::format("{}{}", GetApiAddress(), endpoint);

  if (cvars::logging) {
    XELOGI("cURL: {}", endpoint_API);

    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl_handle, CURLOPT_STDERR, stderr);
  }

  curl_slist* headers = NULL;

  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint_API.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, data);

  if (data_size > 0) {
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE,
                     (curl_off_t)data_size);
  } else {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "charset: utf-8");

    if (headers == NULL) {
      return chunk;
    }

    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  }

  // FindPlayers, QoS, SessionSearchEx
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, callback);

  result = curl_easy_perform(curl_handle);

  if (CURLE_OK != result) {
    XELOGE("POST Failed!");
    return chunk;
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (CURLE_OK == result && chunk.http_code == 201) {
    return chunk;
  }

  XELOGE("POST Failed!");
  return chunk;
}

// Delete data from the server
XLiveAPI::memory XLiveAPI::Delete(std::string endpoint) {
  memory chunk = {0};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (!curl_handle) {
    XELOGE("DELETE Failed!");
    return chunk;
  }

  std::string endpoint_API = fmt::format("{}{}", GetApiAddress(), endpoint);

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");

  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint_API.c_str());

  curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "xenia");

  result = curl_easy_perform(curl_handle);

  if (CURLE_OK != result) {
    XELOGE("DELETE Failed!");
    return chunk;
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (CURLE_OK == result && chunk.http_code == 200) {
    return chunk;
  }

  XELOGE("DELETE Failed!");
  return chunk;
}

// Check connection to xenia web server as well as internet.
sockaddr_in XLiveAPI::Getwhoami() {
  memory chunk = Get("whoami");

  if (chunk.http_code != 200) {
    return online_ip_;
  }

  Document doc;
  doc.Parse(chunk.response);

  auto result =
      inet_pton(AF_INET, doc["address"].GetString(), &(online_ip_.sin_addr));

  XELOGI("Requesting Public IP");

  return online_ip_;
}

sockaddr_in XLiveAPI::GetLocalIP() {
  char local_ip_str[INET_ADDRSTRLEN]{};

  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (sock < 0) {
    return local_ip_;
  }

  sockaddr_in addrin{};
  addrin.sin_family = AF_INET;
  addrin.sin_port = htons(50);

  inet_pton(AF_INET, "8.8.8.8", &addrin.sin_addr);

  if (connect(sock, (sockaddr*)&addrin, sizeof(addrin)) < 0) {
    closesocket(sock);
    return local_ip_;
  }

  int socklen = sizeof(addrin);
  if (getsockname(sock, (sockaddr*)&addrin, &socklen) < 0) {
    return local_ip_;
  }

  local_ip_ = addrin;

  return addrin;
}

const std::string XLiveAPI::ip_to_string(in_addr addr) {
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr.s_addr, ip_str, INET_ADDRSTRLEN);

  return ip_str;
}

const std::string XLiveAPI::ip_to_string(sockaddr_in sockaddr) {
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &sockaddr.sin_addr, ip_str, INET_ADDRSTRLEN);

  return ip_str;
}

void XLiveAPI::DownloadPortMappings() {
  std::string endpoint =
      fmt::format("title/{:08X}/ports", kernel_state()->title_id());

  memory chunk = Get(endpoint);

  if (chunk.http_code != 200) {
    assert_always();
    return;
  }

  Document doc;
  doc.Parse(chunk.response);

  if (doc.HasMember("connect")) {
    for (const auto& port : doc["connect"].GetArray()) {
      auto& mapped = (*upnp_handler.mapped_connect_ports());
      mapped[port["port"].GetInt()] = port["mappedTo"].GetInt();
    }
  }

  if (doc.HasMember("bind")) {
    for (const auto& port : doc["bind"].GetArray()) {
      auto& mapped = (*upnp_handler.mapped_bind_ports());
      mapped[port["port"].GetInt()] = port["mappedTo"].GetInt();
    }
  }

  XELOGI("Requested Port Mappings");
  return;
}

xe::be<uint64_t> XLiveAPI::MacAddresstoUint64(const unsigned char* macAddress) {
  xe::be<uint64_t> macAddress64 = 0;

  for (int i = 5; i >= 0; --i) {
    macAddress64 = macAddress64 << 8;
    macAddress64 |= (uint64_t)macAddress[5 - i];
  }

  return macAddress64;
}

void XLiveAPI::Uint64toSessionId(xe::be<uint64_t> sessionID,
                                 unsigned char* sessionIdOut) {
  for (int i = 0; i < 8; i++) {
    sessionIdOut[7 - i] = ((sessionID >> (8 * i)) & 0xFF);
  }
}

void XLiveAPI::Uint64toMacAddress(xe::be<uint64_t> macAddress,
                                  unsigned char* macAddressOut) {
  for (int i = 0; i < 6; i++) {
    macAddressOut[5 - i] = ((macAddress >> (8 * i)) & 0xFF);
  }
}

uint64_t XLiveAPI::GetMachineId() {
  auto macAddress = mac_address;

  uint64_t machineId = 0;
  for (int i = 5; i >= 0; --i) {
    machineId = machineId << 8;
    machineId |= macAddress[5 - i];
  }

  machineId += 0xFA00000000000000;

  return machineId;
}

// Add player to web server
// A random mac address is changed every time a player is registered!
// xuid + ip + mac = unique player on a network
XLiveAPI::memory XLiveAPI::RegisterPlayer() {
  assert_not_null(mac_address);

  memory chunk{};

  if (!mac_address) {
    XELOGE("Cancelled Registering Player");
    return chunk;
  }

  Document doc;
  doc.SetObject();

  std::string mac_address_str =
      fmt::format("{:012x}", MacAddresstoUint64(mac_address).get());

  std::string machineId_str = fmt::format("{:06x}", GetMachineId());

  const uint32_t index = 0;

  if (!kernel_state()->xam_state()->IsUserSignedIn(index)) {
    return chunk;
  }

  // User index hard-coded
  uint64_t xuid_val = kernel_state()->xam_state()->GetUserProfile(index)->xuid();
  std::string xuid = to_hex_string(xuid_val);

  doc.AddMember("xuid", xuid, doc.GetAllocator());
  doc.AddMember("machineId", machineId_str, doc.GetAllocator());
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", mac_address_str, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  chunk = Post("players", buffer.GetString());

  if (chunk.http_code != 201) {
    assert_always();
    return chunk;
  }

  XELOGI("POST Success");

  return chunk;
}

uint64_t XLiveAPI::hex_to_uint64(const char* hex) {
  uint64_t result = strtoull(hex, NULL, 16);

  if (result == 0 || result == ULLONG_MAX) {
    // Failed to convert
    return 0;
  }

  return result;
}

// Request clients player info via IP address
// This should only be called once on startup no need to request our information
// more than once.
XLiveAPI::Player XLiveAPI::FindPlayers() {
  Player data{};

  Document doc;
  doc.SetObject();
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  // POST & receive.
  memory chunk = Post("players/find", buffer.GetString());

  if (chunk.http_code != 201) {
    XELOGE("FindPlayers POST Failed!");

    assert_always();
    return data;
  }

  doc.Swap(doc.Parse(chunk.response));

  data.xuid = doc["xuid"].GetString();
  data.hostAddress = doc["hostAddress"].GetString();
  data.port = doc["port"].GetUint();

  unsigned char macAddress[6];
  strcpy((char*)macAddress, (const char*)doc["macAddress"].GetString());
  data.macAddress = MacAddresstoUint64(macAddress);

  data.sessionId = hex_to_uint64(doc["sessionId"].GetString());
  data.machineId = hex_to_uint64(doc["machineId"].GetString());

  XELOGI("Requesting player details.");

  return data;
}

bool XLiveAPI::UpdateQoSCache(const xe::be<uint64_t> sessionId,
                              const std::vector<char> qos_payload,
                              const uint32_t payload_size) {
  if (qos_payload_cache[sessionId] != qos_payload) {
    qos_payload_cache[sessionId] = qos_payload;

    XELOGI("Updated QoS Cache.");
    return true;
  }

  return false;
}

// Send QoS binary data to the server
void XLiveAPI::QoSPost(xe::be<uint64_t> sessionId, char* qosData,
                       size_t qosLength) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/qos",
                  kernel_state()->title_id(), sessionId.get());

  memory chunk = Post(endpoint, qosData, qosLength);

  if (chunk.http_code != 201) {
    assert_always();
    return;
  }

  XELOGI("Sent QoS data.");
}

// Get QoS binary data from the server
XLiveAPI::memory XLiveAPI::QoSGet(xe::be<uint64_t> sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/qos",
                  kernel_state()->title_id(), sessionId.get());

  memory chunk = Get(endpoint);

  if (chunk.http_code != 200) {
    XELOGE("QoSGet GET Failed!");

    assert_always();
    return chunk;
  }

  XELOGI("Requesting QoS data.");

  return chunk;
}

void XLiveAPI::SessionModify(xe::be<uint64_t> sessionId, XSessionModify* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/modify",
                  kernel_state()->title_id(), sessionId.get());

  Document doc;
  doc.SetObject();

  doc.AddMember("flags", data->flags, doc.GetAllocator());
  doc.AddMember("publicSlotsCount", data->maxPublicSlots, doc.GetAllocator());

  // L4D1 modifies to large int?
  doc.AddMember("privateSlotsCount", data->maxPrivateSlots, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  if (chunk.http_code != 201) {
    XELOGE("Modify Post Failed!");
    assert_always();
    return;
  }

  XELOGI("Send Modify data.");
}

const std::vector<XLiveAPI::SessionJSON> XLiveAPI::SessionSearchEx(
    XSessionSearchEx* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/search", kernel_state()->title_id());

  Document doc;
  doc.SetObject();

  doc.AddMember("searchIndex", data->proc_index, doc.GetAllocator());
  doc.AddMember("resultsCount", data->num_results, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  std::vector<SessionJSON> sessions{};

  if (chunk.http_code != 201) {
    XELOGE("SessionSearchEx POST Failed!");
    assert_always();

    return sessions;
  }

  doc.Swap(doc.Parse(chunk.response));

  const Value& sessionsJsonArray = doc.GetArray();

  unsigned int i = 0;

  for (Value::ConstValueIterator object_ptr = sessionsJsonArray.Begin();
       object_ptr != sessionsJsonArray.End(); ++object_ptr) {
    SessionJSON session{};

    std::vector<uint8_t> session_id{};
    hex_string_to_array(session_id, (*object_ptr)["id"].GetString());
    session.sessionid = std::string(session_id.begin(), session_id.end());

    session.port = (*object_ptr)["port"].GetInt();

    session.openPublicSlotsCount =
        (*object_ptr)["openPublicSlotsCount"].GetInt();
    session.openPrivateSlotsCount =
        (*object_ptr)["openPrivateSlotsCount"].GetInt();

    session.filledPublicSlotsCount =
        (*object_ptr)["filledPublicSlotsCount"].GetInt();
    session.filledPrivateSlotsCount =
        (*object_ptr)["filledPrivateSlotsCount"].GetInt();

    session.hostAddress = (*object_ptr)["hostAddress"].GetString();

    std::vector<uint8_t> mac{};
    hex_string_to_array(mac, (*object_ptr)["macAddress"].GetString());
    session.macAddress = std::string(mac.begin(), mac.end());

    session.publicSlotsCount = (*object_ptr)["publicSlotsCount"].GetInt();
    session.privateSlotsCount = (*object_ptr)["privateSlotsCount"].GetInt();
    session.flags = (*object_ptr)["flags"].GetInt();

    sessions.push_back(session);
  }

  XELOGI("SessionSearchEx found {} sessions.", sessions.size());

  return sessions;
}

const std::vector<XLiveAPI::SessionJSON> XLiveAPI::SessionSearch(
    XSessionSearch* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/search", kernel_state()->title_id());

  Document doc;
  doc.SetObject();

  doc.AddMember("searchIndex", data->proc_index, doc.GetAllocator());
  doc.AddMember("resultsCount", data->num_results, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  std::vector<SessionJSON> sessions{};

  if (chunk.http_code != 201) {
    XELOGE("SessionSearch POST Failed!");
    assert_always();

    return sessions;
  }

  doc.Swap(doc.Parse(chunk.response));

  const Value& sessionsJsonArray = doc.GetArray();

  unsigned int i = 0;

  for (Value::ConstValueIterator object_ptr = sessionsJsonArray.Begin();
       object_ptr != sessionsJsonArray.End(); ++object_ptr) {
    SessionJSON session{};

    std::vector<uint8_t> session_id{};
    hex_string_to_array(session_id, (*object_ptr)["id"].GetString());
    session.sessionid = std::string(session_id.begin(), session_id.end());

    session.port = (*object_ptr)["port"].GetInt();

    session.openPublicSlotsCount =
        (*object_ptr)["openPublicSlotsCount"].GetInt();
    session.openPrivateSlotsCount =
        (*object_ptr)["openPrivateSlotsCount"].GetInt();

    session.filledPublicSlotsCount =
        (*object_ptr)["filledPublicSlotsCount"].GetInt();
    session.filledPrivateSlotsCount =
        (*object_ptr)["filledPrivateSlotsCount"].GetInt();

    session.hostAddress = (*object_ptr)["hostAddress"].GetString();

    std::vector<uint8_t> mac{};
    hex_string_to_array(mac, (*object_ptr)["macAddress"].GetString());
    session.macAddress = std::string(mac.begin(), mac.end());

    session.publicSlotsCount = (*object_ptr)["publicSlotsCount"].GetInt();
    session.privateSlotsCount = (*object_ptr)["privateSlotsCount"].GetInt();
    session.flags = (*object_ptr)["flags"].GetInt();

    sessions.push_back(session);
  }

  XELOGI("SessionSearch found {} sessions.", sessions.size());

  return sessions;
}

const XLiveAPI::SessionJSON XLiveAPI::SessionDetails(
    xe::be<uint64_t> sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/details",
                  kernel_state()->title_id(), sessionId.get());

  memory chunk = Get(endpoint);

  SessionJSON session{};

  if (chunk.http_code != 200) {
    XELOGE("SessionDetails error code {}", chunk.http_code);
    XELOGE("SessionDetails not found e.g. Invalid sessionId");

    assert_always();
    return session;
  }

  Document doc;
  doc.Parse(chunk.response);

  session.sessionid = doc["id"].GetString();
  session.port = doc["port"].GetInt();

  session.openPublicSlotsCount = doc["openPublicSlotsCount"].GetInt();
  session.openPrivateSlotsCount = doc["openPrivateSlotsCount"].GetInt();

  session.filledPublicSlotsCount = doc["filledPublicSlotsCount"].GetInt();
  session.filledPrivateSlotsCount = doc["filledPrivateSlotsCount"].GetInt();

  session.hostAddress = doc["hostAddress"].GetString();
  session.macAddress = doc["macAddress"].GetString();

  session.publicSlotsCount = doc["publicSlotsCount"].GetInt();
  session.privateSlotsCount = doc["privateSlotsCount"].GetInt();
  session.flags = doc["flags"].GetInt();

  const Value& playersArray = doc["players"].GetArray();

  for (Value::ConstValueIterator object_ptr = playersArray.Begin();
       object_ptr != playersArray.End(); ++object_ptr) {
    Player Player{};

    Player.xuid = (*object_ptr)["xuid"].GetString();

    session.players.push_back(Player);
  }

  XELOGI("Requesting Session Details.");

  return session;
}

XLiveAPI::SessionJSON XLiveAPI::XSessionMigration(xe::be<uint64_t> sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/migrate",
                  kernel_state()->title_id(), sessionId.get());

  Document doc;
  doc.SetObject();

  std::string mac_address_str = fmt::format(
      "{:012x}", static_cast<uint64_t>(MacAddresstoUint64(mac_address)));

  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", mac_address_str, doc.GetAllocator());
  doc.AddMember("port", GetPlayerPort(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  SessionJSON session{};

  if (chunk.http_code != 201) {
    XELOGE("XSessionMigration POST Failed!");
    assert_always();

    if (chunk.http_code == 404) {
      std::string session_id =
          fmt::format("{:016x}", kernel_state()->title_id(), sessionId.get());

      XELOGE("Cannot migrate session {} not found.", session_id);
    }

    // change return type to XLiveAPI::memory?
    return session;
  }

  doc.Swap(doc.Parse(chunk.response));

  session.sessionid = doc["id"].GetString();
  session.hostAddress = doc["hostAddress"].GetString();
  session.macAddress = doc["macAddress"].GetString();
  session.port = GetPlayerPort();

  XELOGI("Send XSessionMigration data.");

  return session;
}

char* XLiveAPI::XSessionArbitration(xe::be<uint64_t> sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/arbitration",
                  kernel_state()->title_id(), sessionId.get());

  memory chunk = Get(endpoint);

  if (chunk.http_code != 200) {
    XELOGE("XSessionMigration GET Failed!");
    assert_always();

    return chunk.response;
  }

  // Document doc;
  // doc.Parse(chunk.response);

  // struct ArbitrationInfo {
  //   std::vector<int> machines;
  //   uint32_t totalPlayers;
  // };

  return chunk.response;
}

void XLiveAPI::SessionWriteStats(xe::be<uint64_t> sessionId,
                                 XSessionWriteStats* stats,
                                 XSessionViewProperties* leaderboard) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/leaderboards",
                  kernel_state()->title_id(), sessionId.get());

  Document rootObject;
  rootObject.SetObject();
  Value leaderboardsObject(kObjectType);

  std::string xuid = fmt::format("{:016x}", static_cast<uint64_t>(stats->xuid));

  for (uint32_t leaderboardIndex = 0;
       leaderboardIndex < stats->number_of_leaderboards; leaderboardIndex++) {
    // Move to implementation?
    auto statistics =
        kernel_state()->memory()->TranslateVirtual<XUSER_PROPERTY*>(
            leaderboard[leaderboardIndex].properties_guest_address);

    Value leaderboardObject(kObjectType);
    Value statsObject(kObjectType);

    for (uint32_t statisticIndex = 0;
         statisticIndex < leaderboard[leaderboardIndex].properties_count;
         statisticIndex++) {
      Value statObject(kObjectType);

      statObject.AddMember("type", statistics[statisticIndex].value.type,
                           rootObject.GetAllocator());

      switch (statistics[statisticIndex].value.type) {
        case 1:
          statObject.AddMember("value",
                               statistics[statisticIndex].value.dword_data,
                               rootObject.GetAllocator());
          break;
        case 2:
          statObject.AddMember("value",
                               statistics[statisticIndex].value.qword_data,
                               rootObject.GetAllocator());
          break;
        default:
          XELOGW("Unimplemented statistic type for write",
                 statistics[statisticIndex].value.type);
          break;
      }

      std::string propertyId = fmt::format(
          "{:08X}",
          static_cast<uint32_t>(statistics[statisticIndex].property_id));

      Value statisticIdKey(propertyId, rootObject.GetAllocator());
      statsObject.AddMember(statisticIdKey, statObject,
                            rootObject.GetAllocator());
    }

    leaderboardObject.AddMember("stats", statsObject,
                                rootObject.GetAllocator());
    Value leaderboardIdKey(
        std::to_string(leaderboard[leaderboardIndex].leaderboard_id).c_str(),
        rootObject.GetAllocator());
    leaderboardsObject.AddMember(leaderboardIdKey, leaderboardObject,
                                 rootObject.GetAllocator());
  }

  rootObject.AddMember("leaderboards", leaderboardsObject,
                       rootObject.GetAllocator());
  rootObject.AddMember("xuid", xuid, rootObject.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  rootObject.Accept(writer);

  if (cvars::logging) {
    XELOGI("SessionWriteStats:\n\n{}", buffer.GetString());
  }

  memory chunk = Post(endpoint, buffer.GetString());

  if (chunk.http_code != 201) {
    XELOGE("SessionWriteStats POST Failed!");
    // assert_always();

    return;
  }
}

XLiveAPI::memory XLiveAPI::LeaderboardsFind(const char* data) {
  std::string endpoint = fmt::format("leaderboards/find");

  memory chunk = Post(endpoint, data);

  if (chunk.http_code != 201) {
    XELOGE("LeaderboardsFind POST Failed!");
    assert_always();
  }

  return chunk;
}

void XLiveAPI::DeleteSession(xe::be<uint64_t> sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}", kernel_state()->title_id(),
                  sessionId.get());

  memory chunk = Delete(endpoint);

  if (chunk.http_code != 200) {
    XELOGI("Failed to delete session {:08X}", sessionId.get());
    // assert_always();
  }

  clearXnaddrCache();
  qos_payload_cache.erase(sessionId);
}

void XLiveAPI::DeleteAllSessions() {
  if (!is_active()) return;

  memory chunk = Delete("DeleteSessions");

  if (chunk.http_code != 200) {
    XELOGI("Failed to delete all sessions");
  }
}

void XLiveAPI::XSessionCreate(xe::be<uint64_t> sessionId, XSesion* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions", kernel_state()->title_id());

  Document doc;
  doc.SetObject();

  std::string sessionId_str = fmt::format("{:016x}", sessionId.get());

  std::string mac_address_str = fmt::format(
      "{:012x}", static_cast<uint64_t>(MacAddresstoUint64(mac_address)));

  assert_true(mac_address_str.size() == 12);
  assert_true(sessionId_str.size() == 16);

  doc.AddMember("sessionId", sessionId_str, doc.GetAllocator());
  doc.AddMember("flags", data->flags, doc.GetAllocator());
  doc.AddMember("publicSlotsCount", data->num_slots_public, doc.GetAllocator());
  doc.AddMember("privateSlotsCount", data->num_slots_private,
                doc.GetAllocator());
  doc.AddMember("userIndex", data->user_index, doc.GetAllocator());
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", mac_address_str, doc.GetAllocator());
  doc.AddMember("port", GetPlayerPort(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  if (chunk.http_code != 201) {
    XELOGI("XSessionCreate POST Failed!");
    assert_always();
    return;
  }

  XELOGI("XSessionCreate POST Success");
}

XLiveAPI::SessionJSON XLiveAPI::XSessionGet(xe::be<uint64_t> sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}", kernel_state()->title_id(),
                  sessionId.get());

  SessionJSON session = SessionJSON{};

  memory chunk = Get(endpoint);

  if (chunk.http_code != 200) {
    XELOGE("XSessionGet error code: {}", chunk.http_code);
    assert_always();

    return session;
  }

  Document doc;
  doc.Parse(chunk.response);

  session.hostAddress = doc["hostAddress"].GetString();

  std::vector<uint8_t> mac{};
  hex_string_to_array(mac, doc["macAddress"].GetString());
  session.macAddress = std::string(mac.begin(), mac.end());

  session.port = GetPlayerPort();

  return session;
}

std::vector<XLiveAPI::XTitleServer> XLiveAPI::GetServers() {
  std::string endpoint =
      fmt::format("title/{:08X}/servers", kernel_state()->title_id());

  memory chunk = Get(endpoint);

  std::vector<XTitleServer> servers{};

  if (chunk.http_code != 200) {
    XELOGE("GetServers error code: {}", chunk.http_code);
    assert_always();

    return servers;
  }

  Document doc;
  doc.Parse(chunk.response);

  for (const auto& server_data : doc.GetArray()) {
    XTitleServer server{};

    inet_pton(AF_INET, server_data["address"].GetString(),
              &server.server_address);

    server.flags = server_data["flags"].GetInt();

    std::string description = server_data["description"].GetString();

    if (description.size() < 200) {
      memcpy(server.server_description, description.c_str(),
             strlen(description.c_str()));
    }

    servers.push_back(server);
  }

  return servers;
}

XLiveAPI::XONLINE_SERVICE_INFO XLiveAPI::GetServiceInfoById(
    xe::be<uint32_t> serviceId) {
  std::string endpoint =
      fmt::format("title/{:08X}/services/{:08X}", kernel_state()->title_id(),
                  static_cast<uint32_t>(serviceId));

  memory chunk = Get(endpoint);

  XONLINE_SERVICE_INFO service{};

  if (chunk.http_code != 200) {
    XELOGE("GetServiceById error code: {}", chunk.http_code);
    assert_always();

    return service;
  }

  Document doc;
  doc.Parse(chunk.response);

  for (const auto& service_info : doc.GetArray()) {
    inet_pton(AF_INET, service_info["address"].GetString(), &service.ip);

    XELOGD("GetServiceById IP: {}", service_info["address"].GetString());

    service.port = service_info["port"].GetInt();
    service.id = serviceId;
  }

  return service;
}

void XLiveAPI::SessionJoinRemote(xe::be<uint64_t> sessionId,
                                 const std::vector<std::string> xuids) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/join",
                  kernel_state()->title_id(), sessionId.get());
  Document doc;
  doc.SetObject();

  Value xuidsJsonArray(kArrayType);

  for (const auto xuid : xuids) {
    Value value;
    value.SetString(xuid.c_str(), 16, doc.GetAllocator());

    xuidsJsonArray.PushBack(value, doc.GetAllocator());
  }

  doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  if (chunk.http_code != 201) {
    XELOGE("SessionJoinRemote error code: {}", chunk.http_code);
    assert_always();
  }
}

void XLiveAPI::SessionLeaveRemote(xe::be<uint64_t> sessionId,
                                  const std::vector<std::string> xuids) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/leave",
                  kernel_state()->title_id(), sessionId.get());

  Document doc;
  doc.SetObject();

  Value xuidsJsonArray(kArrayType);

  for (const auto xuid : xuids) {
    Value value;
    value.SetString(xuid.c_str(), 16, doc.GetAllocator());

    xuidsJsonArray.PushBack(value, doc.GetAllocator());
  }

  doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, buffer.GetString());

  if (chunk.http_code != 201) {
    XELOGE("SessionLeaveRemote error code: {}", chunk.http_code);
    assert_always();
  }
}

unsigned char* XLiveAPI::GenerateMacAddress() {
  unsigned char* mac_address = new unsigned char[6];
  xam::XNetRandom(mac_address, 6);

  return mac_address;
}

unsigned char* XLiveAPI::GetMACaddress() {
  XELOGI("Resolving system mac address.");

  // Use random mac for now.
  return GenerateMacAddress();

#ifdef WIN32
  DWORD dwRetval = 0;
  ULONG outBufLen = 0;

  std::unique_ptr<IP_ADAPTER_ADDRESSES[]> adapter_addresses;

  dwRetval = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL,
                                  NULL, &outBufLen);

  if (dwRetval == ERROR_BUFFER_OVERFLOW) {
    adapter_addresses = std::make_unique<IP_ADAPTER_ADDRESSES[]>(outBufLen);
  } else {
    return GenerateMacAddress();
  }

  dwRetval = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL,
                                  adapter_addresses.get(), &outBufLen);

  if (dwRetval) {
    return GenerateMacAddress();
  }

  for (IP_ADAPTER_ADDRESSES* adapter_ptr = adapter_addresses.get();
       adapter_ptr != NULL; adapter_ptr = adapter_ptr->Next) {
    if (adapter_ptr->OperStatus == IfOperStatusUp &&
        (adapter_ptr->IfType == IF_TYPE_IEEE80211 ||
         adapter_ptr->IfType == IF_TYPE_ETHERNET_CSMACD)) {
      if (adapter_ptr->PhysicalAddress != NULL) {
        char mac_address[MAX_ADAPTER_ADDRESS_LENGTH]{};
        memcpy(mac_address, adapter_ptr->PhysicalAddress,
               MAX_ADAPTER_ADDRESS_LENGTH);

        // Check U/L bit
        if (adapter_ptr->PhysicalAddress[0] & 2) {
          // Universal
          // XELOGI("Universal");
        } else {
          // Local
          // XELOGI("Local");
        }

        if (adapter_ptr->PhysicalAddressLength != NULL &&
            adapter_ptr->PhysicalAddressLength == 6) {
          unsigned char* mac_ptr =
              new unsigned char[MAX_ADAPTER_ADDRESS_LENGTH];

          for (int i = 0; i < 6; i++) {
            mac_ptr[i] =
                static_cast<unsigned char>(adapter_ptr->PhysicalAddress[i]);
          }

          return mac_ptr;
        }
      }
    }
  }

  XELOGI("Cannot find mac address generating random.");

  return GenerateMacAddress();
#else
  XELOGI("Generating random mac address.");

  return GenerateMacAddress();
#endif  // WIN32
}
}  // namespace kernel
}  // namespace xe