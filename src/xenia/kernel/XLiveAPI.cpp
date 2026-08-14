/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/rapidcsv/src/rapidcsv.h"

// clang-format off
// We want to include platform.h first to define NOMINMAX to prevent window.h
// from defining the macros.
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
// clang-format on

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/base/utf8.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/friends_util.h"
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

DEFINE_bool(bind_interface, false,
            "Useful for network Tunnels/VPNs e.g. XLink Kai.", "Live");

DEFINE_bool(xstorage_backend, true,
            "Request XStorage content from backend and fallback locally, "
            "otherwise only use local content.",
            "Live");

DEFINE_bool(
    xstorage_user_data_backend, false,
    "Store user data on backend (not recommended), otherwise fallback locally.",
    "Live");

DEFINE_bool(xhttp, true, "Toggles XHTTP.", "Live");

DEFINE_int32(discord_presence_user_index, 0,
             "User profile index used for Discord rich presence [0, 3].",
             "Live");

using namespace rapidjson;

// TODO:
// libcurl + wolfssl + TLS Support
//
// Use the overlapped task for asynchronous curl requests.
// API endpoint lookup table

// https://patents.google.com/patent/US20060287099A1
namespace xe {
namespace kernel {

XLiveAPI::XLiveAPI() {
  if (cvars::network_mode == NETWORK_MODE::OFFLINE) {
    initialized_ = InitState::Failed;
  }

  if (cvars::logging) {
    PrintLibcurlDetails();
  }
}

XLiveAPI::~XLiveAPI() {
  // TODO(Adrian): Cleanup libcurl multiplexing handles.
}

void XLiveAPI::PrintLibcurlDetails() {
  curl_version_info_data* curl_info = curl_version_info(CURLVERSION_NOW);

  uint32_t major = (curl_info->version_num >> 16) & 0xFF;
  uint32_t minor = (curl_info->version_num >> 8) & 0xFF;
  uint32_t patch = curl_info->version_num & 0xFF;

  XELOGI("libcurl version {}.{}.{}", major, minor, patch);

  if (curl_info->features & CURL_VERSION_SSL) {
    XELOGI("SSL support: Yes");
  } else {
    assert_always();
    XELOGI("SSL support: No");
  }

  if (curl_info->features & CURL_VERSION_HTTP2) {
    XELOGI("HTTP/2 support: Yes");
  } else {
    XELOGI("HTTP/2 support: No");
  }
}

void XLiveAPI::IpGetConsoleXnAddr(XNADDR* XnAddr_ptr) {
  memset(XnAddr_ptr, 0, sizeof(XNADDR));

  const auto adapter_manager =
      kernel_state()->emulator()->GetNetworkAdapterManager();

  const bool is_WAN_routing = adapter_manager->IsSelectedAdapterWANRouting();
  const auto adapter_local_ip = adapter_manager->GetSelectedAdapterLocalIP();
  const auto xbl_api = kernel_state()->GetXboxLiveAPI();
  const auto user_tracker = kernel_state()->xam_state()->user_tracker();

  if (user_tracker->LoggedInToLive()) {
    XnAddr_ptr->ina = xbl_api->OnlineIP().sin_addr;
    XnAddr_ptr->inaOnline = xbl_api->OnlineIP().sin_addr;
  } else if (cvars::network_mode == NETWORK_MODE::LAN) {
    XnAddr_ptr->ina = adapter_local_ip.sin_addr;
  }

  if (kernel_state()->xam_state()->user_tracker()->LoggedInToLive()) {
    XnAddr_ptr->wPortOnline = xbl_api->GetPlayerPort();
  }

  XnAddr_ptr->abOnline.platform_type = PLATFORM_TYPE::Xbox360;

  memcpy(XnAddr_ptr->abEnet, GetConsoleMacAddress().raw(),
         MacAddress::MacAddressSize);
}

void XLiveAPI::GetXnAddrFromSessionObject(SessionObjectJSON session,
                                          XNADDR* XnAddr_ptr) {
  memset(XnAddr_ptr, 0, sizeof(XNADDR));

  XnAddr_ptr->inaOnline = ip_to_in_addr(session.HostAddress());
  XnAddr_ptr->ina = ip_to_in_addr(session.HostAddress());

  const MacAddress mac_address = MacAddress(session.MacAddress());
  memcpy(XnAddr_ptr->abEnet, mac_address.raw(), MacAddress::MacAddressSize);

  XnAddr_ptr->wPortOnline = session.Port();

  // 545407F2 will fail to join session if platform type does not match host's
  // platform type
  XnAddr_ptr->abOnline.platform_type = PLATFORM_TYPE::Xbox360;
}

std::vector<std::string> XLiveAPI::ParseAPIList() const {
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

void XLiveAPI::AddAPIAddress(std::string address) const {
  if (address.back() != '/') {
    address.push_back('/');
  }

  std::vector<std::string> api_addresses = ParseAPIList();

  auto it = std::find(api_addresses.begin(), api_addresses.end(), address);

  if (it == api_addresses.end()) {
    api_addresses.push_back(address);

    cvars::api_list = BuildCSVFromVector(api_addresses);
    OVERRIDE_string(api_list, cvars::api_list);
  }
}

void XLiveAPI::RemoveAPIAddress(std::string address) const {
  if (initialized_ != InitState::Pending) {
    return;
  }

  if (cvars::api_address == default_public_server_) {
    return;
  }

  std::vector<std::string> api_addresses = ParseAPIList();

  auto it = std::find(api_addresses.begin(), api_addresses.end(), address);

  if (it != api_addresses.end()) {
    api_addresses.erase(it);
  }

  cvars::api_list = BuildCSVFromVector(api_addresses);

  if (cvars::api_address == address) {
    OVERRIDE_string(api_address, default_public_server_);
  }

  OVERRIDE_string(api_list, cvars::api_list);
}

void XLiveAPI::SetAPIAddress(std::string address) {
  if (initialized_ == InitState::Pending) {
    OVERRIDE_string(api_address, address);
  }
}

void XLiveAPI::BroadcastNetworkStatus() const {
  switch (cvars::network_mode) {
    case xe::kernel::NETWORK_MODE::OFFLINE: {
      kernel_state()->BroadcastNotification(kXNotificationLiveConnectionChanged,
                                            X_ONLINE_S_LOGON_DISCONNECTED);

      kernel_state()->BroadcastNotification(kXNotificationLiveLinkStateChanged,
                                            0);
    } break;
    case xe::kernel::NETWORK_MODE::LAN: {
      kernel_state()->BroadcastNotification(kXNotificationLiveConnectionChanged,
                                            X_ONLINE_S_LOGON_DISCONNECTED);

      kernel_state()->BroadcastNotification(kXNotificationLiveLinkStateChanged,
                                            1);
    } break;
    case xe::kernel::NETWORK_MODE::XBOXLIVE: {
      kernel_state()->BroadcastNotification(
          kXNotificationLiveConnectionChanged,
          X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED);

      kernel_state()->BroadcastNotification(kXNotificationLiveLinkStateChanged,
                                            1);
    } break;
  }
}

void XLiveAPI::SetNetworkMode(uint32_t mode) const {
  OVERRIDE_int32(network_mode, mode);
}

bool XLiveAPI::SelectNetworkMode(uint32_t mode) {
  if (!kernel_state()->is_title_open()) {
    return true;
  }

  if (mode == NETWORK_MODE::OFFLINE) {
    cvars::network_mode = mode;

    DeleteAllSessionsByMac();

    initialized_ = InitState::Failed;
    online_ip_ = {};
    xlsp_servers_cached_ = false;
    qos_payload_cache_.clear();

    BroadcastNetworkStatus();

    return true;
  }

  // Don't automatically upgrade to Xbox-Live if LAN selected.
  bool lan_limit = mode == NETWORK_MODE::LAN;

  if (mode == NETWORK_MODE::XBOXLIVE) {
    StartWhoamiAsync();
  }

  RefreshNetworkMode(lan_limit);

  const bool switched_mode = cvars::network_mode == mode;

  if (switched_mode) {
    BroadcastNetworkStatus();
  }

  return switched_mode;
}

void XLiveAPI::SetLogging(bool state) const { OVERRIDE_bool(logging, state); }

void XLiveAPI::SetXHttp(bool state) const { OVERRIDE_bool(xhttp, state); }

void XLiveAPI::SetBindInterface(bool state) const {
  OVERRIDE_bool(bind_interface, state);
}

std::string XLiveAPI::GetApiAddress() {
  std::vector<std::string> api_addresses =
      ParseDelimitedList(cvars::api_address, 1);

  if (api_addresses.empty()) {
    cvars::api_address =
        kernel_state()->GetXboxLiveAPI()->GetDefaultLocalServer();
  } else {
    cvars::api_address = api_addresses.front();
  }

  // Add forward slash if not already added
  if (cvars::api_address.back() != '/') {
    cvars::api_address.push_back('/');
  }

  return cvars::api_address;
}

std::string XLiveAPI::BuildEndpoint(std::string endpoint) {
  return fmt::format("{}{}", GetApiAddress(), endpoint);
}

void XLiveAPI::Init() {
  if (GetInitState() != InitState::Pending) {
    return;
  }

  RefreshNetworkMode(false);

  if (!IsConnectedToServer()) {
    return;
  }

  // Download ports mappings before initializing UPnP.
  DownloadPortMappings();

  DownloadHostRedirects();

  // TODO(Adrian):
  // Netplay doesn't support multiple local profiles too well.
  // Only register user index 0 on backend for now to reduce issues.
  const uint32_t user_index = 0;
  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  if (profile) {
    std::unique_ptr<HTTPResponseObjectJSON> register_responce =
        RegisterPlayer(profile->xuid());

    // Add dummy friends here so we can use the title_id.
    kernel_state()->friends_manager()->AddDummyFriends(profile->xuid(),
                                                       dummy_friends_count_);
  }

  // Delete sessions on start-up.
  DeleteAllSessions();
}

NETWORK_MODE XLiveAPI::RefreshNetworkMode(bool lan_limit) {
  const bool is_initialized = initialized_ != InitState::Pending;

  const auto adapter_manager =
      kernel_state()->emulator()->GetNetworkAdapterManager();

  if (!adapter_manager->IsInterfaceSelected()) {
    XELOGI("XLiveAPI:: No interfaces found, enabling offline mode!");

    initialized_ = InitState::Failed;
    cvars::network_mode = NETWORK_MODE::OFFLINE;

    return static_cast<NETWORK_MODE>(cvars::network_mode);
  }

  if (!is_initialized && cvars::network_mode == NETWORK_MODE::OFFLINE) {
    XELOGI("XLiveAPI:: Offline mode enabled!");
    initialized_ = InitState::Failed;
    return static_cast<NETWORK_MODE>(cvars::network_mode);
  }

  if (!is_initialized && cvars::network_mode == NETWORK_MODE::LAN) {
    lan_limit = true;
  }

  // Using future so we don't block this function. This prevents blocking the
  // games thread during network initialization.
  if (whoami_result_.valid()) {
    online_ip_ = whoami_result_.get();
  }

  bool connected = false;

  // We don't need the online IP in LAN mode, instead just use heartbeat.
  // Server is needed for XNetQosLookup.
  if (lan_limit) {
    connected = Heartbeat();
  } else {
    connected = online_ip_.sin_addr.s_addr != 0;
  }

  if (connected) {
    initialized_ = InitState::Success;
  } else {
    initialized_ = InitState::Failed;
  }

  if (!IsConnectedToServer()) {
    // Assign online ip as local ip to ensure XNADDR is not 0 for systemlink
    // online_ip_ = local_ip_;

    cvars::network_mode = NETWORK_MODE::LAN;

    XELOGE("XLiveAPI:: Cannot reach API server.");
    initialized_ = InitState::Failed;
    return static_cast<NETWORK_MODE>(cvars::network_mode);
  }

  // We don't want to automatically upgrade to Xbox-Live.
  if (lan_limit) {
    cvars::network_mode = NETWORK_MODE::LAN;
  } else {
    cvars::network_mode = NETWORK_MODE::XBOXLIVE;
  }

  return static_cast<NETWORK_MODE>(cvars::network_mode);
}

XLiveAPI::InitState XLiveAPI::GetInitState() const { return initialized_; }

// If online NAT open, otherwise strict.
uint32_t XLiveAPI::GetNatType() const {
  return kernel_state()->xam_state()->user_tracker()->LoggedInToLive()
             ? X_NAT_TYPE::NAT_OPEN
             : X_NAT_TYPE::NAT_STRICT;
}

bool XLiveAPI::IsConnectedToServer() const {
  return initialized_ == InitState::Success;
}

uint16_t XLiveAPI::GetPlayerPort() const { return 36000; }

int8_t XLiveAPI::GetVersionStatus() const { return version_status_; }

void XLiveAPI::clearXnaddrCache() {
  sessionIdCache.clear();
  macAddressCache.clear();
}

// Request data from the server
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::Get(std::string endpoint,
                                                      uint32_t timeout) {
  response_data chunk = {};
  CURL* curl_handle = curl_easy_init();
  CURLcode result;

  if (!curl_handle) {
    XELOGE("XLiveAPI::Get: Cannot initialize CURL");
    return PraseResponse(chunk);
  }

  if (cvars::logging) {
    XELOGI("{} Endpoint: {}", __func__, endpoint);
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

  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint.c_str());
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

  if (cvars::logging) {
    XELOGI("{} Endpoint: {}", __func__, endpoint);
  }

  curl_slist* headers = NULL;

  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint.c_str());
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

  if (cvars::logging) {
    XELOGI("{} Endpoint: {}", __func__, endpoint);
  }

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");

  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint.c_str());

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

std::vector<HTTPResponseObjectJSON> XLiveAPI::GetMulti(
    std::vector<std::string> urls, uint32_t per_request_timeout) {
  CURLM* curl_multi_handle = curl_multi_init();
  CURLMcode result;

  if (!curl_multi_handle) {
    XELOGE(fmt::format("XLiveAPI::{}: Cannot initialize CURL", __func__));
    return {};
  }

  curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/octet-stream");

  if (headers == NULL) {
    return {};
  }

  curl_easy_setopt(curl_multi_handle, CURLOPT_HTTP_VERSION,
                   CURL_HTTP_VERSION_2_0);
  // curl_easy_setopt(curl_multi_handle, CURLOPT_PIPEWAIT, 1L);

  std::unordered_map<CURL*, std::unique_ptr<response_data>> tasks;

  std::unordered_map<CURL*, size_t> handle_to_index;

  for (size_t i = 0; i < urls.size(); ++i) {
    const std::string& url = urls[i];
    std::unique_ptr<response_data> task = std::make_unique<response_data>();

    CURL* curl_handle = curl_easy_init();

    if (per_request_timeout > 0) {
      curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, per_request_timeout);
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "xenia");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, task.get());

