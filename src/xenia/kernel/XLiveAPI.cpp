/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/emulator.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"

#include "xenia/kernel/XLiveAPI.h"

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
// Asynchronous UPnP
// Use the overlapped task for asynchronous curl requests.
// API endpoint lookup table
//
// How is systemlink state determined?
// Extract stat descriptions from XDBF.
// Profiles have offline and online XUIDs we only use online.

// https://patents.google.com/patent/US20060287099A1
namespace xe {
namespace kernel {

const uint64_t XLiveAPI::GetMachineId() {
  const uint64_t machineIdMask = 0xFA00000000000000;

  const uint64_t macAddress = mac_address_->to_uint64();
  const uint64_t macAddressUint =
      *reinterpret_cast<const uint64_t*>(&macAddress);
  return machineIdMask | macAddressUint;
}

XLiveAPI::InitState XLiveAPI::GetInitState() { return initialized_; }

std::string XLiveAPI::GetApiAddress() {
  cvars::api_address = xe::string_util::trim(cvars::api_address);

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
  if (GetInitState() != InitState::Pending) {
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

  upnp_handler = new UPnP();
  mac_address_ = new MacAddress(GetMACaddress());
  local_ip_ = ip_to_sockaddr(UPnP::GetLocalIP());

  if (cvars::offline_mode) {
    XELOGI("XLiveAPI:: Offline mode enabled!");
    initialized_ = InitState::Failed;
    return;
  }

  online_ip_ = Getwhoami();

  if (!IsOnline()) {
    XELOGI("XLiveAPI:: Cannot reach API server.");
    initialized_ = InitState::Failed;
    return;
  }

  // Download ports mappings before initializing UPnP.
  DownloadPortMappings();

  if (cvars::upnp) {
    upnp_handler->Initialize();
  }

  // Must get mac address and IP before registering.
  auto reg_result = RegisterPlayer();

  // If player already exists on server then no need to post it again?
  auto player = FindPlayer(OnlineIP_str());

  if (reg_result.http_code == HTTP_STATUS_CODE::HTTP_CREATED &&
      player->XUID() != 0) {
    initialized_ = InitState::Success;
  } else {
    initialized_ = InitState::Failed;
  }

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

  if (GetInitState() == InitState::Failed) {
    XELOGE("XLiveAPI::Get: Initialization failed");
    return chunk;
  }

  if (!curl_handle) {
    XELOGE("XLiveAPI::Get: Cannot initialize CURL");
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

  if (result != CURLE_OK) {
    XELOGE("XLiveAPI::Get: CURL Error Code: {}", static_cast<uint32_t>(result));
    return chunk;
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (result == CURLE_OK &&
      (chunk.http_code == HTTP_STATUS_CODE::HTTP_OK ||
       chunk.http_code == HTTP_STATUS_CODE::HTTP_NO_CONTENT)) {
    return chunk;
  }

  XELOGE("XLiveAPI::Get: Failed! HTTP Error Code: {}", chunk.http_code);
  return chunk;
}

// Send data to the server
XLiveAPI::memory XLiveAPI::Post(std::string endpoint, const uint8_t* data,
                                size_t data_size) {
  memory chunk = {0};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (GetInitState() == InitState::Failed) {
    XELOGE("XLiveAPI::Post: Initialization failed");
    return chunk;
  }

  if (!curl_handle) {
    XELOGE("XLiveAPI::Post: Cannot initialize CURL");
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

  // FindPlayers, QoS, SessionSearch
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, callback);

  result = curl_easy_perform(curl_handle);

  if (result != CURLE_OK) {
    XELOGE("XLiveAPI::Post: CURL Error Code: {}",
           static_cast<uint32_t>(result));
    return chunk;
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (CURLE_OK == result && chunk.http_code == HTTP_STATUS_CODE::HTTP_CREATED) {
    return chunk;
  }

  XELOGE("XLiveAPI::Post: Failed! HTTP Error Code: {}", chunk.http_code);
  return chunk;
}

// Delete data from the server
XLiveAPI::memory XLiveAPI::Delete(std::string endpoint) {
  memory chunk = {0};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (GetInitState() == InitState::Failed) {
    XELOGE("XLiveAPI::Delete: Initialization failed");
    return chunk;
  }

  if (!curl_handle) {
    XELOGE("XLiveAPI::Delete: Cannot initialize CURL");
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

  if (result != CURLE_OK) {
    XELOGE("XLiveAPI::Delete: CURL Error Code: {}",
           static_cast<uint32_t>(result));
    return chunk;
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (result == CURLE_OK && chunk.http_code == HTTP_STATUS_CODE::HTTP_OK) {
    return chunk;
  }

  XELOGE("XLiveAPI::Delete: Failed! HTTP Error Code: {}", chunk.http_code);
  return chunk;
}

// Check connection to xenia web server.
sockaddr_in XLiveAPI::Getwhoami() {
  memory chunk = Get("whoami");

  sockaddr_in addr{};

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    return addr;
  }

  Document doc;
  doc.Parse(chunk.response);

  XELOGI("Requesting Public IP");

  addr = ip_to_sockaddr(doc["address"].GetString());

  return addr;
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
      upnp_handler->AddMappedConnectPort(port["port"].GetInt(),
                                         port["mappedTo"].GetInt());
    }
  }

  if (doc.HasMember("bind")) {
    for (const auto& port : doc["bind"].GetArray()) {
      upnp_handler->AddMappedBindPort(port["port"].GetInt(),
                                      port["mappedTo"].GetInt());
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

  // User index hard-coded
  const uint32_t index = 0;

  if (!kernel_state()->xam_state()->IsUserSignedIn(index)) {
    XELOGE("Cancelled Registering Player, player not signed in!");
    return chunk;
  }

  if (!mac_address_) {
    XELOGE("Cancelled Registering Player");
    return chunk;
  }

  PlayerObjectJSON player = PlayerObjectJSON();

  player.XUID(kernel_state()->xam_state()->GetUserProfile(index)->xuid());
  player.MachineID(GetMachineId());
  player.HostAddress(OnlineIP_str());
  player.MacAddress(mac_address_->to_uint64());

  std::string player_output;
  bool valid = player.SerializeToString(player_output);
  assert_true(valid);

  chunk = Post("players", (uint8_t*)player_output.c_str());

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
std::unique_ptr<PlayerObjectJSON> XLiveAPI::FindPlayer(std::string ip) {
  std::unique_ptr<PlayerObjectJSON> player =
      std::make_unique<PlayerObjectJSON>();

  Document doc;
  doc.SetObject();
  doc.AddMember("hostAddress", ip, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  // POST & receive.
  memory chunk = Post("players/find", (uint8_t*)buffer.GetString());

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("FindPlayers POST Failed!");

    assert_always();
    return player;
  }

  player->DeserializeFromString(chunk.response);

  XELOGI("Requesting {:016X} player details.",
         static_cast<uint64_t>(player->XUID()));

  return player;
}

bool XLiveAPI::UpdateQoSCache(const uint64_t sessionId,
                              const std::vector<uint8_t> qos_payload) {
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

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK &&
      chunk.http_code != HTTP_STATUS_CODE::HTTP_NO_CONTENT) {
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

const std::vector<std::unique_ptr<SessionObjectJSON>> XLiveAPI::SessionSearch(
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

  memory chunk = Post(endpoint, (uint8_t*)buffer.GetString());

  std::vector<std::unique_ptr<SessionObjectJSON>> sessions;

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
    std::unique_ptr<SessionObjectJSON> session =
        std::make_unique<SessionObjectJSON>();
    bool valid = session->Deserialize(object_ptr->GetObj());
    assert_true(valid);

    sessions.push_back(std::move(session));
  }

  XELOGI("SessionSearch found {} sessions.", sessions.size());

  return sessions;
}

const std::unique_ptr<SessionObjectJSON> XLiveAPI::SessionDetails(
    uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/details",
                                     kernel_state()->title_id(), sessionId);

  memory chunk = Get(endpoint);

  auto session = std::make_unique<SessionObjectJSON>();

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("SessionDetails error code {}", chunk.http_code);
    XELOGE("SessionDetails not found e.g. Invalid sessionId");

    assert_always();
    return session;
  }

  bool valid = session->DeserializeFromString(chunk.response);
  assert_true(valid);

  XELOGI("Requesting Session Details.");

  return session;
}

std::unique_ptr<SessionObjectJSON> XLiveAPI::XSessionMigration(
    uint64_t sessionId) {
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

  std::unique_ptr<SessionObjectJSON> session =
      std::make_unique<SessionObjectJSON>();

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("XSessionMigration POST Failed!");
    assert_always();

    if (chunk.http_code == HTTP_STATUS_CODE::HTTP_NOT_FOUND) {
      XELOGE("Cannot migrate session {:016X} not found.", sessionId);
    }

    return session;
  }

  bool valid = session->DeserializeFromString(chunk.response);
  assert_true(valid);

  XELOGI("Send XSessionMigration data.");

  return session;
}

std::unique_ptr<ArbitrationObjectJSON> XLiveAPI::XSessionArbitration(
    uint64_t sessionId) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/arbitration",
                  kernel_state()->title_id(), sessionId);

  std::unique_ptr<ArbitrationObjectJSON> arbitration =
      std::make_unique<ArbitrationObjectJSON>();

  memory chunk = Get(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionArbitration GET Failed!");
    assert_always();

    return arbitration;
  }

  bool valid = arbitration->DeserializeFromString(chunk.response);
  assert_true(valid);

  return arbitration;
}

void XLiveAPI::SessionWriteStats(uint64_t sessionId, XSessionWriteStats* stats,
                                 XSessionViewProperties* view_properties) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/leaderboards",
                  kernel_state()->title_id(), sessionId);

