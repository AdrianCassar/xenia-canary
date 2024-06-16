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

DEFINE_string(api_address, "192.168.0.1:36000/",
              "Xenia Server Address e.g. IP:PORT", "Live");

DEFINE_string(
    api_list, "https://xenia-netplay-2a0298c0e3f4.herokuapp.com/,",
    "Comma delimited list URL1, URL2. Set api_address during runtime.", "Live");

DEFINE_bool(logging, false, "Log Network Activity & Stats", "Live");

DEFINE_bool(log_mask_ips, true, "Do not include P2P IPs inside the log",
            "Live");

DEFINE_bool(offline_mode, false, "Offline Mode e.g. not connected to a LAN",
            "Live");

DEFINE_string(network_guid, "", "Network Interface GUID", "Live");

DECLARE_string(upnp_root);

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
// Extract stat descriptions from XDBF.
// Profiles have offline and online XUIDs we only use online.

// https://patents.google.com/patent/US20060287099A1
namespace xe {
namespace kernel {

void XLiveAPI::IpGetConsoleXnAddr(XNADDR* XnAddr_ptr) {
  memset(XnAddr_ptr, 0, sizeof(XNADDR));

  if (IsOnline()) {
    XnAddr_ptr->ina = OnlineIP().sin_addr;
    XnAddr_ptr->inaOnline = OnlineIP().sin_addr;
    XnAddr_ptr->wPortOnline = GetPlayerPort();
  }

  // XnAddr_ptr->ina = LocalIP().sin_addr;

  memcpy(XnAddr_ptr->abEnet, mac_address_->raw(), sizeof(MacAddress));
  memcpy(XnAddr_ptr->abOnline, mac_address_->raw(), sizeof(MacAddress));
}

const uint64_t XLiveAPI::GetMachineId(const uint64_t mac_address) {
  const uint64_t machine_id_mask = 0xFA00000000000000;

  return machine_id_mask | mac_address;
}

const uint64_t XLiveAPI::GetLocalMachineId() {
  if (!mac_address_) {
    XELOGE("Mac Address not initialized!");
    assert_always();
  }

  return GetMachineId(mac_address_->to_uint64());
}

XLiveAPI::InitState XLiveAPI::GetInitState() { return initialized_; }

std::vector<std::string> XLiveAPI::ParseAPIList() {
  if (cvars::api_list.empty()) {
    OVERRIDE_string(api_list, default_public_server_ + ",");
  }

  std::unordered_set<std::string> unique_api_addresses;
  std::vector<std::string> api_addresses;

  std::stringstream api_list(cvars::api_list);
  std::string api_address;

  while (std::getline(api_list, api_address, ',')) {
    if (api_addresses.size() >= 10) {
      break;
    }

    api_address = xe::string_util::trim(api_address);

    if (api_address.empty()) {
      continue;
    }

    // Check if address is unique
    if (unique_api_addresses.insert(api_address).second) {
      api_addresses.push_back(api_address);
    }
  }

  if (api_addresses.size() < 10) {
    if (unique_api_addresses.insert(GetApiAddress()).second) {
      OVERRIDE_string(api_list, cvars::api_list + GetApiAddress() + ",");
      api_addresses.push_back(GetApiAddress());
    }
  }

  return api_addresses;
}

void XLiveAPI::SetAPIAddress(std::string address) {
  if (initialized_ == InitState::Pending) {
    OVERRIDE_string(api_address, address);
  }
}

void XLiveAPI::SetNetworkInterfaceByGUID(std::string guid) {
  if (initialized_ == InitState::Pending) {
    OVERRIDE_string(network_guid, guid);

    DiscoverNetworkInterfaces();
    SelectNetworkInterface();
  }
}

std::string XLiveAPI::GetApiAddress() {
  cvars::api_address = xe::string_util::trim(cvars::api_address);

  if (cvars::api_address.empty()) {
    cvars::api_address = default_local_server_;
  }

  // Add forward slash if not already added
  if (cvars::api_address.back() != '/') {
    cvars::api_address = cvars::api_address + '/';
  }

  return cvars::api_address;
}

// If online NAT open, otherwise strict.
uint32_t XLiveAPI::GetNatType() { return IsOnline() ? 1 : 3; }

bool XLiveAPI::IsOnline() { return OnlineIP().sin_addr.s_addr != 0; }

bool XLiveAPI::IsConnectedToLAN() { return LocalIP().sin_addr.s_addr != 0; }

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