    curl_multi_add_handle(curl_multi_handle, curl_handle);

    tasks[curl_handle] = std::move(task);
    handle_to_index[curl_handle] = i;
  }

  int still_running = static_cast<int>(urls.size());
  while (still_running) {
    result = curl_multi_perform(curl_multi_handle, &still_running);
    if (still_running) {
      curl_multi_poll(curl_multi_handle, NULL, 0, 1000, NULL);
    }
  }

  std::vector<std::unique_ptr<HTTPResponseObjectJSON>> indexed_responses(
      urls.size());

  for (const auto& kv : tasks) {
    CURL* handle = kv.first;
    const std::unique_ptr<response_data>& task = kv.second;

    CURLcode curl_result =
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &task->http_code);

    if (curl_result != CURLE_OK) {
      continue;
    }

    const HTTP_STATUS_CODE http_code =
        static_cast<HTTP_STATUS_CODE>(task->http_code);

    if (http_code != HTTP_STATUS_CODE::HTTP_OK &&
        http_code != HTTP_STATUS_CODE::HTTP_NO_CONTENT) {
      XELOGE("XLiveAPI::{}: Failed! HTTP Error Code: {}", __func__,
             task->http_code);
    }

    std::unique_ptr<HTTPResponseObjectJSON> response = PraseResponse(*task);