  std::vector<XSessionViewProperties> properties(
      view_properties, view_properties + stats->number_of_leaderboards);

  LeaderboardObjectJSON leaderboard = LeaderboardObjectJSON();

  leaderboard.Stats(*stats);
  leaderboard.ViewProperties(properties);

  std::string output;
  bool valid = leaderboard.SerializeToString(output);
  assert_true(valid);

  if (cvars::logging) {
    XELOGI("SessionWriteStats:\n\n{}", output);
  }

  memory chunk = Post(endpoint, (uint8_t*)output.c_str());

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
  const std::string endpoint =
      fmt::format("DeleteSessions/{}", mac_address_->to_string());

  memory chunk = Delete(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGI("Failed to delete all sessions");
  }
}

void XLiveAPI::DeleteAllSessions() {
  const std::string endpoint = fmt::format("DeleteSessions");

  memory chunk = Delete(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGI("Failed to delete all sessions");
  }
}

void XLiveAPI::XSessionCreate(uint64_t sessionId, XSessionData* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions", kernel_state()->title_id());

  std::string sessionId_str = fmt::format("{:016x}", sessionId);
  assert_true(sessionId_str.size() == 16);

  const auto& media_id = kernel_state()
                             ->GetExecutableModule()
                             ->xex_module()
                             ->opt_execution_info()
                             ->media_id;

  const std::string mediaId_str =
      fmt::format("{:08X}", static_cast<uint32_t>(media_id));

  SessionObjectJSON session = SessionObjectJSON();

  session.SessionID(sessionId_str);
  session.Title(kernel_state()->emulator()->title_name());
  session.MediaID(mediaId_str);
  session.Version(kernel_state()->emulator()->title_version());
  session.Flags(data->flags);
  session.PublicSlotsCount(data->num_slots_public);
  session.PrivateSlotsCount(data->num_slots_private);
  session.UserIndex(data->user_index);
  session.HostAddress(OnlineIP_str());
  session.MacAddress(mac_address_->to_string());
  session.Port(GetPlayerPort());

  std::string session_output;
  bool valid = session.SerializeToString(session_output);
  assert_true(valid);

  memory chunk = Post(endpoint, (uint8_t*)session_output.c_str());

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

  Value contextsJson(kArrayType);

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
    XELOGI("SessionContextSet POST Failed!");
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

  Document doc;
  doc.Parse(chunk.response);

  const Value& contexts = doc["context"];

  for (auto itr = contexts.MemberBegin(); itr != contexts.MemberEnd(); itr++) {
    const uint32_t context_id =
        xe::string_util::from_string<uint32_t>(itr->name.GetString(), true);
    result.insert({context_id, itr->value.GetInt()});
  }

  return result;
}

std::unique_ptr<SessionObjectJSON> XLiveAPI::XSessionGet(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}",
                                     kernel_state()->title_id(), sessionId);

  std::unique_ptr<SessionObjectJSON> session =
      std::make_unique<SessionObjectJSON>();

  memory chunk = Get(endpoint);

  if (chunk.http_code != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionGet error code: {}", chunk.http_code);
    assert_always();

    return session;
  }

  bool valid = session->DeserializeFromString(chunk.response);
  assert_true(valid);

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

    server.server_address = ip_to_in_addr(server_data["address"].GetString());

    server.flags = server_data["flags"].GetInt();

    std::string description = server_data["description"].GetString();

    if (description.size() < sizeof(server.server_description)) {
      strcpy(server.server_description, description.c_str());
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
    service.ip = ip_to_in_addr(service_info["address"].GetString());

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

  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<uint16_t> dist(0, 0xFF);

  for (int i = 3; i < 6; i++) {
    mac_address[i] = (uint8_t)dist(rnd);
  }

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
