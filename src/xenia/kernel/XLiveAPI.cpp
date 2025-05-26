/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "third_party/rapidcsv/src/rapidcsv.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"

DEFINE_string(api_address, "192.168.0.1:36000/",
              "Xenia Server Address e.g. IP:PORT", "Live");

DEFINE_string(
    api_list, "https://xenia-netplay-2a0298c0e3f4.herokuapp.com/,",
    "Comma delimited list URL1, URL2 (Max 10). Set api_address during runtime.",
    "Live");

DEFINE_bool(logging, false, "Log Network Activity & Stats", "Live");

DEFINE_bool(log_mask_ips, true, "Do not include P2P IPs inside the log",
            "Live");

DEFINE_int32(network_mode, 2,
             "Network mode types: 0 - Offline, 1 - Systemlink, 2 - Xbox Live.",
             "Live");

DEFINE_bool(xlink_kai_systemlink_hack, false,
            "Enable hacks for XLink Kai support. May break some games. See: "
            "https://www.teamxlink.co.uk/wiki/Xenia_Support",
            "Live");

DEFINE_string(network_guid, "", "Network Interface GUID", "Live");

DEFINE_string(friends_xuids, "", "Comma delimited list of XUIDs. (Max 100)",
              "Live");

DEFINE_bool(xstorage_backend, true,
            "Request XStorage content from backend and fallback locally, "
            "otherwise only use local content.",
            "Live");

DEFINE_bool(
    xstorage_user_data_backend, false,
    "Store user data on backend (not recommended), otherwise fallback locally.",
    "Live");

DEFINE_int32(discord_presence_user_index, 0,
             "User profile index used for Discord rich presence [0, 3].",
             "Live");

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