    auto it = handle_to_index.find(handle);
    if (it != handle_to_index.end()) {
      size_t idx = it->second;
      if (idx < indexed_responses.size()) {
        indexed_responses[idx] = std::move(response);
      }
    }

    curl_multi_remove_handle(curl_multi_handle, handle);
    curl_easy_cleanup(handle);
  }

  tasks.clear();

  curl_multi_cleanup(curl_multi_handle);
  curl_slist_free_all(headers);

  std::vector<HTTPResponseObjectJSON> responses;
  responses.reserve(urls.size());

  for (size_t i = 0; i < indexed_responses.size(); ++i) {
    if (indexed_responses[i]) {
      responses.push_back(*indexed_responses[i]);
    } else {
      std::unique_ptr<HTTPResponseObjectJSON> empty_response =
          PraseResponse({});

      responses.push_back(*empty_response);
    }
  }

  return responses;
}

void XLiveAPI::StartWhoamiAsync() {
  whoami_result_ = std::async(std::launch::async, &XLiveAPI::Getwhoami, this);
}

// Check connection to xenia web server.
sockaddr_in XLiveAPI::Getwhoami() {
  std::unique_ptr<HTTPResponseObjectJSON> response =
      Get(BuildEndpoint("whoami"));

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
  std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/ports", kernel_state()->title_id()));

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    assert_always();
    return;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (!upnp) {
    return;
  }

  if (doc.HasMember("connect")) {
    for (const auto& port : doc["connect"].GetArray()) {
      upnp->AddMappedConnectPort(port["port"].GetUint(),
                                 port["mappedTo"].GetUint());
    }
  }

  if (doc.HasMember("bind")) {
    for (const auto& port : doc["bind"].GetArray()) {
      const auto upnp = kernel_state()->emulator()->GetUPnP();
      upnp->AddMappedBindPort(port["port"].GetUint(),
                              port["mappedTo"].GetUint());
    }
  }

  XELOGI("Requested Port Mappings");
  return;
}

void XLiveAPI::DownloadHostRedirects() {
  const std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/hosts", kernel_state()->title_id()));

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("DownloadHostRedirects error message: {}", response->Message());
    return;
  }

  Document doc;
  doc.Parse(response->RawResponse().response);

  if (!doc.IsObject()) {
    return;
  }

  for (const auto& redirect : doc.GetObj()) {
    if (!redirect.value.IsString()) {
      continue;
    }

    host_redirects_[xe::utf8::lower_ascii(redirect.name.GetString())] =
        redirect.value.GetString();
  }

  XELOGI("Requested {} Host Redirects", host_redirects_.size());
  return;
}