  if (cvars::offline_mode) {
    XELOGI("XLiveAPI:: Offline mode enabled!");
    initialized_ = InitState::Failed;
    return;
  }

  if (cvars::upnp) {
    upnp_handler->Initialize();
  }

  DiscoverNetworkInterfaces();
  SelectNetworkInterface();

  online_ip_ = Getwhoami();

  if (!IsOnline()) {
    // Assign online ip as local ip to ensure XNADDR is not 0 for systemlink
    online_ip_ = local_ip_;

    XELOGE("XLiveAPI:: Cannot reach API server.");
    initialized_ = InitState::Failed;
    return;
  }

  // Download ports mappings before initializing UPnP.
  DownloadPortMappings();

  std::unique_ptr<HTTPResponseObjectJSON> reg_result = RegisterPlayer();

  initialized_ = InitState::Success;

  // Delete sessions on start-up.
  DeleteAllSessions();
}

void XLiveAPI::clearXnaddrCache() {
  sessionIdCache.clear();
  macAddressCache.clear();
}

// Request data from the server
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::Get(std::string endpoint) {
  response_data chunk = {};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (GetInitState() == InitState::Failed) {
    XELOGE("XLiveAPI::Get: Initialization failed");
    return PraseResponse(chunk);
  }

  if (!curl_handle) {
    XELOGE("XLiveAPI::Get: Cannot initialize CURL");
    return PraseResponse(chunk);
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
    return PraseResponse(chunk);
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
    return PraseResponse(chunk);
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (result == CURLE_OK &&
      (chunk.http_code == HTTP_STATUS_CODE::HTTP_OK ||
       chunk.http_code == HTTP_STATUS_CODE::HTTP_NO_CONTENT)) {
    return PraseResponse(chunk);
  }

  XELOGE("XLiveAPI::Get: Failed! HTTP Error Code: {}", chunk.http_code);
  return PraseResponse(chunk);
}

// Send data to the server
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::Post(std::string endpoint,
                                                       const uint8_t* data,
                                                       size_t data_size) {
  response_data chunk = {};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (GetInitState() == InitState::Failed) {
    XELOGE("XLiveAPI::Post: Initialization failed");
    return PraseResponse(chunk);
  }

  if (!curl_handle) {
    XELOGE("XLiveAPI::Post: Cannot initialize CURL");
    return PraseResponse(chunk);
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
      return PraseResponse(chunk);
    }

    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  }

  // FindPlayers, QoS, SessionSearch
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, callback);

  result = curl_easy_perform(curl_handle);

  if (result != CURLE_OK) {
    XELOGE("XLiveAPI::Post: CURL Error Code: {}", static_cast<uint32_t>(result));
    return PraseResponse(chunk);
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (CURLE_OK == result && chunk.http_code == HTTP_STATUS_CODE::HTTP_CREATED) {
    return PraseResponse(chunk);
  }

  XELOGE("XLiveAPI::Post: Failed! HTTP Error Code: {}", chunk.http_code);
  return PraseResponse(chunk);
}

// Delete data from the server
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::Delete(std::string endpoint) {
  response_data chunk = {};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (GetInitState() == InitState::Failed) {
    XELOGE("XLiveAPI::Delete: Initialization failed");
    return PraseResponse(chunk);
  }

  if (!curl_handle) {
    XELOGE("XLiveAPI::Delete: Cannot initialize CURL");
    return PraseResponse(chunk);
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
    XELOGE("XLiveAPI::Delete: CURL Error Code: {}", static_cast<uint32_t>(result));
    return PraseResponse(chunk);
  }

  result =
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &chunk.http_code);

