/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <third_party/miniupnp/miniupnpc/include/miniwget.h>
#include <third_party/miniupnp/miniupnpc/include/upnpcommands.h>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/upnp.h"
#include "xenia/kernel/util/net_utils.h"

DEFINE_string(upnp_root, "", "UPnP Root Device", "Live");

DEFINE_bool(upnp, false, "Automatically port forward using UPnP", "Live");

DECLARE_bool(logging);

namespace xe {
namespace kernel {

UPnP::UPnP() {}

UPnP::~UPnP() {
  CloseOpenPorts();

  std::lock_guard igd_lock(igd_mutex_);
  FreeUPNPUrls(&igd_urls_);
}

void UPnP::SetUPnPState(bool upnp_state) { OVERRIDE_bool(upnp, upnp_state); }

void UPnP::Initialize() {
  if (active_) {
    return;
  }

  get_valid_IGD_ = std::async(std::launch::async, &UPnP::GetValidIGD, this);
};

void UPnP::Start() {
  if (active_) {
    return;
  }

  if (!get_valid_IGD_.valid()) {
    Initialize();
  }

  const std::optional<std::string> igd_desc = get_valid_IGD_.get();

  if (igd_desc.has_value()) {
    if (cvars::upnp_root != igd_desc) {
      cvars::upnp_root = igd_desc.value();
      OVERRIDE_string(upnp_root, cvars::upnp_root);
    }

    StartPeriodicPortsRefresher();
    active_ = true;
  } else {
    cvars::upnp_root = "";
    OVERRIDE_string(upnp_root, cvars::upnp_root);
  }
}

std::optional<std::string> UPnP::GetValidIGD() {
  // Check saved UPnP device is still valid this ensures we do not receive
  // HTTP_UNAUTHORIZED when performing UPnP actions.
  if (!cvars::upnp_root.empty()) {
    if (LoadIGD(cvars::upnp_root)) {
      return cvars::upnp_root;
    }
  }

  return DiscoverValidIGD();
}

UPNPDev* UPnP::DiscoverUPnPDevices() {
  XELOGI("{}: Starting UPnP search", __func__);

  int error = 0;
  UPNPDev* device_list = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);

  if (error) {
    XELOGE("{}: error code: {}", __func__, error);
    return nullptr;
  }