// Register player on the backend
// xuid + ip + mac = unique player on a network
std::unique_ptr<HTTPResponseObjectJSON> XLiveAPI::RegisterPlayer(
    uint64_t xuid) {
  std::unique_ptr<HTTPResponseObjectJSON> response = {};

  const auto user_profile = kernel_state()->xam_state()->GetUserProfile(xuid);

  if (!user_profile) {
    XELOGE("Cancelled registering profile, profile not signed in!");
    return response;
  }

  if (GetConsoleMacAddress().to_uint64() == 0) {
    XELOGE("Cancelled registering profile!");
    return response;
  }

  if (kernel_state()->xam_state()->user_tracker()->LoggedInToLive() &&
      !user_profile->IsLiveEnabled()) {
    XELOGE("Cancelled registering profile, profile is not live enabled!");
    return response;
  }

  uint64_t registered_xuid = user_profile->GetOnlineXUID();

  // Register offline profile for systemlink usage
  if (cvars::network_mode == NETWORK_MODE::LAN &&
      !user_profile->IsLiveEnabled()) {
    registered_xuid = user_profile->xuid();

    XELOGI("Registering offline profile {:016X} for systemlink usage",
           registered_xuid);
  }

  std::map<uint32_t, std::vector<xam::UserSetting>> settings;

  const auto dashboard_settings =
      kernel_state()->xam_state()->user_tracker()->GetSettingIds(user_profile,
                                                                 kDashboardID);

  const auto title_settings =
      kernel_state()->xam_state()->user_tracker()->GetSettingIds(
          user_profile, kernel_state()->title_id());

  for (const xam::UserSettingId setting_id : dashboard_settings) {
    const auto setting =
        kernel_state()->xam_state()->user_tracker()->GetSetting(
            user_profile, kDashboardID, static_cast<uint32_t>(setting_id));

    if (setting.has_value()) {
      settings[kDashboardID].push_back(setting.value());
    }
  }

  for (const xam::UserSettingId setting_id : title_settings) {
    const uint32_t title_id = kernel_state()->title_id();

    const auto setting =
        kernel_state()->xam_state()->user_tracker()->GetSetting(
            user_profile, title_id, static_cast<uint32_t>(setting_id));

    if (setting.has_value()) {
      settings[title_id].push_back(setting.value());
    }
  }

  PlayerObjectJSON player = PlayerObjectJSON();

  MacAddress mac_address = GetConsoleMacAddress();

  player.XUID(registered_xuid);
  player.Gamertag(user_profile->name());
  player.MachineID(GetLocalMachineId(mac_address));
  player.HostAddress(OnlineIP_str());
  player.MacAddress(mac_address.to_uint64());
  player.Settings(settings);

  std::string player_output;
  bool valid = player.Serialize(player_output);
  assert_true(valid);

  std::string endpoint = BuildEndpoint("players");

  const uint8_t* player_register =
      reinterpret_cast<const uint8_t*>(player_output.c_str());

  response = Post(endpoint, player_register);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    assert_always();
    return response;
  }

  XELOGI("POST Success");

  auto player_lookup = FindPlayer(OnlineIP_str());

  // Check for erroneous profile lookup
  if (player_lookup->XUID() != player.XUID()) {
    XELOGI("XLiveAPI:: {} XUID mismatch!", player.Gamertag());
    xuid_mismatch_ = true;

    // assert_always();
  } else {
    xuid_mismatch_ = false;
  }

  return response;
}

const std::map<uint64_t, std::string> XLiveAPI::DeleteMyProfiles() {
  std::string endpoint = BuildEndpoint("players/deletemyprofiles");

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (!response->RawResponse().response) {
    return {};
  }

  const auto deleted_profiles =
      response->Deserialize<DeleteMyProfilesObjectJSON>();

  return deleted_profiles->GetDeletedProfiles();
}

// Request clients player info via IP address
std::unique_ptr<PlayerObjectJSON> XLiveAPI::FindPlayer(std::string ip) {
  std::unique_ptr<PlayerObjectJSON> player =
      std::make_unique<PlayerObjectJSON>();

  Document doc;
  doc.SetObject();
  doc.AddMember("hostAddress", ip, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  const uint8_t* find_players_data =
      reinterpret_cast<const uint8_t*>(buffer.GetString());

  // POST & receive.
  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(BuildEndpoint("players/find"), find_players_data);

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
  if (qos_payload_cache_[sessionId] != qos_payload) {
    qos_payload_cache_[sessionId] = qos_payload;

    XELOGI("Updated QoS Cache.");
    return true;
  }

  return false;
}

// Send QoS binary data to the server
void XLiveAPI::QoSPost(uint64_t sessionId, uint8_t* qosData, size_t qosLength) {
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/qos",
                                kernel_state()->title_id(), sessionId));

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
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/qos",
                                kernel_state()->title_id(), sessionId));

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
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/modify",
                                kernel_state()->title_id(), sessionId));

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

  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/search", title_id));

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
  std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/sessions/search", kernel_state()->title_id()));

  const auto user_profile =
      kernel_state()->xam_state()->GetUserProfile(data->user_index);

  Document doc;
  doc.SetObject();

  doc.AddMember("searchIndex", data->proc_index, doc.GetAllocator());
  doc.AddMember("resultsCount", data->num_results, doc.GetAllocator());
  doc.AddMember("numUsers", num_users, doc.GetAllocator());

  const xam::XUSER_CONTEXT* contexts_ptr =
      kernel_memory()->TranslateVirtual<xam::XUSER_CONTEXT*>(data->ctx_ptr);

  const xam::XUSER_PROPERTY* properties_ptr =
      kernel_memory()->TranslateVirtual<xam::XUSER_PROPERTY*>(data->props_ptr);

  std::vector<xam::XUSER_CONTEXT> guest_contexts(contexts_ptr,
                                                 contexts_ptr + data->num_ctx);

  std::vector<xam::XUSER_PROPERTY> guest_properties(
      properties_ptr, properties_ptr + data->num_props);

  std::vector<xam::Property> property_filters;
  std::vector<std::string> serialized_property_filters;

  for (const auto& guest_context : guest_contexts) {
    const xam::Property context(guest_context.context_id, guest_context.value);

    if (context.IsContext()) {
      property_filters.push_back(context);
    }
  }

  for (auto& guest_property : guest_properties) {
    const xam::Property property(
        guest_property.property_id.get(), sizeof(xam::X_USER_DATA),
        reinterpret_cast<uint8_t*>(&guest_property.data.data));

    if (!property.IsContext()) {
      property_filters.push_back(property);
    }
  }

  for (const auto& property : property_filters) {
    const auto serialize_prop = property.SerializeToBase64();

    if (serialize_prop.has_value()) {
      serialized_property_filters.push_back(serialize_prop.value());
    }
  }

  // Filter own sessions from search.
  if (user_profile) {
    const std::string searcher_xuid_str =
        fmt::format("{:016X}", user_profile->GetOnlineXUID());

    doc.AddMember("searcher_xuid", searcher_xuid_str, doc.GetAllocator());
  }

  Value properties_array(kArrayType);

  for (const auto& serialized_property : serialized_property_filters) {
    Value serialized_value(serialized_property, doc.GetAllocator());

    properties_array.PushBack(serialized_value.Move(), doc.GetAllocator());
  }

  doc.AddMember("filters", properties_array, doc.GetAllocator());

  rapidjson::StringBuffer buffer;
  PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, reinterpret_cast<const uint8_t*>(buffer.GetString()));

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
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/details",
                                kernel_state()->title_id(), sessionId));

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
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/migrate",
                                kernel_state()->title_id(), sessionId));

  Document doc;
  doc.SetObject();

  uint64_t xuid = 0;

  const auto profile =
      kernel_state()->xam_state()->GetUserProfile(data->user_index);

  if (profile) {
    xuid = profile->GetOnlineXUID();
  } else {
    XELOGI("New host is remote.");
  }

  const std::string xuid_str = fmt::format("{:016X}", xuid);

  doc.AddMember("xuid", xuid_str, doc.GetAllocator());
  doc.AddMember("hostAddress", OnlineIP_str(), doc.GetAllocator());
  doc.AddMember("macAddress", GetConsoleMacAddress().to_string(),
                doc.GetAllocator());
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

  XELOGI("Sent XSessionMigration data.");

  return session;
}