  curl_easy_cleanup(curl_handle);
  curl_slist_free_all(headers);

  if (result == CURLE_OK && chunk.http_code == HTTP_STATUS_CODE::HTTP_OK) {
    return PraseResponse(chunk);
  }

  XELOGE("XLiveAPI::Delete: Failed! HTTP Error Code: {}", chunk.http_code);
  return PraseResponse(chunk);
}

// Check connection to xenia web server.
sockaddr_in XLiveAPI::Getwhoami() {
  std::unique_ptr<HTTPResponseObjectJSON> response = Get("whoami");

  sockaddr_in addr{};

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    return addr;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

  XELOGI("Requesting Public IP");

  addr = ip_to_sockaddr(doc["address"].GetString());

  return addr;
}

void XLiveAPI::DownloadPortMappings() {
  std::string endpoint =
      fmt::format("title/{:08X}/ports", kernel_state()->title_id());

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    assert_always();
    return;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

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
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::RegisterPlayer() {
  assert_not_null(mac_address_);

  std::unique_ptr<HTTPResponseObjectJSON> response{};

  // User index hard-coded
  const uint32_t index = 0;

  if (!kernel_state()->xam_state()->IsUserSignedIn(index)) {
    XELOGE("Cancelled Registering Player, player not signed in!");
    return response;
  }

  if (!mac_address_) {
    XELOGE("Cancelled Registering Player");
    return response;
  }

  PlayerObjectJSON player = PlayerObjectJSON();

  // User index hard-coded
  player.XUID(kernel_state()->xam_state()->GetUserProfile(index)->xuid());
  player.Gamertag(kernel_state()->xam_state()->GetUserProfile(index)->name());
  player.MachineID(GetLocalMachineId());
  player.HostAddress(OnlineIP_str());
  player.MacAddress(mac_address_->to_uint64());

  std::string player_output;
  bool valid = player.Serialize(player_output);
  assert_true(valid);

  response = Post("players", (uint8_t*)player_output.c_str());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    assert_always();
    return response;
  }

  XELOGI("POST Success");

  auto player_lookup = FindPlayer(OnlineIP_str());

  // Check for errnours profile lookup
  if (player_lookup->XUID() != player.XUID()) {
    XELOGI("XLiveAPI:: Player 0 XUID mismatch!");

    assert_always();
  }

  return response;
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
  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post("players/find", (uint8_t*)buffer.GetString());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("FindPlayers error message: {}", response->Message());
    assert_always();

    return player;
  }

  player = response->Deserialize<PlayerObjectJSON>();

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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, qosData, qosLength);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    assert_always();
    return;
  }

  XELOGI("Sent QoS data.");
}

// Get QoS binary data from the server
response_data XLiveAPI::QoSGet(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/qos",
                                     kernel_state()->title_id(), sessionId);

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK &&
      response->StatusCode() != HTTP_STATUS_CODE::HTTP_NO_CONTENT) {
    XELOGE("QoSGet error message: {}", response->Message());
    assert_always();

    return response->RawResponse();
  }

  XELOGI("Requesting QoS data.");

  return response->RawResponse();
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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)buffer.GetString());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("Modify error message: {}", response->Message());
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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)buffer.GetString());

  std::vector<std::unique_ptr<SessionObjectJSON>> sessions;

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionSearch error message: {}", response->Message());
    assert_always();

    return sessions;
  }

  doc.Swap(doc.Parse(response->RawResponse().response));

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

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  std::unique_ptr<SessionObjectJSON> session =
      std::make_unique<SessionObjectJSON>();

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("SessionDetails error message: {}", response->Message());
    assert_always();

    return session;
  }

  session = response->Deserialize<SessionObjectJSON>();

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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)buffer.GetString());

  std::unique_ptr<SessionObjectJSON> session =
      std::make_unique<SessionObjectJSON>();

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("XSessionMigration error message: {}", response->Message());

    assert_always();

    if (response->StatusCode() == HTTP_STATUS_CODE::HTTP_NOT_FOUND) {
      XELOGE("Cannot migrate session {:016X} not found.", sessionId);
    }

    return session;
  }

  session = response->Deserialize<SessionObjectJSON>();

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

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionArbitration error message: {}", response->Message());
    assert_always();

    return arbitration;
  }

  arbitration = response->Deserialize<ArbitrationObjectJSON>();

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
  bool valid = leaderboard.Serialize(output);
  assert_true(valid);

  if (cvars::logging) {
    XELOGI("SessionWriteStats:\n\n{}", output);
  }

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)output.c_str());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionWriteStats error message: {}", response->Message());
    // assert_always();

    return;
  }
}