// https://patents.google.com/patent/US20060287099A1
namespace xe {
namespace kernel {

void XLiveAPI::IpGetConsoleXnAddr(XNADDR* XnAddr_ptr) {
  memset(XnAddr_ptr, 0, sizeof(XNADDR));

  if (cvars::network_mode != NETWORK_MODE::OFFLINE) {
    if (IsConnectedToServer() && adapter_has_wan_routing) {
      XnAddr_ptr->ina = OnlineIP().sin_addr;
      XnAddr_ptr->inaOnline = OnlineIP().sin_addr;
    } else {
      XnAddr_ptr->ina = LocalIP().sin_addr;
      XnAddr_ptr->inaOnline = LocalIP().sin_addr;
    }

    XnAddr_ptr->wPortOnline = GetPlayerPort();
  }

  memcpy(XnAddr_ptr->abEnet, mac_address_->raw(), sizeof(MacAddress));
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

std::vector<std::string> XLiveAPI::ParseDelimitedList(std::string_view csv,
                                                      uint32_t count) {
  std::vector<std::string> parsed_list;

  std::stringstream sstream(csv.data());

  rapidcsv::Document delimiter(
      sstream, rapidcsv::LabelParams(-1, -1),
      rapidcsv::SeparatorParams(',', true), rapidcsv::ConverterParams(),
      rapidcsv::LineReaderParams(true /* pSkipCommentLines */,
                                 '#' /* pCommentPrefix */,
                                 true /* pSkipEmptyLines */));

  if (!delimiter.GetRowCount()) {
    return parsed_list;
  }

  parsed_list = delimiter.GetRow<std::string>(0);

  parsed_list.erase(std::remove_if(parsed_list.begin(), parsed_list.end(),
                                   [](const std::string& element) {
                                     return element.empty();
                                   }),
                    parsed_list.end());

  if (count != 0 && parsed_list.size() > count) {
    parsed_list.resize(count);
  }

  return parsed_list;
}

std::string XLiveAPI::BuildCSVFromVector(std::vector<std::string>& data,
                                         uint32_t count) {
  rapidcsv::Document doc(
      "", rapidcsv::LabelParams(-1, -1), rapidcsv::SeparatorParams(',', true),
      rapidcsv::ConverterParams(),
      rapidcsv::LineReaderParams(true /* pSkipCommentLines */,
                                 '#' /* pCommentPrefix */,
                                 true /* pSkipEmptyLines */));

  std::ostringstream csv;

  if (count != 0 && data.size() > count) {
    data.resize(count);
  }

  doc.InsertRow(0, data);
  doc.Save(csv);

  return xe::string_util::trim(csv.str());
}

std::vector<std::string> XLiveAPI::ParseAPIList() {
  if (cvars::api_list.empty()) {
    OVERRIDE_string(api_list, default_public_server_ + ",");
  }

  const uint32_t limit = 10;

  std::vector<std::string> api_addresses =
      ParseDelimitedList(cvars::api_list, limit);

  const std::string api_address = GetApiAddress();

  if (api_addresses.size() < limit) {
    if (std::find(api_addresses.begin(), api_addresses.end(), api_address) ==
        api_addresses.end()) {
      OVERRIDE_string(api_list, cvars::api_list + api_address + ",");
      api_addresses.push_back(api_address);
    }
  }

  // Enforce size limit
  cvars::api_list = BuildCSVFromVector(api_addresses);

  OVERRIDE_string(api_list, cvars::api_list);
  OVERRIDE_string(api_address, cvars::api_address);

  return api_addresses;
}

std::vector<std::uint64_t> XLiveAPI::ParseFriendsXUIDs() {
  const auto& xuids = cvars::friends_xuids;

  const std::vector<std::string> friends_xuids =
      ParseDelimitedList(xuids, X_ONLINE_MAX_FRIENDS);

  std::vector<std::uint64_t> xuids_parsed;

  uint32_t index = 0;
  for (const auto& friend_xuid : friends_xuids) {
    const uint64_t xuid = string_util::from_string<uint64_t>(
        xe::string_util::trim(friend_xuid), true);

    if (xuid == 0) {
      XELOGI("{}: Skip adding invalid friend XUID!", __func__);
      continue;
    }

    if (index == 0 && xuid <= X_ONLINE_MAX_FRIENDS) {
      dummy_friends_count = static_cast<uint32_t>(xuid);

      index++;
      continue;
    }

    xuids_parsed.push_back(xuid);

    index++;
  }

  return xuids_parsed;
}

void XLiveAPI::AddFriend(uint64_t xuid) {
  const auto delimeter = cvars::friends_xuids.empty() ? "" : ",";
  const auto& xuids =
      cvars::friends_xuids + fmt::format("{}{:016X}", delimeter, xuid);

  std::vector<std::string> friend_xuids =
      ParseDelimitedList(xuids, X_ONLINE_MAX_FRIENDS);

  // Remove duplicate xuids
  std::sort(friend_xuids.begin(), friend_xuids.end());
  friend_xuids.erase(std::unique(friend_xuids.begin(), friend_xuids.end()),
                     friend_xuids.end());

  const std::string friends_list =
      BuildCSVFromVector(friend_xuids, X_ONLINE_MAX_FRIENDS);

  OVERRIDE_string(friends_xuids, friends_list);
}

void XLiveAPI::RemoveFriend(uint64_t xuid) {
  auto xuid_str = fmt::format("{:016X}", xuid);

  std::vector<std::string> friend_xuids =
      ParseDelimitedList(cvars::friends_xuids, X_ONLINE_MAX_FRIENDS);

  friend_xuids.erase(
      std::remove(friend_xuids.begin(), friend_xuids.end(), xuid_str),
      friend_xuids.end());

  const std::string friends_list =
      BuildCSVFromVector(friend_xuids, X_ONLINE_MAX_FRIENDS);

  OVERRIDE_string(friends_xuids, friends_list);
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

void XLiveAPI::SetNetworkMode(uint32_t mode) {
  OVERRIDE_int32(network_mode, mode);

  if (mode == NETWORK_MODE::OFFLINE && IsConnectedToServer()) {
    DeleteAllSessionsByMac();
  }

  // Initialize Server
  if (initialized_ != InitState::Pending) {
    initialized_ = InitState::Pending;

    Init();
  }
}

std::string XLiveAPI::GetApiAddress() {
  std::vector<std::string> api_addresses =
      ParseDelimitedList(cvars::api_address, 1);

  if (api_addresses.empty()) {
    cvars::api_address = default_local_server_;
  } else {
    cvars::api_address = api_addresses.front();
  }

  // Add forward slash if not already added
  if (cvars::api_address.back() != '/') {
    cvars::api_address = cvars::api_address + '/';
  }

  return cvars::api_address;
}

// If online NAT open, otherwise strict.
uint32_t XLiveAPI::GetNatType() { return IsConnectedToServer() ? 1 : 3; }

bool XLiveAPI::IsConnectedToServer() { return OnlineIP().sin_addr.s_addr != 0; }

bool XLiveAPI::IsConnectedToLAN() { return LocalIP().sin_addr.s_addr != 0; }

uint16_t XLiveAPI::GetPlayerPort() { return 36000; }

int8_t XLiveAPI::GetVersionStatus() { return version_status; }

void XLiveAPI::Init() {
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

  if (!upnp_handler) {
    upnp_handler = new UPnP();
  }

  if (!mac_address_) {
    mac_address_ = new MacAddress(GetMACaddress());
  }

  if (cvars::network_mode == NETWORK_MODE::OFFLINE) {
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

  if (!IsConnectedToServer()) {
    // Assign online ip as local ip to ensure XNADDR is not 0 for systemlink
    // online_ip_ = local_ip_;

    // Fixes 4D53085F from crashing when joining via systemlink.
    // kernel_state()->BroadcastNotification(kXNotificationIDLiveConnectionChanged,
    //                                      X_ONLINE_S_LOGON_DISCONNECTED);

    XELOGE("XLiveAPI:: Cannot reach API server.");
    initialized_ = InitState::Failed;
    return;
  }

  // Download ports mappings before initializing UPnP.
  DownloadPortMappings();

  std::unique_ptr<HTTPResponseObjectJSON> reg_result = RegisterPlayer();

  if (reg_result &&
      reg_result->StatusCode() == HTTP_STATUS_CODE::HTTP_CREATED) {
    const uint32_t index = 0;
    const auto profile = kernel_state()->xam_state()->GetUserProfile(index);

    if (profile->GetFriends().size() < dummy_friends_count) {
      profile->AddDummyFriends(dummy_friends_count);
    }
  }

  initialized_ = InitState::Success;

  // Delete sessions on start-up.
  DeleteAllSessions();
}

void XLiveAPI::clearXnaddrCache() {
  sessionIdCache.clear();
  macAddressCache.clear();
}

// Request data from the server
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::Get(std::string endpoint,
                                                      const uint32_t timeout) {
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

  if (timeout > 0) {
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, timeout);
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
    XELOGE("XLiveAPI::Post: CURL Error Code: {}",
           static_cast<uint32_t>(result));
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
    XELOGE("XLiveAPI::Delete: CURL Error Code: {}",
           static_cast<uint32_t>(result));
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
  const uint32_t timeout = 3;

  std::unique_ptr<HTTPResponseObjectJSON> response = Get("whoami", timeout);

  sockaddr_in addr{};

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    return addr;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

  XELOGI("Requesting Public IP");

  const char* address_str = doc["address"].GetString();

  if (address_str) {
    addr = ip_to_sockaddr(address_str);
  }

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
    XELOGE("Cancelled registering profile, profile not signed in!");
    return response;
  }

  if (!mac_address_) {
    XELOGE("Cancelled registering profile!");
    return response;
  }

  const auto user_profile = kernel_state()->xam_state()->GetUserProfile(index);

  if (cvars::network_mode == NETWORK_MODE::XBOXLIVE &&
      !user_profile->IsLiveEnabled()) {
    XELOGE("Cancelled registering profile, profile is not live enabled!");
    return response;
  }

  uint64_t xuid = user_profile->GetOnlineXUID();

  // Register offline profile for systemlink usage
  if (cvars::network_mode == NETWORK_MODE::LAN &&
      !user_profile->IsLiveEnabled()) {
    xuid = user_profile->xuid();

    XELOGI("Registering offline profile {:016X} for systemlink usage", xuid);
  }

  PlayerObjectJSON player = PlayerObjectJSON();

  player.XUID(xuid);
  player.Gamertag(user_profile->name());
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
    XELOGI("XLiveAPI:: {} XUID mismatch!", player.Gamertag());
    xuid_mismatch = true;

    // assert_always();
  } else {
    xuid_mismatch = false;
  }

  return response;
}

const std::map<uint64_t, std::string> XLiveAPI::DeleteMyProfiles() {
  std::unique_ptr<HTTPResponseObjectJSON> response =
      Get("players/deletemyprofiles");

  if (!response->RawResponse().response) {
    return {};
  }

  const auto deleted_profiles =
      response->Deserialize<DeleteMyProfilesObjectJSON>();

  return deleted_profiles->GetDeletedProfiles();
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

  XELOGI("Requesting {:016X} player details.", player->XUID().get());

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

void XLiveAPI::SessionModify(uint64_t sessionId, XGI_SESSION_MODIFY* data) {
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

std::vector<std::unique_ptr<SessionObjectJSON>> XLiveAPI::GetTitleSessions(
    uint32_t title_id) {
  if (!title_id) {
    title_id = kernel_state()->title_id();
  }

  std::string endpoint = fmt::format("title/{:08X}/sessions/search", title_id);

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  std::vector<std::unique_ptr<SessionObjectJSON>> sessions;

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("GetTitleSessions error message: {}", response->Message());
    assert_always();

    return sessions;
  }

  Document doc;
  doc.SetObject();

  doc.Swap(doc.Parse(response->RawResponse().response));

  const Value& sessionsJsonArray = doc.GetArray();

  for (Value::ConstValueIterator object_ptr = sessionsJsonArray.Begin();
       object_ptr != sessionsJsonArray.End(); ++object_ptr) {
    std::unique_ptr<SessionObjectJSON> session =
        std::make_unique<SessionObjectJSON>();
    bool valid = session->Deserialize(object_ptr->GetObj());
    assert_true(valid);

    sessions.push_back(std::move(session));
  }

  XELOGI("GetTitleSessions found {} sessions.", sessions.size());

  return sessions;
}

const std::vector<std::unique_ptr<SessionObjectJSON>> XLiveAPI::SessionSearch(
    XGI_SESSION_SEARCH* data, uint32_t num_users) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/search", kernel_state()->title_id());

  Document doc;
  doc.SetObject();

  doc.AddMember("searchIndex", data->proc_index, doc.GetAllocator());
  doc.AddMember("resultsCount", data->num_results, doc.GetAllocator());
  doc.AddMember("numUsers", num_users, doc.GetAllocator());

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
    uint64_t sessionId, XGI_SESSION_MIGRATE* data) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/migrate",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  xe::be<uint64_t> xuid = 0;

  if (kernel_state()->xam_state()->IsUserSignedIn(data->user_index)) {
    const auto& profile =
        kernel_state()->xam_state()->GetUserProfile(data->user_index);

    xuid = profile->GetOnlineXUID();
  } else {
    XELOGI("New host is remote.");
  }

  const std::string xuid_str = fmt::format("{:016X}", xuid.get());

  doc.AddMember("xuid", xuid_str, doc.GetAllocator());
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

void XLiveAPI::SessionWriteStats(uint64_t sessionId, XGI_STATS_WRITE stats) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions/{:016x}/leaderboards",
                  kernel_state()->title_id(), sessionId);