std::unique_ptr<ArbitrationObjectJSON> XLiveAPI::XSessionArbitration(
    uint64_t sessionId) {
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/arbitration",
                                kernel_state()->title_id(), sessionId));

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

bool XLiveAPI::SessionFlushStats(uint64_t sessionId,
                                 view_properties_unordered_map stats) {
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/leaderboards",
                                kernel_state()->title_id(), sessionId));

  if (stats.empty()) {
    return true;
  }

  LeaderboardObjectJSON leaderboard = LeaderboardObjectJSON(stats);

  std::string output;
  bool valid = leaderboard.Serialize(output);
  assert_true(valid);

  if (cvars::logging) {
    XELOGI("{}:\n\n{}", __func__, output);
  }

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, (uint8_t*)output.c_str());

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("{} error message: {}", __func__, response->Message());
    // assert_always();

    return false;
  }

  return true;
}

std::unique_ptr<LeaderboardObjectJSON> XLiveAPI::LeaderboardsFind(
    const XGI_XUSER_READ_STATS stats) {
  std::string endpoint = BuildEndpoint(fmt::format("leaderboards/find"));

  auto read_stats = ReadUserStatsObjectJSON(stats);

  std::string read_user_stats_json = "";
  bool valid = read_stats.Serialize(read_user_stats_json);
  assert_true(valid);

  std::unique_ptr<LeaderboardObjectJSON> leaderboards =
      std::make_unique<LeaderboardObjectJSON>();

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, reinterpret_cast<uint8_t*>(read_user_stats_json.data()));

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("LeaderboardsFind error message: {}", response->Message());
    assert_always();

    return leaderboards;
  }

  leaderboards = response->Deserialize<LeaderboardObjectJSON>();

  return leaderboards;
}

void XLiveAPI::DeleteSession(uint64_t sessionId) {
  std::string endpoint = BuildEndpoint(fmt::format(
      "title/{:08X}/sessions/{:016x}", kernel_state()->title_id(), sessionId));

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete session {:08X}", sessionId);
    XELOGE("DeleteSession error message: {}", response->Message());
    // assert_always();
  }

  clearXnaddrCache();
  qos_payload_cache_.erase(sessionId);
}

void XLiveAPI::DeleteAllSessionsByMac() {
  const std::string endpoint = BuildEndpoint(
      fmt::format("DeleteSessions/{}", GetConsoleMacAddress().to_string()));

  // Since we usually delete on close, we don't want to block main thread on
  // close.
  if (!IsConnectedToServer()) {
    return;
  }

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete all sessions");
  }
}

void XLiveAPI::DeleteAllSessions() {
  const std::string endpoint = BuildEndpoint(fmt::format("DeleteSessions"));

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("Failed to delete all sessions");
  }
}

void XLiveAPI::XSessionCreate(uint64_t sessionId, XGI_SESSION_CREATE* data) {
  std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/sessions", kernel_state()->title_id()));

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

  const auto xlast =
      kernel_state()->emulator()->game_info_database()->GetXLast();

  // Technically we could just send the matchmaking query instead of complete
  // XLast source.
  std::optional<std::string> xlast_source_base64;

  if (xlast) {
    xlast_source_base64 = xlast->SerializeSourceToBase64();
  }

  SessionObjectJSON session;

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
  session.MacAddress(GetConsoleMacAddress().to_string());
  session.Port(GetPlayerPort());

  if (xlast_source_base64.has_value()) {
    session.XLast(xlast_source_base64.value());
  }

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

// 4D5308AB doesn't contain XPROPERTY_GAMER_HOSTNAME in XMAT but nevertheless is
// required to discover sessions.
// Include XPROPERTY_GAMER_PUID anyway, it's useful information to have on the
// backend.
const std::set<uint32_t> default_system_matchmaking_properties = {
    XPROPERTY_GAMER_PUID, XPROPERTY_GAMER_HOSTNAME};

bool XLiveAPI::SessionPropertiesSet(uint64_t session_id, uint64_t xuid) {
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/properties",
                                kernel_state()->title_id(), session_id));

  std::unique_ptr<PropertiesObjectJSON> properties_json =
      std::make_unique<PropertiesObjectJSON>();

  const auto user_profile = kernel_state()->xam_state()->GetUserProfile(xuid);

  const auto propertie_ids =
      kernel_state()->xam_state()->user_tracker()->GetUserPropertyIds(
          user_profile->xuid());

  std::vector<xam::Property> properties = {};

  // XMAT Filtering:
  // Only send properties that are used in matchmaking queries.
  //
  // This prevents 4D5307D5 trying to set property XPROPERTY_GAMER_MU and
  // XPROPERTY_GAMER_SIGMA without data_address in XGIUserSetPropertyEx when
  // joining a session via custom search.
  //
  // 4E4D07DC will sometimes fail to find friends sessions, filtering
  // properties by XMAT fixes session discovery inconsistency.
  //
  // 545107D4 doesn't contain many system matchmaking properties in SPA.
  for (const auto& property_attribute : propertie_ids) {
    const auto property =
        kernel_state()->emulator()->game_info_database()->GetProperty(
            property_attribute.value);

    // 545107D4
    if (!property.has_value()) {
      XELOGI("{}: Property {:08X} not found in SPA!", __func__,
             property_attribute.value);
    }

    if ((property.has_value() && property->is_matchmaking) ||
        default_system_matchmaking_properties.contains(
            property_attribute.value)) {
      const xam::Property* property =
          kernel_state()->xam_state()->user_tracker()->GetProperty(
              user_profile->xuid(), property_attribute.value);

      if (property) {
        properties.push_back(*property);
      } else {
        XELOGI("{}: Property {:08X} is unset!", __func__,
               property_attribute.value);
      }
    }
  }

  const auto contexts_ids =
      kernel_state()->xam_state()->user_tracker()->GetUserContextIds(
          user_profile->xuid());

  for (const auto& context_attribute : contexts_ids) {
    const auto context_property =
        kernel_state()->emulator()->game_info_database()->GetContext(
            context_attribute.value);

    if (context_property.has_value()) {
      if (context_property->is_matchmaking) {
        const xam::Property* property =
            kernel_state()->xam_state()->user_tracker()->GetProperty(
                user_profile->xuid(), context_attribute.value);

        if (property) {
          properties.push_back(*property);
        } else {
          XELOGI("{}: Context {:08X} is unset!", __func__,
                 context_attribute.value);
        }
      }
    } else {
      XELOGI("{}: Context {:08X} not found in SPA!", __func__,
             context_attribute.value);
    }
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
    return false;
  }

  return true;
}

