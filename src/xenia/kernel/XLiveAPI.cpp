/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/util/shim_utils.h"

#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/xam/xam_net.h"

#ifdef XE_PLATFORM_WIN32
#include <IPTypes.h>
#include <iphlpapi.h>
#endif  // XE_PLATFORM_WIN32

DEFINE_string(api_address, "127.0.0.1:36000", "Xenia Master Server Address",
              "Live");

DEFINE_bool(logging, false, "Log Network Activity & Stats", "Live");

DEFINE_bool(log_mask_ips, true, "Do not include P2P IPs inside the log",
            "Live");

DEFINE_bool(offline_mode, false, "Offline Mode", "Live");

DECLARE_bool(upnp);

using namespace rapidjson;

// TODO:
// LeaderboardsFind
//
// libcurl + wolfssl + TLS Support
//
// JSON deserialization instead of structs
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

uint64_t XLiveAPI::GetMachineId() {
  auto macAddress = mac_address_->to_uint64();
  auto macAddressUint = *reinterpret_cast<uint64_t*>(&macAddress);

  uint64_t machineId = 0xFA00000000000000;
  machineId |= macAddressUint;

  return machineId;
}

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

  GetLocalIP();

  mac_address_ = new MacAddress(GetMACaddress());
  XELOGI("MAC ADDRESS: {}", mac_address_->to_printable_form());

  if (cvars::offline_mode) {
    XELOGI("Offline mode enabled!");
    initialized_ = true;
    return;
  }

  Getwhoami();

  if (!IsOnline()) {
    XELOGI("Cannot access API server.");
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

  if (reg_result.http_code == HTTP_STATUS_CODE::HTTP_CREATED &&
      player.xuid != 0) {
    active_ = true;
  }

  initialized_ = true;

  // Delete sessions on start-up.
  DeleteAllSessions();
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

  if (CURLE_OK == result && chunk.http_code == HTTP_STATUS_CODE::HTTP_OK) {
    return chunk;
  }

  XELOGE("GET Failed!");
  return chunk;
}

// Send data to the server
XLiveAPI::memory XLiveAPI::Post(std::string endpoint, const uint8_t* data,
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

  if (CURLE_OK == result && chunk.http_code == HTTP_STATUS_CODE::HTTP_CREATED) {
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

  if (CURLE_OK == result && chunk.http_code == HTTP_STATUS_CODE::HTTP_OK) {
    return chunk;
  }

  XELOGE("DELETE Failed!");
  return chunk;
}

// Check connection to xenia web server as well as internet.
sockaddr_in XLiveAPI::Getwhoami() {
  memory chunk = Get("whoami");

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
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

void XLiveAPI::DownloadPortMappings() {
  std::string endpoint =
      fmt::format("title/{:08X}/ports", kernel_state()->title_id());

  memory chunk = Get(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
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

// Add player to web server
// A random mac address is changed every time a player is registered!
// xuid + ip + mac = unique player on a network
XLiveAPI::memory XLiveAPI::RegisterPlayer() {
  assert_not_null(mac_address_);

  memory chunk{};

  if (!mac_address_) {
    XELOGE("Cancelled Registering Player");
    return chunk;
  }

  Document doc;
  doc.SetObject();

  std::string machineId_str = fmt::format("{:06x}", GetMachineId());

  const uint32_t index = 0;

  if (!kernel_state()->xam_state()->IsUserSignedIn(index)) {
    return chunk;
  }

  // User index hard-coded
  uint64_t xuid_val = kernel_state()->xam_state()->GetUserProfile(index)->xuid();
  std::string xuid = string_util::to_hex_string(xuid_val);

  doc.AddMember("xuid", xuid, doc.GetAllocator());
  doc.AddMember("machineId", machineId_str, doc.GetAllocator());
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", mac_address_->to_string(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  chunk = Post("players", (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    assert_always();
    return chunk;
  }

  XELOGI("POST Success");

  return chunk;
}

// Request clients player info via IP address
// This should only be called once on startup no need to request our information
// more than once.
Player XLiveAPI::FindPlayers() {
  Player data{};

  Document doc;
  doc.SetObject();
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  // POST & receive.
  memory chunk = Post("players/find", (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("FindPlayers POST Failed!");

    assert_always();
    return data;
  }

  doc.Swap(doc.Parse(chunk.response));

  data.xuid = string_util::from_string<uint64_t>(doc["xuid"].GetString(), true);
  data.hostAddress = doc["hostAddress"].GetString();
  data.port = doc["port"].GetUint();

  MacAddress address = MacAddress(doc["macAddress"].GetString());
  data.macAddress = address.to_uint64();

  data.sessionId =
      string_util::from_string<uint64_t>(doc["sessionId"].GetString(), true);
  data.machineId =
      string_util::from_string<uint64_t>(doc["machineId"].GetString(), true);

  XELOGI("Requesting player details.");

  return data;
}

bool XLiveAPI::UpdateQoSCache(const uint64_t sessionId,
                              const std::vector<uint8_t> qos_payload,
                              const uint32_t payload_size) {
  if (qos_payload_cache[sessionId] != qos_payload) {
    qos_payload_cache[sessionId] = qos_payload;

    XELOGI("Updated QoS Cache.");
    return true;
  }

  return false;
}

// Send QoS binary data to the server
void XLiveAPI::QoSPost(uint64_t sessionId, uint8_t* qosData, size_t qosLength) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/qos",
                                     kernel_state()->title_id(), sessionId);

  memory chunk = Post(endpoint, qosData, qosLength);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    assert_always();
    return;
  }

  XELOGI("Sent QoS data.");
}

// Get QoS binary data from the server
XLiveAPI::memory XLiveAPI::QoSGet(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/qos",
                                     kernel_state()->title_id(), sessionId);

  memory chunk = Get(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("QoSGet GET Failed!");

    assert_always();
    return chunk;
  }

  XELOGI("Requesting QoS data.");

  return chunk;
}