  XSESSION_VIEW_PROPERTIES* view_properties =
      kernel_state()->memory()->TranslateVirtual<XSESSION_VIEW_PROPERTIES*>(
          stats.views_ptr);

  std::vector<XSESSION_VIEW_PROPERTIES> properties(
      view_properties, view_properties + stats.num_views);

  LeaderboardObjectJSON* leaderboard =
      new LeaderboardObjectJSON(stats, properties);

  std::string output;
  bool valid = leaderboard->Serialize(output);
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
  const std::string endpoint = fmt::format("DeleteSessions", 3);

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete all sessions");
  }
}

void XLiveAPI::XSessionCreate(uint64_t sessionId, XGI_SESSION_CREATE* data) {
  std::string endpoint =
      fmt::format("title/{:08X}/sessions", kernel_state()->title_id());

  std::string sessionId_str = fmt::format("{:016x}", sessionId);
  assert_true(sessionId_str.size() == 16);

  const auto& media_id = kernel_state()
                             ->GetExecutableModule()
                             ->xex_module()
                             ->opt_execution_info()
                             ->media_id;

  const std::string mediaId_str = fmt::format("{:08X}", media_id.get());

  xe::be<uint64_t> xuid = 0;

  if (kernel_state()->xam_state()->IsUserSignedIn(data->user_index)) {
    const auto& profile =
        kernel_state()->xam_state()->GetUserProfile(data->user_index);

    xuid = profile->GetOnlineXUID();
  }

  const std::string xuid_str = fmt::format("{:016X}", xuid.get());

  SessionObjectJSON session = SessionObjectJSON();

  session.SessionID(sessionId_str);
  session.XUID(xuid_str);
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

void XLiveAPI::SessionPropertiesSet(uint64_t session_id, uint32_t user_index) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/properties",
                                     kernel_state()->title_id(), session_id);

  std::unique_ptr<PropertiesObjectJSON> properties_json =
      std::make_unique<PropertiesObjectJSON>();

  const auto user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  const auto propertie_ids =
      kernel_state()->xam_state()->user_tracker()->GetUserPropertyIds(
          user_profile->xuid());

  std::vector<xam::Property> properties = {};

  for (const auto& property_attribute : propertie_ids) {
    const xam::Property* property =
        kernel_state()->xam_state()->user_tracker()->GetProperty(
            user_profile->xuid(), property_attribute.value);

    properties.push_back(*property);
  }

  std::vector<xam::Property> contexts = {};

  const auto contexts_ids =
      kernel_state()->xam_state()->user_tracker()->GetUserContextIds(
          user_profile->xuid());

  for (const auto& context_attribute : contexts_ids) {
    const xam::Property* property =
        kernel_state()->xam_state()->user_tracker()->GetProperty(
            user_profile->xuid(), context_attribute.value);

    properties.push_back(*property);
  }

  properties_json->Properties(properties);

  std::string properties_seralized;
  bool valid = properties_json->Serialize(properties_seralized);
  assert_true(valid);

  auto const post_data =
      reinterpret_cast<const uint8_t*>(properties_seralized.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response = Post(endpoint, post_data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionPropertiesAdd error message: {}", response->Message());
    assert_always();
  }
}