std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::LeaderboardsFind(
    const uint8_t* data) {
  std::string endpoint = fmt::format("leaderboards/find");

  std::unique_ptr<HTTPResponseObjectJSON> response = Post(endpoint, data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("LeaderboardsFind error message: {}", response->Message());
    assert_always();
  }

  return response;
}

void XLiveAPI::DeleteSession(uint64_t sessionId) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}",
                                     kernel_state()->title_id(), sessionId);

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete session {:08X}", sessionId);
    XELOGE("DeleteSession error message: {}", response->Message());
    // assert_always();
  }

  clearXnaddrCache();
  qos_payload_cache.erase(sessionId);
}

void XLiveAPI::DeleteAllSessionsByMac() {
  if (!mac_address_) {
    return;
  }

  const std::string endpoint =
      fmt::format("DeleteSessions/{}", mac_address_->to_string());

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete all sessions");
  }
}

void XLiveAPI::DeleteAllSessions() {
  const std::string endpoint = fmt::format("DeleteSessions");

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete all sessions");
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
  bool valid = session.Serialize(session_output);
  assert_true(valid);

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)session_output.c_str());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("XSessionCreate error message: {}", response->Message());
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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)buffer.GetString());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionContextSet error message: {}", response->Message());
    assert_always();
  }
}

const std::map<uint32_t, uint32_t> XLiveAPI::SessionContextGet(
    uint64_t session_id) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/context",
                                     kernel_state()->title_id(), session_id);

  std::map<uint32_t, uint32_t> result = {};

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("SessionContextGet error message: {}", response->Message());
    assert_always();

    return result;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

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

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionGet error message: {}", response->Message());
    assert_always();

    return session;
  }

  session = response->Deserialize<SessionObjectJSON>();

  return session;
}

std::vector<XTitleServer> XLiveAPI::GetServers() {
  std::string endpoint =
      fmt::format("title/{:08X}/servers", kernel_state()->title_id());

  if (xlsp_servers_cached) {
    return xlsp_servers;
  }

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("GetServers error message: {}", response->Message());
    assert_always();

    return xlsp_servers;
  }

  xlsp_servers_cached = true;

  Document doc;
  doc.Parse(response->RawResponse().response);

  for (const auto& server_data : doc.GetArray()) {
    XTitleServer server{};

    server.server_address = ip_to_in_addr(server_data["address"].GetString());

    server.flags = server_data["flags"].GetInt();

    std::string description = server_data["description"].GetString();

    if (description.size() < sizeof(server.server_description)) {
      strcpy(server.server_description, description.c_str());
    }

    xlsp_servers.push_back(server);
  }

  return xlsp_servers;
}

XONLINE_SERVICE_INFO XLiveAPI::GetServiceInfoById(uint32_t serviceId) {
  std::string endpoint = fmt::format("title/{:08X}/services/{:08X}",
                                     kernel_state()->title_id(), serviceId);

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  XONLINE_SERVICE_INFO service{};

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("GetServiceById error message: {}", response->Message());
    assert_always();

    return service;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)buffer.GetString());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionJoinRemote error message: {}", response->Message());
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

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)buffer.GetString());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionLeaveRemote error message: {}", response->Message());
    assert_always();
  }
}