void XLiveAPI::SessionModify(uint64_t sessionId, XSessionModify* data) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/modify",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  doc.AddMember("flags", data->flags, doc.GetAllocator());
  doc.AddMember("publicSlotsCount", data->maxPublicSlots, doc.GetAllocator());

  // L4D1 modifies to large int?
  doc.AddMember("privateSlotsCount", data->maxPrivateSlots, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("Modify Post Failed!");
    assert_always();
    return;
  }

  XELOGI("Send Modify data.");
}

const std::vector<SessionJSON> XLiveAPI::SessionSearchEx(
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

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  std::vector<SessionJSON> sessions{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionSearchEx POST Failed!");
    assert_always();

    return sessions;
  }

  doc.Swap(doc.Parse(chunk.response));

  const Value& sessionsJsonArray = doc.GetArray();

  for (Value::ConstValueIterator object_ptr = sessionsJsonArray.Begin();
       object_ptr != sessionsJsonArray.End(); ++object_ptr) {
    SessionJSON session{};

    session.sessionid = string_util::from_string<uint64_t>(
        (*object_ptr)["id"].GetString(), true);
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
    session.macAddress = (*object_ptr)["macAddress"].GetString();

    session.publicSlotsCount = (*object_ptr)["publicSlotsCount"].GetInt();
    session.privateSlotsCount = (*object_ptr)["privateSlotsCount"].GetInt();
    session.flags = (*object_ptr)["flags"].GetInt();

    sessions.push_back(session);
  }

  XELOGI("SessionSearchEx found {} sessions.", sessions.size());

  return sessions;
}

const std::vector<SessionJSON> XLiveAPI::SessionSearch(XSessionSearch* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/search", kernel_state()->title_id());

  Document doc;
  doc.SetObject();

  doc.AddMember("searchIndex", data->proc_index, doc.GetAllocator());
  doc.AddMember("resultsCount", data->num_results, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  std::vector<SessionJSON> sessions{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
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

    session.sessionid = string_util::from_string<uint64_t>(
        (*object_ptr)["id"].GetString(), true);
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
    session.macAddress = (*object_ptr)["macAddress"].GetString();

    session.publicSlotsCount = (*object_ptr)["publicSlotsCount"].GetInt();
    session.privateSlotsCount = (*object_ptr)["privateSlotsCount"].GetInt();
    session.flags = (*object_ptr)["flags"].GetInt();

    sessions.push_back(session);
  }

  XELOGI("SessionSearch found {} sessions.", sessions.size());

  return sessions;
}

const SessionJSON XLiveAPI::SessionDetails(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/details",
                                     kernel_state()->title_id(), sessionId);

  memory chunk = Get(endpoint);

  SessionJSON session{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("SessionDetails error code {}", chunk.http_code);
    XELOGE("SessionDetails not found e.g. Invalid sessionId");

    assert_always();
    return session;
  }

  Document doc;
  doc.Parse(chunk.response);

  session.sessionid =
      string_util::from_string<uint64_t>(doc["id"].GetString(), true);
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
    Player.xuid = string_util::from_string<uint64_t>(
        (*object_ptr)["xuid"].GetString(), true);

    session.players.push_back(Player);
  }

  XELOGI("Requesting Session Details.");

  return session;
}