const std::vector<xam::Property> XLiveAPI::SessionPropertiesGet(
    uint64_t session_id) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/properties",
                                     kernel_state()->title_id(), session_id);

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("SessionPropertiesGet error message: {}", response->Message());
    assert_always();

    return {};
  }

  const auto properties = response->Deserialize<PropertiesObjectJSON>();

  return properties->Properties();
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

std::vector<X_TITLE_SERVER> XLiveAPI::GetServers() {
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
    X_TITLE_SERVER server{};

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

HTTP_STATUS_CODE XLiveAPI::GetServiceInfoById(
    uint32_t serviceId, X_ONLINE_SERVICE_INFO* session_info) {
  std::string endpoint = fmt::format("title/{:08X}/services/{:08X}",
                                     kernel_state()->title_id(), serviceId);

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  HTTP_STATUS_CODE status =
      static_cast<HTTP_STATUS_CODE>(response->StatusCode());

  if (status != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("GetServiceById error message: {}", response->Message());
    assert_always();

    return status;
  }

  std::unique_ptr<ServiceInfoObjectJSON> service_info =
      response->Deserialize<ServiceInfoObjectJSON>();

  XELOGD("GetServiceById IP: {}", service_info->Address());

  session_info->id = serviceId;
  session_info->port = service_info->Port();
  session_info->ip = ip_to_in_addr(service_info->Address());

  return status;
}

void XLiveAPI::SessionJoinRemote(uint64_t sessionId,
                                 std::unordered_map<uint64_t, bool> members) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/join",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  Value xuidsJsonArray = Value(kArrayType);
  Value privateSlotsJsonArray = Value(kArrayType);

  for (const auto& [xuid, is_private] : members) {
    const std::string xuid_str = string_util::to_hex_string(xuid);

    Value xuid_value = Value(xuid_str.c_str(), 16, doc.GetAllocator());

    Value is_private_value = Value(is_private);

    xuidsJsonArray.PushBack(xuid_value.Move(), doc.GetAllocator());
    privateSlotsJsonArray.PushBack(is_private_value.Move(), doc.GetAllocator());
  }

  doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());
  doc.AddMember("privateSlots", privateSlotsJsonArray, doc.GetAllocator());

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
                                  const std::vector<xe::be<uint64_t>> xuids) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016x}/leave",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  Value xuidsJsonArray(kArrayType);

  for (const auto& xuid : xuids) {
    const std::string xuid_str = string_util::to_hex_string(xuid);

    Value xuid_value = Value(xuid_str.c_str(), 16, doc.GetAllocator());

    xuidsJsonArray.PushBack(xuid_value.Move(), doc.GetAllocator());
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

void XLiveAPI::SessionPreJoin(uint64_t sessionId,
                              const std::set<uint64_t>& xuids) {
  std::string endpoint = fmt::format("title/{:08X}/sessions/{:016X}/prejoin",
                                     kernel_state()->title_id(), sessionId);

  Document doc;
  doc.SetObject();

  Value xuids_array(kArrayType);

  for (const auto& xuid : xuids) {
    const std::string xuid_str = xe::string_util::to_hex_string(xuid);

    Value xuid_value = Value(xuid_str.c_str(), 16, doc.GetAllocator());
    xuids_array.PushBack(xuid_value.Move(), doc.GetAllocator());
  }

  doc.AddMember("xuids", xuids_array, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, reinterpret_cast<const uint8_t*>(buffer.GetString()));

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SessionPreJoin error message: {}", response->Message());
    assert_always();
  }
}