std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::PraseResponse(
    response_data chunk) {
  std::unique_ptr<HTTPResponseObjectJSON> response =
      std::make_unique<HTTPResponseObjectJSON>(chunk);

  const std::string defaultMessage = "{ \"message\": \"N/A\" }";

  /*
     Valid:
     {}
     []

     Invalid:
     QoS binary data
  */

  // Replace null response with default response
  const std::string responseData =
      chunk.response ? chunk.response : defaultMessage;

  bool validJSON = response->Deserialize(responseData);

  // Always set status code in case validation fails
  if (!response->StatusCode()) {
    response->StatusCode(chunk.http_code);
  }

  return response;
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
  return GenerateMacAddress();

  XELOGI("Resolving system mac address.");

#ifdef XE_PLATFORM_WIN32
  // Select MAC based on network adapter
  for (auto& adapter : adapter_addresses) {
    if (cvars::network_guid == adapter.AdapterName) {
      if (adapter.PhysicalAddressLength != NULL &&
          adapter.PhysicalAddressLength == 6) {
        uint8_t* adapter_mac_ptr = new uint8_t[MAX_ADAPTER_ADDRESS_LENGTH - 2];

        memcpy(adapter_mac_ptr, adapter.PhysicalAddress,
               sizeof(adapter_mac_ptr));

        return adapter_mac_ptr;
      }
    }
  }

  return GenerateMacAddress();
#else
  return GenerateMacAddress();
#endif  // XE_PLATFORM_WIN32
}

std::string XLiveAPI::GetNetworkFriendlyName(IP_ADAPTER_ADDRESSES adapter) {
  char interface_name[MAX_ADAPTER_NAME_LENGTH];
  wcstombs(interface_name, adapter.FriendlyName, sizeof(interface_name));

  return interface_name;
}

void XLiveAPI::DiscoverNetworkInterfaces() {
  XELOGI("Discovering network interfaces...");

#ifdef XE_PLATFORM_WIN32
  uint32_t dwRetval = 0;
  ULONG outBufLen = 0;

  IP_ADAPTER_ADDRESSES* adapters_ptr = nullptr;

  adapter_addresses.clear();
  adapter_addresses_buf.clear();

  dwRetval = GetAdaptersAddresses(AF_INET, 0, 0, 0, &outBufLen);

  adapter_addresses_buf.resize(outBufLen);

  if (dwRetval == ERROR_BUFFER_OVERFLOW) {
    adapters_ptr =
        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapter_addresses_buf.data());
  }

  dwRetval = GetAdaptersAddresses(AF_INET, 0, 0, adapters_ptr, &outBufLen);

  std::string networks = "Network Interfaces:\n";

  for (IP_ADAPTER_ADDRESSES* adapter_ptr = adapters_ptr; adapter_ptr != nullptr;
       adapter_ptr = adapter_ptr->Next) {
    if (adapter_ptr->OperStatus == IfOperStatusUp &&
        (adapter_ptr->IfType == IF_TYPE_IEEE80211 ||
         adapter_ptr->IfType == IF_TYPE_ETHERNET_CSMACD)) {
      if (adapter_ptr->PhysicalAddress != nullptr) {
        for (PIP_ADAPTER_UNICAST_ADDRESS_LH adapater_address =
                 adapter_ptr->FirstUnicastAddress;
             adapater_address != nullptr;
             adapater_address = adapater_address->Next) {
          sockaddr_in addr_ptr = *reinterpret_cast<sockaddr_in*>(
              adapater_address->Address.lpSockaddr);

          if (addr_ptr.sin_family == AF_INET) {
            std::string friendlyName = GetNetworkFriendlyName(*adapter_ptr);
            std::string guid = adapter_ptr->AdapterName;

            IP_ADAPTER_ADDRESSES adapter = IP_ADAPTER_ADDRESSES(*adapter_ptr);

            adapter_addresses.push_back(adapter);

            if (guid == cvars::network_guid) {
              interface_name = friendlyName;
            }

            networks += fmt::format("{} {}: {}\n", friendlyName, guid,
                                    ip_to_string(addr_ptr));
          }
        }
      }
    }
  }

  if (adapter_addresses.empty()) {
    XELOGI("No network interfaces detected!\n");
  } else {
    XELOGI("Found {} network interfaces!\n", adapter_addresses.size());
  }

  if (cvars::logging) {
    XELOGI("{}", xe::string_util::trim(networks));
  }