// Ordered Properties
const std::vector<xam::Property> XLiveAPI::SessionPropertiesGet(
    uint64_t session_id, uint32_t query_id) {
  std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/sessions/{:016x}/properties/{}",
                  kernel_state()->title_id(), session_id, query_id));

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("SessionPropertiesGet error message: {}", response->Message());
    assert_always();

    return {};
  }

  const auto properties = response->Deserialize<PropertiesObjectJSON>();

  return properties->Properties();
}

SessionObjectJSON XLiveAPI::XSessionGet(uint64_t sessionId) {
  std::string endpoint = BuildEndpoint(fmt::format(
      "title/{:08X}/sessions/{:016x}", kernel_state()->title_id(), sessionId));

  std::unique_ptr<SessionObjectJSON> session =
      std::make_unique<SessionObjectJSON>();

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XSessionGet error message: {}", response->Message());
    assert_always();

    return *session;
  }

  session = response->Deserialize<SessionObjectJSON>();

  return *session;
}

std::vector<X_TITLE_SERVER> XLiveAPI::GetServers() {
  std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/servers", kernel_state()->title_id()));

  if (!kernel_state()->xam_state()->user_tracker()->LoggedInToLive()) {
    return {};
  }

  if (xlsp_servers_cached_) {
    return xlsp_servers_;
  }

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("GetServers error message: {}", response->Message());
    assert_always();

    return xlsp_servers_;
  }

  xlsp_servers_cached_ = true;

  Document doc;
  doc.Parse(response->RawResponse().response);

  for (const auto& server_data : doc.GetArray()) {
    X_TITLE_SERVER server{};

    server.server_address = ip_to_in_addr(server_data["address"].GetString());

    server.flags = server_data["flags"].GetInt();

    std::string description = server_data["description"].GetString();

    xe::string_util::copy_truncating(server.server_description,
                                     description.c_str(),
                                     sizeof(server.server_description));

    xlsp_servers_.push_back(server);
  }

  return xlsp_servers_;
}

// Hostnames the title reaches over XHTTP and the addresses they are redirected
// to (src/titles/<TITLEID>/hosts.json on the backend).
std::string XLiveAPI::GetHostRedirect(const std::string& host) {
  const auto redirect = host_redirects_.find(xe::utf8::lower_ascii(host));

  if (redirect == host_redirects_.end()) {
    return "";
  }

  return redirect->second;
}

std::unique_ptr<ServicesObjectJSON> XLiveAPI::GetServices() {
  std::string endpoint = BuildEndpoint(
      fmt::format("title/{:08X}/services", kernel_state()->title_id()));

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  std::unique_ptr<ServicesObjectJSON> services =
      std::make_unique<ServicesObjectJSON>();

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("GetServices error message: {}", response->Message());
    assert_always();

    return services;
  }

  services = response->Deserialize<ServicesObjectJSON>();

  return services;
}

bool XLiveAPI::Heartbeat() const {
  CURL* curl = curl_easy_init();

  if (!curl) {
    return false;
  }

  std::string endpoint = GetApiAddress();
  bool accessible = false;

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);

  CURLcode result = curl_easy_perform(curl);

  if (result == CURLE_OK) {
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code >= HTTP_STATUS_CODE::HTTP_OK &&
        response_code < HTTP_STATUS_CODE::HTTP_BAD_REQUEST) {
      accessible = true;
    }
  }

  curl_easy_cleanup(curl);

  return accessible;
}

void XLiveAPI::SessionJoinRemote(uint64_t sessionId,
                                 std::unordered_map<uint64_t, bool> members) {
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/join",
                                kernel_state()->title_id(), sessionId));

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
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016x}/leave",
                                kernel_state()->title_id(), sessionId));

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
  std::string endpoint =
      BuildEndpoint(fmt::format("title/{:08X}/sessions/{:016X}/prejoin",
                                kernel_state()->title_id(), sessionId));

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
    const std::set<uint64_t>& xuids) {
  const std::string endpoint = BuildEndpoint("players/presence");

  std::unique_ptr<FriendsPresenceObjectJSON> friends =
      std::make_unique<FriendsPresenceObjectJSON>();

  if (xuids.empty()) {
    return friends;
  }

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
  std::string endpoint = server_path;

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
  std::string endpoint = server_path;

  std::unique_ptr<HTTPResponseObjectJSON> response = Delete(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("XStorageDelete: {}", response->Message());
    assert_always();

    return false;
  }

  return true;
}

std::vector<uint8_t> XLiveAPI::XStorageDownload(std::string server_path) {
  std::string endpoint = server_path;

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  std::vector<uint8_t> buffer = {};

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK &&
      response->StatusCode() != HTTP_STATUS_CODE::HTTP_NO_CONTENT) {
    XELOGE("XStorageDownload: {}", response->Message());
    assert_always();

    return buffer;
  }

  if (response->RawResponse().response) {
    const uint32_t size = static_cast<uint32_t>(response->RawResponse().size);
    const uint8_t* downloaded_data =
        reinterpret_cast<const uint8_t*>(response->RawResponse().response);

    buffer = std::vector<uint8_t>(downloaded_data, downloaded_data + size);
  }

  return buffer;
}