std::unique_ptr<FriendsPresenceObjectJSON> XLiveAPI::GetFriendsPresence(
    const std::vector<uint64_t>& xuids) {
  const std::string endpoint = "players/presence";

  std::unique_ptr<FriendsPresenceObjectJSON> friends =
      std::make_unique<FriendsPresenceObjectJSON>();

  friends->XUIDs(xuids);

  std::string xuids_list;
  bool valid = friends->Serialize(xuids_list);
  assert_true(valid);

  const uint8_t* xuids_data =
      reinterpret_cast<const uint8_t*>(xuids_list.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response = Post(endpoint, xuids_data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("FriendsPresence error message: {}", response->Message());
    assert_always();

    return friends;
  }

  friends = response->Deserialize<FriendsPresenceObjectJSON>();

  return friends;
}

X_STORAGE_BUILD_SERVER_PATH_RESULT XLiveAPI::XStorageBuildServerPath(
    std::string server_path) {
  // Remove address it's added later
  std::string endpoint = server_path.substr(GetApiAddress().size());

  X_STORAGE_BUILD_SERVER_PATH_RESULT result =
      X_STORAGE_BUILD_SERVER_PATH_RESULT::Invalid;

  std::unique_ptr<HTTPResponseObjectJSON> response = Post(endpoint, nullptr);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("{}: {}", __func__, response->Message());
    return result;
  }

  if (response->RawResponse().response) {
    result = static_cast<X_STORAGE_BUILD_SERVER_PATH_RESULT>(
        xe::string_util::from_string<int32_t>(response->RawResponse().response,
                                              false));
  }

  switch (result) {
    case kernel::Created:
      XELOGI("{}: Created Path: {}", __func__, server_path);
      break;
    case kernel::Found:
      XELOGI("{}: Found Path: {}", __func__, server_path);
      break;
    case kernel::Invalid:
    default:
      XELOGW("{}: Failed to create path: {}", __func__, server_path);
      break;
  }

  return result;
}