SessionJSON XLiveAPI::XSessionMigration(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/migrate",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", mac_address_->to_string(), doc.GetAllocator());
  doc.AddMember("port", GetPlayerPort(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  SessionJSON session{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("XSessionMigration POST Failed!");
    assert_always();

    if (chunk.http_code == HTTP_STATUS_CODE::HTTP_NOT_FOUND) {
      std::string session_id =
          fmt::format("{:016x}", kernel_state()->title_id(), sessionId);

      XELOGE("Cannot migrate session {} not found.", session_id);
    }

    // change return type to XLiveAPI::memory?
    return session;
  }

  doc.Swap(doc.Parse(chunk.response));

  session.sessionid =
      string_util::from_string<uint64_t>(doc["id"].GetString(), true);
  session.hostAddress = doc["hostAddress"].GetString();
  session.macAddress = doc["macAddress"].GetString();
  session.port = GetPlayerPort();

  XELOGI("Send XSessionMigration data.");

  return session;
}

XSessionArbitrationJSON XLiveAPI::XSessionArbitration(uint64_t sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/arbitration",
                  kernel_state()->title_id(), sessionId);

  XSessionArbitrationJSON result = {};

  memory chunk = Get(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionMigration GET Failed!");
    assert_always();

    return result;
  }

  rapidjson::Document doc;
  doc.Parse(chunk.response);

  result.totalPlayers = doc["totalPlayers"].GetInt();

  const auto machinesArray = doc["machines"].GetArray();

  for (const auto& machine : machinesArray) {
    MachineInfo machine_info;

    machine_info.machineId =
        string_util::from_string<uint64_t>(machine["id"].GetString(), true);

    const auto playersArray = machine["players"].GetArray();
    machine_info.playerCount = playersArray.Size();

    for (const auto& player : playersArray) {
      std::vector<uint8_t> pId;
      xe::string_util::hex_string_to_array(pId, player["xuid"].GetString());
      machine_info.xuids.push_back(*reinterpret_cast<uint64_t*>(pId.data()));
    }

    result.machines.push_back(machine_info);
  }
  return result;
}

void XLiveAPI::SessionWriteStats(uint64_t sessionId, XSessionWriteStats* stats,
                                 XSessionViewProperties* leaderboard) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/leaderboards",
                  kernel_state()->title_id(), sessionId);

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

      statObject.AddMember(
          "type", static_cast<uint32_t>(statistics[statisticIndex].data.type),
          rootObject.GetAllocator());

      switch (statistics[statisticIndex].data.type) {
        case X_USER_DATA_TYPE::INT32:
          statObject.AddMember("value", statistics[statisticIndex].data.s32,
                               rootObject.GetAllocator());
          break;
        case X_USER_DATA_TYPE::INT64:
          statObject.AddMember("value", statistics[statisticIndex].data.s64,
                               rootObject.GetAllocator());
          break;
        default:
          XELOGW("Unimplemented statistic type for write {}",
                 static_cast<uint32_t>(statistics[statisticIndex].data.type));
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

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionWriteStats POST Failed!");
    // assert_always();

    return;
  }
}

XLiveAPI::memory XLiveAPI::LeaderboardsFind(const uint8_t* data) {
  std::string endpoint = fmt::format("leaderboards/find");

  memory chunk = Post(endpoint, data);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("LeaderboardsFind POST Failed!");
    assert_always();
  }

  return chunk;
}

void XLiveAPI::DeleteSession(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}",
                                     kernel_state()->title_id(), sessionId);

  memory chunk = Delete(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGI("Failed to delete session {:08X}", sessionId);
    // assert_always();
  }

  clearXnaddrCache();
  qos_payload_cache.erase(sessionId);
}

void XLiveAPI::DeleteAllSessionsByMac() {
  if (!is_active()) return;

  const std::string endpoint =
      fmt::format("DeleteSessions/{}", mac_address_->to_string());

  memory chunk = Delete(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGI("Failed to delete all sessions");
  }
}

void XLiveAPI::DeleteAllSessions() {
  if (!is_active()) return;
  
  const std::string endpoint = fmt::format("DeleteSessions");

  memory chunk = Delete(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGI("Failed to delete all sessions");
  }
}