X_STORAGE_UPLOAD_RESULT XLiveAPI::XStorageUpload(std::string server_path,
                                                 std::span<uint8_t> buffer) {
  std::string endpoint = server_path;

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
  CURL* curl = curl_easy_init();

  if (!curl) {
    return {};
  }

  char* encoded_url = curl_easy_escape(curl, server_path.c_str(),
                                       static_cast<int>(server_path.size()));

  if (!encoded_url) {
    return {};
  }

  std::string endpoint =
      BuildEndpoint(fmt::format("xstorage/enumerate/{}", encoded_url));

  curl_free(encoded_url);
  curl_easy_cleanup(curl);

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
  const std::string endpoint = BuildEndpoint("players/findusers");

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

PresenceObjectJSON XLiveAPI::BuildRichPresenceRequest(
    std::set<uint64_t> xuids) {
  PresenceObjectJSON presence = {};

  for (const auto& xuid : xuids) {
    const auto user_profile =
        kernel_state()->xam_state()->GetUserProfileAny(xuid);

    if (!user_profile) {
      continue;
    }

    if (!user_profile->IsLiveEnabled()) {
      continue;
    }

    FriendPresenceObjectJSON profile_presence = {};

    profile_presence.XUID(user_profile->GetOnlineXUID());
    profile_presence.RichPresence(user_profile->GetPresenceString());

    presence.AddPresence(profile_presence);
  }

  return presence;
}

void XLiveAPI::SetPresence(std::set<uint64_t> xuids) {
  const std::string endpoint = BuildEndpoint("players/setpresence");

  std::string player_presence;
  bool valid = BuildRichPresenceRequest(xuids).Serialize(player_presence);
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

bool XLiveAPI::SetUsersSettings(user_settingids_map settings) {
  const std::string endpoint = BuildEndpoint("players/setsettings");

  user_settings_map users_settings = {};

  for (const auto& [xuid, titles] : settings) {
    for (const auto& [title_id, settings] : titles) {
      for (const auto& setting_id : settings) {
        const auto user_profile =
            kernel_state()->xam_state()->GetUserProfileAny(xuid);

        if (const auto setting =
                kernel_state()->xam_state()->user_tracker()->GetSetting(
                    user_profile, title_id,
                    static_cast<uint32_t>(setting_id))) {
          if (setting.has_value()) {
            users_settings[xuid][title_id].push_back(setting.value());
          }
        }
      }
    }
  }

  SetUserSettingsObjectJSON user_settingsObj = {};
  user_settingsObj.Settings(users_settings);

  std::string user_settings_json;
  bool valid = user_settingsObj.Serialize(user_settings_json);
  assert_true(valid);

  const uint8_t* user_settings_data =
      reinterpret_cast<const uint8_t*>(user_settings_json.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, user_settings_data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("{} error message: {}", __func__, response->Message());
    assert_always();

    return false;
  }

  return true;
}

user_settings_map XLiveAPI::GetUsersSettings(user_settingids_map settings) {
  const std::string endpoint = BuildEndpoint("players/getsettings");

  GetUserSettingsObjectJSON user_settingsObj = {};
  user_settingsObj.SettingIds(settings);

  std::string user_settings_json;
  bool valid = user_settingsObj.Serialize(user_settings_json);
  assert_true(valid);

  const uint8_t* user_settings_data =
      reinterpret_cast<const uint8_t*>(user_settings_json.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response =
      Post(endpoint, user_settings_data);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
    XELOGE("{} error message: {}", __func__, response->Message());
    // assert_always();

    return {};
  }

  auto result = response->Deserialize<GetUserSettingsObjectJSON>();

  return result->Settings();
}

std::vector<uint8_t> XLiveAPI::GetUserGamerpicTile(uint64_t xuid,
                                                   bool small_tile) {
  user_settingids_map settings_ids = {};

  settings_ids[xuid][xe::kernel::kDashboardID].push_back(
      xam::UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY);

  const auto settings = GetUsersSettings(settings_ids);

  uint32_t setting_id =
      static_cast<uint32_t>(xam::UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY);

  bool has_gamerpic_key = false;

  if (settings.contains(xuid)) {
    if (settings.at(xuid).contains(xe::kernel::kDashboardID)) {
      for (const auto& setting :
           settings.at(xuid).at(xe::kernel::kDashboardID)) {
        if (setting.get_setting_id() == setting_id) {
          has_gamerpic_key = true;
        }
      }
    }
  }

  if (!has_gamerpic_key) {
    return {};
  }

  xam::UserSetting gamerpic_setting =
      settings.at(xuid).at(xe::kernel::kDashboardID).front();

  std::string gamerpic_key_data =
      xe::to_utf8(std::get<std::u16string>(gamerpic_setting.get_host_data()));

  const xam::GamerPictureKey gamerpic_key =
      *reinterpret_cast<const xam::GamerPictureKey*>(gamerpic_key_data.c_str());

  std::vector<uint8_t> gamerpic = {};

  uint32_t tile_id = gamerpic_key.GetBigTileId();

  if (small_tile) {
    tile_id = gamerpic_key.GetSmallTileId();
  }

  gamerpic = DownloadGamerpicTile(gamerpic_key.GetTitleId(), tile_id);

  return gamerpic;
}

TitleGamerpicsObjectJSON XLiveAPI::GetTitleGamerpic(uint32_t title_id) {
  const std::string endpoint =
      fmt::format("https://xboxgamer.pics/api/title/{:08x}", title_id);

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("{} error message: {}", __func__, response->Message());
    // assert_always();

    return {};
  }

  return *response->Deserialize<TitleGamerpicsObjectJSON>();
}

std::set<uint32_t> XLiveAPI::GetSupportedGamerpicTitles() {
  const std::string endpoint = "https://xboxgamer.pics/api/idlist";

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("{} error message: {}", __func__, response->Message());
    // assert_always();

    return {};
  }

  std::set<uint32_t> supported_titles = {};

  Document document;
  document.Parse(response->RawResponse().response);

  if (document.IsArray()) {
    for (const auto& title_id_str : document.GetArray()) {
      if (title_id_str.IsString()) {
        uint32_t title_id =
            string_util::from_string<uint32_t>(title_id_str.GetString(), true);

        supported_titles.insert(title_id);
      }
    }
  }

  return supported_titles;
}

std::optional<PageGamerpicsObjectJSON> XLiveAPI::GetGamerpicPage(
    uint32_t page, uint32_t per_page, std::string type_query) {
  const std::string endpoint = fmt::format(
      "https://xboxgamer.pics/api/titles?page={}&per_page={}&type={}", page,
      per_page, type_query.c_str());

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("{} error message: {}", __func__, response->Message());
    assert_always();

    return std::nullopt;
  }

  return *response->Deserialize<PageGamerpicsObjectJSON>();
}

std::map<uint32_t, std::vector<uint8_t>> XLiveAPI::GetMultiGameInfo(
    std::unordered_map<uint32_t, std::string> images_data) {
  if (images_data.empty()) {
    return {};
  }

  std::vector<std::string> urls = {};

  for (const auto& [title_id, image] : images_data) {
    urls.push_back(fmt::format("https://assets.xboxgamer.pics/titles/{:x}/{}",
                               title_id, image));
  }

  std::vector<HTTPResponseObjectJSON> games_info = GetMulti(urls, 5);

  std::map<uint32_t, std::vector<uint8_t>> images = {};

  if (games_info.size() != images_data.size()) {
    assert_always();
    return images;
  }

  // Requires the responses to be in order
  for (uint32_t i = 0; const auto& [title_id, image] : images_data) {
    const auto& game_info = games_info[i];

    if (game_info.RawResponse().response) {
      const uint32_t size = static_cast<uint32_t>(game_info.RawResponse().size);
      const uint8_t* downloaded_data =
          reinterpret_cast<const uint8_t*>(game_info.RawResponse().response);

      images[title_id] =
          std::vector<uint8_t>(downloaded_data, downloaded_data + size);
    }

    i++;
  }

  return images;
}