bool XLiveAPI::XStorageDelete(std::string server_path) {
  // Remove address it's added later
  std::string endpoint = server_path.substr(GetApiAddress().size());

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XStorageDelete: {}", response->Message());
    assert_always();

    return false;
  }

  return true;
}

std::span<uint8_t> XLiveAPI::XStorageDownload(std::string server_path) {
  // Remove address it's added later
  std::string endpoint = server_path.substr(GetApiAddress().size());

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  std::span<uint8_t> buffer = {};

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK &&
      response->StatusCode() != HTTP_STATUS_CODE::HTTP_NO_CONTENT) {
    XELOGE("XStorageDownload: {}", response->Message());
    assert_always();

    return buffer;
  }

  if (response->RawResponse().response) {
    buffer = std::span<uint8_t>(
        reinterpret_cast<uint8_t*>(response->RawResponse().response),
        response->RawResponse().size);
  }

  return buffer;
}

X_STORAGE_UPLOAD_RESULT XLiveAPI::XStorageUpload(std::string server_path,
                                                 std::span<uint8_t> buffer) {
  // Remove address it's added later
  std::string endpoint = server_path.substr(GetApiAddress().size());

  X_STORAGE_UPLOAD_RESULT result = X_STORAGE_UPLOAD_RESULT::UPLOAD_ERROR;

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, buffer.data(), buffer.size());

  if (response->StatusCode() == HTTP_STATUS_CODE::HTTP_PAYLOAD_TOO_LARGE) {
    return X_STORAGE_UPLOAD_RESULT::PAYLOAD_TOO_LARGE;
  }

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("XStorageUpload: {}", response->Message());
    assert_always();

    return result;
  }

  if (response->RawResponse().response) {
    result = static_cast<X_STORAGE_UPLOAD_RESULT>(
        xe::string_util::from_string<int32_t>(response->RawResponse().response,
                                              false));
  }

  return result;
}