#else
#endif  // XE_PLATFORM_WIN32
}

bool XLiveAPI::UpdateNetworkInterface(sockaddr_in local_ip,
                                      IP_ADAPTER_ADDRESSES adapter) {
  for (PIP_ADAPTER_UNICAST_ADDRESS_LH address = adapter.FirstUnicastAddress;
       address != NULL; address = address->Next) {
    sockaddr_in adapter_addr =
        *reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);

    if (adapter_addr.sin_family == AF_INET) {
      if (cvars::network_guid.empty()) {
        if (local_ip.sin_addr.s_addr == adapter_addr.sin_addr.s_addr ||
            local_ip.sin_addr.s_addr == 0) {
          local_ip_ = adapter_addr;
          OVERRIDE_string(network_guid, adapter.AdapterName);
          return true;
        }
      } else {
        local_ip_ = adapter_addr;
        OVERRIDE_string(network_guid, adapter.AdapterName);
        return true;
      }
    }
  }

  return false;
}

void XLiveAPI::SelectNetworkInterface() {
  sockaddr_in local_ip{};

  // If upnp is disabled or upnp_root is empty fallback to winsock
  if (cvars::upnp && !cvars::upnp_root.empty()) {
    local_ip = ip_to_sockaddr(UPnP::GetLocalIP());
  } else {
    local_ip = WinsockGetLocalIP();
  }

  XELOGI("Checking for interface: {}", cvars::network_guid);

  bool updated = false;

  // If existing network GUID exists use it
  for (auto const& adapter : adapter_addresses) {
    if (cvars::network_guid == adapter.AdapterName) {
      if (UpdateNetworkInterface(local_ip, adapter)) {
        interface_name = GetNetworkFriendlyName(adapter);
        updated = true;
        break;
      }
    }
  }

  // Find interface that has local_ip
  if (!updated) {
    XELOGI("Network Interface GUID: {} not found!",
           cvars::network_guid.empty() ? "N\\A" : cvars::network_guid);

    for (auto const& adapter : adapter_addresses) {
      if (UpdateNetworkInterface(local_ip, adapter)) {
        interface_name = GetNetworkFriendlyName(adapter);
        updated = true;
        break;
      }
    }
  }

  // Use first interface from adapter_addresses, otherwise unspecified network
  if (!updated) {
    // Reset the GUID
    OVERRIDE_string(network_guid, "");

    XELOGI("Interface GUID: {} not found!",
           cvars::network_guid.empty() ? "N\\A" : cvars::network_guid);

    if (cvars::network_guid.empty()) {
      if (!adapter_addresses.empty()) {
        auto& adapter = adapter_addresses.front();

        if (UpdateNetworkInterface(local_ip, adapter)) {
          interface_name = GetNetworkFriendlyName(adapter);
        }
      } else {
        local_ip_ = local_ip;
        interface_name = "Unspecified Network";
      }
    } else {
      interface_name = "Unspecified Network";
    }
  }

  XELOGI("Set network interface: {} {}", interface_name, cvars::network_guid);

  assert_false(cvars::network_guid == "");
}
}  // namespace kernel
}  // namespace xe