std::map<uint32_t, std::vector<uint8_t>> XLiveAPI::GetMultiGamerpics(
    std::vector<std::string> cdn_parts) {
  if (cdn_parts.empty()) {
    return {};
  }

  std::vector<std::string> urls = {};

  for (const auto& cdn : cdn_parts) {
    urls.push_back(fmt::format("https://assets.xboxgamer.pics{}", cdn));
  }

  std::vector<HTTPResponseObjectJSON> gamerpics_data = GetMulti(urls, 5);

  std::map<std::uint32_t, std::vector<uint8_t>> gamerpics = {};

  if (gamerpics_data.size() != cdn_parts.size()) {
    assert_always();
    return gamerpics;
  }

  // Requires the responses to be in order
  for (uint32_t i = 0; const auto& cdn : cdn_parts) {
    const auto& gamerpic = gamerpics_data[i];

    if (gamerpic.RawResponse().response) {
      const uint32_t size = static_cast<uint32_t>(gamerpic.RawResponse().size);
      const uint8_t* downloaded_data =
          reinterpret_cast<const uint8_t*>(gamerpic.RawResponse().response);

      std::string gamerpic_id_str = std::filesystem::path(cdn).stem().string();

      uint32_t tile_id =
          string_util::from_string<uint32_t>(gamerpic_id_str, true);

      gamerpics[tile_id] =
          std::vector<uint8_t>(downloaded_data, downloaded_data + size);
    }

    i++;
  }

  return gamerpics;
}

std::vector<uint8_t> XLiveAPI::DownloadGamerpicTile(uint32_t title_id,
                                                    uint32_t tile_id) {
  const std::string gamerpic_url = fmt::format(
      "https://assets.xboxgamer.pics/titles/{:x}/{:x}.png", title_id, tile_id);

  std::vector<uint8_t> tile = XStorageDownload(gamerpic_url);

  return tile;
}

std::future<std::vector<uint8_t>> XLiveAPI::DownloadGamerpicTileAsync(
    uint32_t title_id, uint32_t tile_id) {
  auto gamerpic = std::async(std::launch::async, [this, title_id, tile_id]() {
    return DownloadGamerpicTile(title_id, tile_id);
  });

  return gamerpic;
}

std::map<uint64_t, std::vector<uint8_t>> XLiveAPI::GetMultiGamerpicsFromXUIDs(
    std::set<uint64_t> xuids, bool fsmall) {
  user_settingids_map remote_user_setting_ids = {};

  for (const auto& xuid : xuids) {
    remote_user_setting_ids[xuid][kDashboardID].push_back(
        xam::UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY);
  }

  const auto remote_user_settings = GetUsersSettings(remote_user_setting_ids);

  std::map<uint64_t, uint32_t> remote_users_tile = {};

  std::vector<std::string> cdn_parts = {};

  for (const auto& [xuid, title_ids] : remote_user_settings) {
    for (const auto& [title_id, settings] : title_ids) {
      for (const auto& setting : settings) {
        if (setting.get_setting_id() ==
            static_cast<uint32_t>(
                xam::UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY)) {
          const xam::GamerPictureKey gamerpic_key =
              *reinterpret_cast<const xam::GamerPictureKey*>(
                  xe::to_utf8(std::get<std::u16string>(setting.get_host_data()))
                      .c_str());

          const uint32_t tile_id = fsmall ? gamerpic_key.GetSmallTileId()
                                          : gamerpic_key.GetBigTileId();

          const std::string gamerpic_cdn = fmt::format(
              "/titles/{:x}/{:x}.png", gamerpic_key.GetTitleId(), tile_id);

          cdn_parts.push_back(gamerpic_cdn);

          remote_users_tile[xuid] = tile_id;
        }
      }
    }
  }

  const auto gamerpics_data = GetMultiGamerpics(cdn_parts);

  std::map<uint64_t, std::vector<uint8_t>> gamerpics = {};

  for (const auto& [xuid, tile_id] : remote_users_tile) {
    if (gamerpics_data.contains(tile_id)) {
      gamerpics[xuid] = gamerpics_data.at(tile_id);
    }
  }

  return gamerpics;
}

std::shared_future<gamerpics_pair> XLiveAPI::DownloadCompleteGamerpic(
    xam::GamerPictureKey gamerpic_key) {
  auto gamerpic = std::async(std::launch::async, [this, gamerpic_key]() {
    std::vector<std::string> cdn_parts = {};

    const std::string big_gamerpic_cdn =
        fmt::format("/titles/{:x}/{:x}.png", gamerpic_key.GetTitleId(),
                    gamerpic_key.GetBigTileId());
    const std::string small_gamerpic_cdn =
        fmt::format("/titles/{:x}/{:x}.png", gamerpic_key.GetTitleId(),
                    gamerpic_key.GetSmallTileId());

    cdn_parts.push_back(big_gamerpic_cdn);
    cdn_parts.push_back(small_gamerpic_cdn);

    const auto gamerpics_data = GetMultiGamerpics(cdn_parts);

    gamerpics_pair gamerpics;

    if (!gamerpics_data.contains(gamerpic_key.GetBigTileId()) ||
        !gamerpics_data.contains(gamerpic_key.GetSmallTileId())) {
      return gamerpics;
    }

    const auto& big_gamerpic = gamerpics_data.at(gamerpic_key.GetBigTileId());
    const auto& small_gamerpic =
        gamerpics_data.at(gamerpic_key.GetSmallTileId());

    gamerpics = {big_gamerpic, small_gamerpic};

    return gamerpics;
  });

  return gamerpic.share();
}

std::vector<uint8_t> XLiveAPI::DownloadRandomGamerpic() {
  const std::string endpoint =
      fmt::format("https://xboxgamer.pics/api/random/gamerpics?count=1");

  std::unique_ptr<HTTPResponseObjectJSON> response = Get(endpoint);

  if (response->StatusCode() != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGE("{} error message: {}", __func__, response->Message());
    assert_always();

    return {};
  }

  std::string gamerpic_path;

  Document document;
  document.Parse(response->RawResponse().response);

  if (document.IsArray()) {
    const auto gamerpic = document.GetArray();

    if (gamerpic.Size() > 0) {
      gamerpic_path = gamerpic[0].GetString();
    }
  }

  if (gamerpic_path.empty()) {
    return {};
  }

  const std::string endpoint_gamerpic =
      fmt::format("https://xboxgamer.pics/{}", gamerpic_path);

  std::vector<uint8_t> tile = XStorageDownload(endpoint_gamerpic);

  return tile;
}

std::future<std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>>
XLiveAPI::GetFriendsGamerpicsAsync(uint64_t xuid,
                                   ui::ImGuiDrawer* imgui_drawer) {
  const auto user_profile = kernel_state()->xam_state()->GetUserProfile(xuid);

  if (!user_profile) {
    return {};
  }

  return std::async(std::launch::async, [this, xuid, imgui_drawer]() {
    const auto friends_xuids =
        kernel_state()->friends_manager()->GetFriendsXUIDs(xuid);

    const auto gamerpics = GetMultiGamerpicsFromXUIDs(friends_xuids);

    std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>
        immediate_gamerpics = {};

    for (const auto& [friend_xuid, gamerpic] : gamerpics) {
      immediate_gamerpics[friend_xuid] =
          std::move(imgui_drawer->LoadImGuiIcon({gamerpic}));
    }

    return immediate_gamerpics;
  });
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

}  // namespace kernel
}  // namespace xe