std::pair<std::unique_ptr<XStorageFilesInfoObjectJSON>, bool>
XLiveAPI::XStorageEnumerate(std::string server_path, uint32_t max_items) {
  const size_t prefix_size =
      GetApiAddress().size() + std::string("xstorage/").size();

  std::string url_to_encode = server_path.substr(prefix_size);

  CURL* curl = curl_easy_init();

  char* encoded_url = curl_easy_escape(curl, url_to_encode.c_str(),
                                       static_cast<int>(url_to_encode.size()));

  curl_easy_cleanup(curl);

  std::string endpoint = "xstorage/enumerate/" + std::string(encoded_url);

  if (encoded_url) {
    curl_free(encoded_url);
  }

  std::pair<std::unique_ptr<XStorageFilesInfoObjectJSON>, bool>
      enumeration_result = {};

  std::unique_ptr<XStorageFilesInfoObjectJSON> enumerate_xstorage =
      std::make_unique<XStorageFilesInfoObjectJSON>();

  enumerate_xstorage->MaxItems(max_items);

  std::string enumerate_limit;
  bool valid = enumerate_xstorage->Serialize(enumerate_limit);
  assert_true(valid);

  const uint8_t* enumerate_limit_data_ptr =
      reinterpret_cast<const uint8_t*>(enumerate_limit.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, enumerate_limit_data_ptr);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("XStorageEnumerate: {}", response->Message());

    assert_always();

    enumeration_result.first = std::move(enumerate_xstorage);
    enumeration_result.second = false;

    return enumeration_result;
  }

  enumerate_xstorage = response->Deserialize<XStorageFilesInfoObjectJSON>();

  enumeration_result.first = std::move(enumerate_xstorage);
  enumeration_result.second = true;

  return enumeration_result;
}

std::unique_ptr<FindUsersObjectJSON> XLiveAPI::GetFindUsers(
    const std::vector<FIND_USER_INFO>& find_users_info) {
  const std::string endpoint = "players/findusers";

  std::unique_ptr<FindUsersObjectJSON> find_users =
      std::make_unique<FindUsersObjectJSON>();

  find_users->SetFindUsers(find_users_info);

  std::string find_users_serialized;
  bool valid = find_users->Serialize(find_users_serialized);
  assert_true(valid);

  const uint8_t* find_users_data =
      reinterpret_cast<const uint8_t*>(find_users_serialized.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, find_users_data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("GetFindUsers error message: {}", response->Message());
    assert_always();

    return find_users;
  }

  find_users = response->Deserialize<FindUsersObjectJSON>();

  return find_users;
}