  return device_list;
}

std::optional<std::string> UPnP::DiscoverValidIGD() {
  std::lock_guard igd_lock(igd_mutex_);

  CleanupIGD();

  UPNPDev* device_list = DiscoverUPnPDevices();

  if (!device_list) {
    XELOGE("{}: No UPnP devices were found", __func__);
    return std::nullopt;
  }

  const int status = UPNP_GetValidIGD(device_list, &igd_urls_, &igd_data_,
                                      lan_addr_, sizeof(lan_addr_), nullptr, 0);

  std::optional<std::string> igd_desc_url;

  switch (status) {
    case 1:
      igd_desc_url = device_list->descURL;

      XELOGI("Found valid connected IGD: {} at {}", device_list->st,
             device_list->descURL);
      break;
    case 2:
      XELOGW("Found valid IGD, but it reported as NOT connected.");
      break;
    case 3:
      XELOGW("UPnP device found but not recognized as an IGD.");
      break;
    default:
      XELOGW("No valid IGD found (status: {}).", status);
      break;
  }

  freeUPNPDevlist(device_list);

  return igd_desc_url;
};

bool UPnP::LoadIGD(std::string igd_root) {
  std::lock_guard igd_lock(igd_mutex_);

  CleanupIGD();

  return UPNP_GetIGDFromUrl(igd_root.c_str(), &igd_urls_, &igd_data_, lan_addr_,
                            sizeof(lan_addr_));
}

std::future<int32_t> UPnP::AddPortAsync(std::string addr,
                                        uint16_t internal_port,
                                        std::string protocol) {
  auto add_port = std::async(std::launch::async, &UPnP::AddPort, this, addr,
                             internal_port, protocol);

  return add_port;
}

// Games can bind to ports or close sockets from any thread therefore all member
// variables must be accessed and written to safely using mutex.
int32_t UPnP::AddPort(std::string addr, uint16_t internal_port,
                      std::string protocol) {
  if (!active_) {
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  std::lock_guard igd_lock(igd_mutex_);
  std::lock_guard bindings_lock(mutex_bindings_);

  internal_port = GetMappedBindPort(internal_port);

  // If port is already open then skip opening again.
  if (port_bindings_.contains(protocol)) {
    if (port_bindings_.at(protocol).contains(internal_port)) {
      return UPNPCOMMAND_SUCCESS;
    }
  }

  TrackPort(internal_port, protocol);

  const uint16_t external_port = internal_port;
  const std::string internal_port_str = fmt::format("{}", internal_port);
  const std::string external_port_str = fmt::format("{}", external_port);
  const std::string lease_time_str =
      fmt::format("{}", default_lease_time_.count());

  int result =
      UPNP_AddPortMapping(igd_urls_.controlURL, igd_data_.first.servicetype,
                          external_port_str.c_str(), internal_port_str.c_str(),
                          addr.c_str(), "Xenia Canary Netplay",
                          protocol.c_str(), nullptr, lease_time_str.c_str());

  if (result ==
      static_cast<int>(UPnPErrorCodes::OnlyPermanentLeasesSupported)) {
    result = UPNP_AddPortMapping(
        igd_urls_.controlURL, igd_data_.first.servicetype,
        external_port_str.c_str(), internal_port_str.c_str(), addr.c_str(),
        "Xenia Canary Netplay", protocol.c_str(), nullptr, "0");

    leases_supported_ = false;
  }

  if (result != UPNPCOMMAND_SUCCESS) {
    if (result == HTTP_UNAUTHORIZED) {
      XELOGE("UPnP Unauthorized!");
    }

    XELOGE("Failed to bind port! {}:{}({}) to IGD:{}", addr, internal_port,
           protocol, external_port);

    XELOGE("UPnP error code {}", result);
    port_binding_results_[protocol][external_port] = result;
    return result;
  }

  port_bindings_[protocol][internal_port] = external_port;

  XELOGI("Successfully opened {}:{}({}) to IGD:{}", addr, internal_port,
         protocol, external_port);

  port_binding_results_[protocol][external_port] = result;

  return result;
}

std::future<int32_t> UPnP::RemovePortAsync(uint16_t port,
                                           std::string protocol) {
  auto remove_port =
      std::async(std::launch::async, &UPnP::RemovePort, this, port, protocol);

  return remove_port;
}

int32_t UPnP::RemovePort(uint16_t port, std::string protocol) {
  if (!active_) {
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  std::lock_guard igd_lock(igd_mutex_);
  std::lock_guard bindings_lock(mutex_bindings_);

  if (!port_bindings_.contains(protocol)) {
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  const auto& port_mapping = port_bindings_.at(protocol);

  if (!port_mapping.contains(port)) {
    XELOGE("Tried to unbind port mapping {} to IGD({}) but it isn't bound",
           port, protocol);
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  const std::string external_port_str = fmt::format("{}", port);

  const int result = UPNP_DeletePortMapping(
      igd_urls_.controlURL, igd_data_.first.servicetype,
      external_port_str.c_str(), protocol.c_str(), nullptr);

  if (result != UPNPCOMMAND_SUCCESS) {
    XELOGE("Failed to delete port mapping IGD:{}({}): {}", external_port_str,
           protocol, result);
  }

  port_binding_results_.at(protocol).erase(port);
  port_bindings_.at(protocol).erase(port);

  if (port_binding_results_.at(protocol).empty()) {
    port_binding_results_.erase(protocol);
  }

  if (port_bindings_.at(protocol).empty()) {
    port_bindings_.erase(protocol);
  }

  XELOGE("Successfully deleted port mapping {} to IGD:{}({})", port, port,
         protocol);

  return result;
}

void UPnP::CleanupIGD() {
  if (active_) {
    std::fill_n(lan_addr_, sizeof(lan_addr_), 0);

    igd_data_ = {};
    FreeUPNPUrls(&igd_urls_);
  }
}

std::string UPnP::GetLocalIP() {
  std::lock_guard igd_lock(igd_mutex_);
  return lan_addr_;
}

std::string UPnP::GetLocalIP_wget() {
  char lan_addr[64] = {};
  int responce_size = 0;
  int status = 0;

  miniwget_getaddr(cvars::upnp_root.c_str(), &responce_size, lan_addr,
                   sizeof(lan_addr), 0, &status);

  return lan_addr;
}

void UPnP::TrackPort(uint16_t port, std::string protocol) {
  std::lock_guard tracked_lock(mutex_tracked_ports_);
  tracked_ports_[port] = protocol;
}

void UPnP::OpenTrackedPorts() {
  if (!active_) {
    return;
  }

  for (const auto& [internal_port, protocol] : GetTrackedPorts()) {
    AddPort(GetLocalIP(), internal_port, protocol);
  }
}

void UPnP::OpenPorts(
    std::map<std::string, std::map<uint16_t, uint16_t>> open_ports) {
  if (!active_) {
    return;
  }

  for (const auto& [protocol, ports] : open_ports) {
    for (const auto& [internal_port, external_port] : ports) {
      AddPort(GetLocalIP(), internal_port, protocol);
    }
  }
}

void UPnP::CloseOpenPorts() {
  if (!active_) {
    return;
  }

  const auto opened_ports = GetOpenedPorts();

  for (const auto& [protocol, prot_bindings] : opened_ports) {
    for (const auto& [internal_port, external_port] : prot_bindings) {
      RemovePort(external_port, protocol);
    }
  }
}

void UPnP::RefreshPorts() {
  const auto opened_ports = GetOpenedPorts();

  // First we remove all the ports, otherwise we will receive conflict in
  // mapping entry error.
  CloseOpenPorts();

  // Open all tracked ports back effectively resetting the lease time.
  OpenPorts(opened_ports);
}

// Update the UPnP lease time every 45 minutes
void UPnP::StartPeriodicPortsRefresher() {
  if (refresh_ports_timer_) {
    return;
  }

  // We don't know if router supports variable lease times until we open the
  // first port.
  auto run = [=]() {
    if (leases_supported_) {
      RefreshPorts();
    }
  };

  refresh_ports_timer_ = xe::threading::PeriodicCallback::CreateRepeating(
      refresh_ports_interval_, run, "UPnP Refresh Ports");
}

uint16_t UPnP::GetMappedConnectPort(uint16_t external_port) {
  std::lock_guard mapped_lock(mapped_mutex_);

  if (mapped_connect_ports_.contains(external_port)) {
    return mapped_connect_ports_[external_port];
  }

  if (mapped_connect_ports_.contains(0)) {
    if (cvars::logging) {
      XELOGI("Using wildcard connect port for guest port {}!", external_port);
    }

    return mapped_connect_ports_.at(0);
  }

  if (cvars::logging) {
    XELOGI("Using connect port {}", external_port);
  }

  return external_port;
}

uint16_t UPnP::GetMappedBindPort(uint16_t external_port) {
  std::lock_guard mapped_lock(mapped_mutex_);

  if (mapped_bind_ports_.contains(external_port)) {
    return mapped_bind_ports_[external_port];
  }

  if (mapped_bind_ports_.contains(0)) {
    if (cvars::logging) {
      XELOGI("Using wildcard bind port for guest port {}!", external_port);
    }

    return mapped_bind_ports_.at(0);
  }

  return external_port;
}

const std::map<std::string, std::map<uint16_t, uint16_t>>
UPnP::GetOpenedPorts() {
  std::lock_guard bindings_lock(mutex_bindings_);
  return port_bindings_;
}

const std::map<std::string, std::map<uint16_t, int32_t>>
UPnP::GetPortBindingResults() {
  std::lock_guard bindings_lock(mutex_bindings_);
  return port_binding_results_;
}

const std::map<uint16_t, std::string> UPnP::GetTrackedPorts() {
  std::lock_guard tracked_lock(mutex_tracked_ports_);
  return tracked_ports_;
}

std::string_view UPnP::GetMiniUPnPcErrorCodeToDesc(int32_t error) noexcept {
  switch (error) {
    case UPNPCOMMAND_SUCCESS:
      return "Success";
    case UPNPCOMMAND_INVALID_ARGS:
      return "Invalid Args";
    case UPNPCOMMAND_HTTP_ERROR:
      return "HTTP Error";
    case UPNPCOMMAND_INVALID_RESPONSE:
      return "Invalid Response";
    case UPNPCOMMAND_MEM_ALLOC_ERROR:
      return "Memory Allocation";
    case UPNPCOMMAND_UNKNOWN_ERROR:
    default:
      return "Unknown Error Code";
  }
}

std::string_view UPnP::GetUPnPErrorCodeToDesc(int32_t error) noexcept {
  return GetUPnPErrorCodeToDesc(static_cast<UPnPErrorCodes>(error));
}

std::string_view UPnP::GetUPnPErrorCodeToDesc(UPnPErrorCodes error) noexcept {
  switch (error) {
    case UPnPErrorCodes::Success:
      return "Success";

    case UPnPErrorCodes::HttpUnauthorized:
      return "HTTP Unauthorized";

    case UPnPErrorCodes::ActionNotAuthorized:
      return "Action Not Authorized";

    case UPnPErrorCodes::InactiveConnectionStateRequired:
      return "Inactive Connection State Required";
    case UPnPErrorCodes::ConnectionSetupFailed:
      return "Connection Setup Failed";
    case UPnPErrorCodes::ConnectionSetupInProgress:
      return "Connection Setup In Progress";
    case UPnPErrorCodes::ConnectionNotConfigured:
      return "Connection Not Configured";
    case UPnPErrorCodes::DisconnectInProgress:
      return "Disconnect In Progress";
    case UPnPErrorCodes::InvalidLayer2Address:
      return "Invalid Layer2 Address";
    case UPnPErrorCodes::InternetAccessDisabled:
      return "Internet Access Disabled";
    case UPnPErrorCodes::InvalidConnectionType:
      return "Invalid Connection Type";
    case UPnPErrorCodes::ConnectionAlreadyTerminated:
      return "Connection Already Terminated";
    case UPnPErrorCodes::SpecifiedArrayIndexInvalid:
      return "Specified Array Index Invalid";
    case UPnPErrorCodes::NoSuchEntryInArray:
      return "No Such Entry In Array";
    case UPnPErrorCodes::WildcardNotPermittedInSourceIP:
      return "Wildcard Not Permitted In Source IP";
    case UPnPErrorCodes::WildcardNotPermittedInExternalPort:
      return "Wildcard Not Permitted In External Port";
    case UPnPErrorCodes::ConflictInMappingEntry:
      return "Conflict In Mapping Entry";
    case UPnPErrorCodes::SamePortValuesRequired:
      return "Same Port Values Required";
    case UPnPErrorCodes::OnlyPermanentLeasesSupported:
      return "Only Permanent Lease Supported";
    case UPnPErrorCodes::RemoteHostOnlySupportsRawTcp:
      return "Remote Host Only Supports Raw TCP";
    case UPnPErrorCodes::ExternalPortOnlySupportsWildcard:
      return "External Port Only Supports Wildcard";
    case UPnPErrorCodes::NoPortMappingsAvailable:
      return "No Port Mappings Available";
    case UPnPErrorCodes::ConflictWithOtherMechanisms:
      return "Conflict With Other Mechanisms";
    case UPnPErrorCodes::PortMappingNotFound:
      return "Port Mapping Not Found";
    case UPnPErrorCodes::InconsistentParameters:
      return "Inconsistent Parameters";
  }

  const auto error_code = static_cast<int32_t>(error);

  if (error_code >= 600 && error_code <= 699) {
    return "Unknown Common Action Error";
  }

  if (error_code >= 700 && error_code <= 799) {
    return "Unknown Action Specific Error";
  }

  return "Unknown Error Code";
}

}  // namespace kernel
}  // namespace xe