void XLiveAPI::XSessionCreate(uint64_t sessionId, XSessionData* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions", kernel_state()->title_id());

  Document doc;
  doc.SetObject();

  std::string sessionId_str = fmt::format("{:016x}", sessionId);
  assert_true(sessionId_str.size() == 16);

  doc.AddMember("sessionId", sessionId_str, doc.GetAllocator());
  doc.AddMember("flags", data->flags, doc.GetAllocator());
  doc.AddMember("publicSlotsCount", data->num_slots_public, doc.GetAllocator());
  doc.AddMember("privateSlotsCount", data->num_slots_private,
                doc.GetAllocator());
  doc.AddMember("userIndex", data->user_index, doc.GetAllocator());
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", mac_address_->to_string(), doc.GetAllocator());
  doc.AddMember("port", GetPlayerPort(), doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGI("XSessionCreate POST Failed!");
    assert_always();
    return;
  }

  XELOGI("XSessionCreate POST Success");
}

void XLiveAPI::SessionContextSet(uint64_t session_id,
                                 std::map<uint32_t, uint32_t> contexts) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/context",
                                     kernel_state()->title_id(), session_id);

  Document doc;
  doc.SetObject();

  Value contextsJson(rapidjson::kArrayType);

  for (const auto& entry : contexts) {
    Value contextJson(kObjectType);
    contextJson.AddMember("contextId", entry.first, doc.GetAllocator());
    contextJson.AddMember("value", entry.second, doc.GetAllocator());
    contextsJson.PushBack(contextJson.Move(), doc.GetAllocator());
  }
  doc.AddMember("contexts", contextsJson, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGI("XSessionCreate POST Failed!");
    assert_always();
  }
}

const std::map<uint32_t, uint32_t> XLiveAPI::SessionContextGet(
    uint64_t session_id) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/context",
                                     kernel_state()->title_id(), session_id);

  std::map<uint32_t, uint32_t> result = {};
  memory chunk = Get(endpoint);
  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionGet error code: {}", chunk.http_code);
    assert_always();

    return result;
  }

  rapidjson::Document doc;
  doc.Parse(chunk.response);

  const Value& contexts = doc["context"];

  for (auto itr = contexts.MemberBegin(); itr != contexts.MemberEnd(); itr++) {
    const uint32_t context_id =
        xe::string_util::from_string<uint32_t>(itr->name.GetString(), true);
    result.insert({context_id, itr->value.GetInt()});
  }

  return result;
}
SessionJSON XLiveAPI::XSessionGet(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}",
                                     kernel_state()->title_id(), sessionId);

  SessionJSON session = SessionJSON{};

  memory chunk = Get(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionGet error code: {}", chunk.http_code);
    assert_always();

    return session;
  }

  Document doc;
  doc.Parse(chunk.response);

  session.hostAddress = doc["hostAddress"].GetString();
  session.macAddress = doc["macAddress"].GetString();

  session.port = GetPlayerPort();

  return session;
}

std::vector<XTitleServer> XLiveAPI::GetServers() {
  std::string endpoint =
      fmt::format("title/{:08X}/servers", kernel_state()->title_id());

  memory chunk = Get(endpoint);

  std::vector<XTitleServer> servers{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
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

XONLINE_SERVICE_INFO XLiveAPI::GetServiceInfoById(uint32_t serviceId) {
  std::string endpoint = fmt::format("title/{:08X}/services/{:08X}",
                                     kernel_state()->title_id(), serviceId);

  memory chunk = Get(endpoint);

  XONLINE_SERVICE_INFO service{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
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

void XLiveAPI::SessionJoinRemote(uint64_t sessionId,
                                 const std::vector<std::string> xuids) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/join",
                                     kernel_state()->title_id(), sessionId);
  Document doc;
  doc.SetObject();

  Value xuidsJsonArray(kArrayType);

  for each (const auto xuid in xuids) {
    Value value;
    value.SetString(xuid.c_str(), 16, doc.GetAllocator());

    xuidsJsonArray.PushBack(value, doc.GetAllocator());
  }

  doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionJoinRemote error code: {}", chunk.http_code);
    assert_always();
  }
}

void XLiveAPI::SessionLeaveRemote(uint64_t sessionId,
                                  const std::vector<std::string> xuids) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/leave",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  Value xuidsJsonArray(kArrayType);

  for each (const auto xuid in xuids) {
    Value value;
    value.SetString(xuid.c_str(), 16, doc.GetAllocator());

    xuidsJsonArray.PushBack(value, doc.GetAllocator());
  }

  doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionLeaveRemote error code: {}", chunk.http_code);
    assert_always();
  }
}

const uint8_t* XLiveAPI::GenerateMacAddress() {
  uint8_t* mac_address = new uint8_t[6];
  // MAC OUI part for MS devices.
  mac_address[0] = 0x00;
  mac_address[1] = 0x22;
  mac_address[2] = 0x48;

  xam::XNetRandom(mac_address + 3, 3);

  return mac_address;
}

const uint8_t* XLiveAPI::GetMACaddress() {
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