void XLiveAPI::SetPresence() {
  const std::string endpoint = "players/setpresence";

  std::unique_ptr<PresenceObjectJSON> presence =
      std::make_unique<PresenceObjectJSON>();

  // Update presence for all signed in xbox live enabled profiles
  for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
    const auto user_profile = kernel_state()->xam_state()->GetUserProfile(i);

    if (user_profile) {
      FriendPresenceObjectJSON* profile_presence = new FriendPresenceObjectJSON();

      if (user_profile->IsLiveEnabled()) {
        profile_presence->XUID(user_profile->GetOnlineXUID());
        profile_presence->RichPresence(user_profile->GetPresenceString());
      }

      presence->AddPresence(*profile_presence);
    }
  }

  std::string player_presence;
  bool valid = presence->Serialize(player_presence);
  assert_true(valid);

  const uint8_t* player_presence_data =
      reinterpret_cast<const uint8_t*>(player_presence.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, player_presence_data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("SetPresence error message: {}", response->Message());
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

std::vector<FriendPresenceObjectJSON> XLiveAPI::GetAllFriendsPresence(
    const uint32_t user_index) {
  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  auto offline_peer_presences = GetOfflineFriendsPresence(user_index);
  std::map<uint64_t, FriendPresenceObjectJSON> online_peer_presences = {};

  if (XLiveAPI::IsConnectedToServer()) {
    online_peer_presences = GetOnlineFriendsPresence(user_index);
  }

  auto& merged_peer_presences = online_peer_presences;

  merged_peer_presences.merge(offline_peer_presences);

  std::vector<FriendPresenceObjectJSON> peer_presences;

  std::ranges::transform(
      merged_peer_presences, std::back_inserter(peer_presences),
      &std::pair<const uint64_t, FriendPresenceObjectJSON>::second);

  std::sort(peer_presences.begin(), peer_presences.end(),
            [](const FriendPresenceObjectJSON& peer_1,
               FriendPresenceObjectJSON& peer_2) {
              uint32_t peer_1_state = peer_1.State() & 0xFF;
              uint32_t peer_2_state = peer_2.State() & 0xFF;

              if (peer_1_state == peer_2_state &&
                  (peer_1.SessionID() || peer_2.SessionID())) {
                if (peer_1.SessionID() && peer_2.SessionID()) {
                  return true;
                }

                return peer_1.SessionID() ? true : false;
              }

              return peer_1_state > peer_2_state;
            });

  return peer_presences;
}

std::map<uint64_t, FriendPresenceObjectJSON>
XLiveAPI::GetOfflineFriendsPresence(const uint32_t user_index) {
  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  std::map<uint64_t, FriendPresenceObjectJSON> peer_presences = {};

  for (uint32_t count = 1; const auto& xuid : profile->GetFriendsXUIDs()) {
    FriendPresenceObjectJSON peer = {};
    peer.Gamertag(std::format("Friend {}", count));
    peer.XUID(xuid);

    count++;
    peer_presences[xuid] = peer;
  }

  return peer_presences;
}

std::map<uint64_t, FriendPresenceObjectJSON> XLiveAPI::GetOnlineFriendsPresence(
    const uint32_t user_index) {
  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  std::map<uint64_t, FriendPresenceObjectJSON> peer_presences = {};

  const auto freinds_presence =
      XLiveAPI::GetFriendsPresence(profile->GetFriendsXUIDs())
          ->PlayersPresence();

  for (const auto& presence : freinds_presence) {
    peer_presences[presence.XUID()] = presence;
  }

  return peer_presences;
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
  size_t bytes_out =
      wcstombs(interface_name, adapter.FriendlyName, sizeof(interface_name));

  // Fallback to adapater GUID if name failed to convert
  if (bytes_out == -1) {
    strcpy(interface_name, adapter.AdapterName);
  }

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
          adapter_has_wan_routing =
              (local_ip.sin_addr.s_addr == adapter_addr.sin_addr.s_addr);
          local_ip_ = adapter_addr;
          OVERRIDE_string(network_guid, adapter.AdapterName);
          return true;
        }
      } else {
        adapter_has_wan_routing =
            local_ip.sin_addr.s_addr == adapter_addr.sin_addr.s_addr;
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

  std::string WAN_interface = xe::kernel::XLiveAPI::adapter_has_wan_routing
                                  ? "(Default)"
                                  : "(Non Default)";

  XELOGI("Set network interface: {} {} {} {}", interface_name,
         cvars::network_guid, LocalIP_str(), WAN_interface);

  assert_false(cvars::network_guid == "");
}
}  // namespace kernel
}  // namespace xe