/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <memory>
#include <thread>
#include <utility>

#include <third_party/miniupnp/miniupnpc/include/miniwget.h>
#include <third_party/miniupnp/miniupnpc/include/upnpcommands.h>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/upnp.h"
#include "xenia/kernel/util/net_utils.h"
#include "xenia/kernel/xnet.h"

DEFINE_string(upnp_root, "", "UPnP Root Device", "Live");

DEFINE_bool(upnp, false, "Automatically port forward using UPnP", "Live");

namespace xe {
namespace kernel {

UPnP::UPnP() {}

UPnP::~UPnP() {
  {
    std::lock_guard actions_lock(mutex_port_actions_);
    if (last_port_action_.valid()) {
      last_port_action_.wait();
    }
  }

  CloseOpenPorts();

  std::lock_guard igd_lock(igd_mutex_);
  CleanupIGD();
}

void UPnP::SetUPnPState(bool upnp_state) { OVERRIDE_bool(upnp, upnp_state); }

void UPnP::Initialize() {
  if (active_) {
    return;
  }

  // Detached so shutdown doesn't wait for discovery.
  std::promise<std::unique_ptr<IGDResult>> igd_promise;
  get_valid_IGD_ = igd_promise.get_future();

  std::thread([saved_root = cvars::upnp_root,
               promise = std::move(igd_promise)]() mutable {
    promise.set_value(GetValidIGD(saved_root));
  }).detach();
};

void UPnP::Start() {
  if (active_) {
    return;
  }

  if (!get_valid_IGD_.valid()) {
    Initialize();
  }

  const std::unique_ptr<IGDResult> igd = get_valid_IGD_.get();

  if (igd) {
    {
      std::lock_guard igd_lock(igd_mutex_);

      CleanupIGD();

      igd_data_ = igd->data;
      igd_urls_ = std::exchange(igd->urls, UPNPUrls{});
      std::copy_n(igd->lan_addr, sizeof(lan_addr_), lan_addr_);
    }

    if (cvars::upnp_root != igd->desc_url) {
      cvars::upnp_root = igd->desc_url;
      OVERRIDE_string(upnp_root, cvars::upnp_root);
    }

    StartPeriodicPortsRefresher();
    active_ = true;
  } else {
    cvars::upnp_root = "";
    OVERRIDE_string(upnp_root, cvars::upnp_root);
  }
}

std::unique_ptr<UPnP::IGDResult> UPnP::GetValidIGD(
    const std::string& saved_root) {
  auto igd = std::make_unique<IGDResult>();

  // Check saved UPnP device is still valid this ensures we do not receive
  // HTTP_UNAUTHORIZED when performing UPnP actions.
  if (!saved_root.empty()) {
    if (LoadIGD(saved_root, *igd)) {
      igd->desc_url = saved_root;
      return igd;
    }

    igd = std::make_unique<IGDResult>();
  }

  if (DiscoverValidIGD(*igd)) {
    return igd;
  }

  return nullptr;
}

UPNPDev* UPnP::DiscoverUPnPDevices() {
  XELOGI("{}: Starting UPnP search", __func__);

  int error = 0;
  UPNPDev* device_list = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);

  if (error) {
    if (device_list) {
      freeUPNPDevlist(device_list);
    }

    XELOGE("{}: error code: {}", __func__, error);
    return nullptr;
  }

  return device_list;
}

bool UPnP::DiscoverValidIGD(IGDResult& igd) {
  std::unique_ptr<UPNPDev, decltype(&freeUPNPDevlist)> device_list(
      DiscoverUPnPDevices(), &freeUPNPDevlist);

  if (!device_list) {
    XELOGE("{}: No UPnP devices were found", __func__);
    return false;
  }

  const int status =
      UPNP_GetValidIGD(device_list.get(), &igd.urls, &igd.data, igd.lan_addr,
                       sizeof(igd.lan_addr), nullptr, 0);

  bool valid = false;

  switch (status) {
    case UPNP_NO_IGD: {
      XELOGW("No IGD found.");
    } break;
    case UPNP_CONNECTED_IGD: {
      igd.desc_url = device_list->descURL;
      valid = true;

      XELOGI("Found valid and connected IGD: {} at {}", device_list->st,
             device_list->descURL);
    } break;
    case UPNP_PRIVATEIP_IGD: {
      igd.desc_url = device_list->descURL;
      valid = true;

      XELOGI(
          "Found valid and connected IGD but with a reserved address: {} at {}",
          device_list->st, device_list->descURL);
    } break;
    case UPNP_DISCONNECTED_IGD: {
      XELOGW("Found valid IGD, but it reported as NOT connected.");
    } break;
    case UPNP_UNKNOWN_DEVICE: {
      XELOGW("UPnP device has been found but was not recognized as an IGD.");
    } break;
    default: {
      XELOGW("No valid IGD found (status: {}).", status);
    } break;
  }

  return valid;
};

bool UPnP::LoadIGD(const std::string& igd_root, IGDResult& igd) {
  return UPNP_GetIGDFromUrl(igd_root.c_str(), &igd.urls, &igd.data,
                            igd.lan_addr, sizeof(igd.lan_addr));
}

std::future<int32_t> UPnP::QueuePortAction(std::function<int32_t()> action) {
  std::lock_guard actions_lock(mutex_port_actions_);

  auto promise = std::make_shared<std::promise<int32_t>>();
  auto result = promise->get_future();

  last_port_action_ =
      std::async(std::launch::async, [previous = last_port_action_,
                                      action = std::move(action), promise]() {
        if (previous.valid()) {
          previous.wait();
        }

        promise->set_value(action());
      }).share();

  return result;
}

std::future<int32_t> UPnP::AddPortAsync(std::string addr,
                                        uint16_t internal_port,
                                        std::string protocol) {
  return QueuePortAction([this, addr, internal_port, protocol]() {
    return AddPort(addr, internal_port, protocol);
  });
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
  const auto protocol_bindings = port_bindings_.find(protocol);
  if (protocol_bindings != port_bindings_.end() &&
      protocol_bindings->second.find(internal_port) !=
          protocol_bindings->second.end()) {
    return UPNPCOMMAND_SUCCESS;
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
  return QueuePortAction([this, port, protocol]() {
    UntrackPort(port, protocol);
    return RemovePort(port, protocol);
  });
}

int32_t UPnP::RemovePort(uint16_t internal_port, std::string protocol) {
  if (!active_) {
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  std::lock_guard igd_lock(igd_mutex_);
  std::lock_guard bindings_lock(mutex_bindings_);

  auto protocol_it = port_bindings_.find(protocol);
  if (protocol_it == port_bindings_.end()) {
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  auto binding_it = protocol_it->second.find(internal_port);
  if (binding_it == protocol_it->second.end()) {
    return UPNPCOMMAND_UNKNOWN_ERROR;
  }

  const uint16_t external_port = binding_it->second;
  const std::string external_port_str = fmt::format("{}", external_port);

  const int result = UPNP_DeletePortMapping(
      igd_urls_.controlURL, igd_data_.first.servicetype,
      external_port_str.c_str(), protocol.c_str(), nullptr);

  const bool mapping_removed =
      result == UPNPCOMMAND_SUCCESS ||
      result == static_cast<int>(UPnPErrorCodes::PortMappingNotFound);

  if (!mapping_removed) {
    XELOGE("Failed to delete port mapping IGD:{}({}): {}", external_port,
           protocol, result);
    port_binding_results_[protocol][external_port] = result;
    return result;
  }

  protocol_it->second.erase(binding_it);
  if (protocol_it->second.empty()) {
    port_bindings_.erase(protocol_it);
  }

  auto results_it = port_binding_results_.find(protocol);
  if (results_it != port_binding_results_.end()) {
    results_it->second.erase(external_port);
    if (results_it->second.empty()) {
      port_binding_results_.erase(results_it);
    }
  }

  XELOGI("Deleted port mapping {} to IGD:{}({})", internal_port, external_port,
         protocol);

  return result;
}

void UPnP::CleanupIGD() {
  std::fill_n(lan_addr_, sizeof(lan_addr_), '\0');
  igd_data_ = {};
  FreeUPNPUrls(&igd_urls_);
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

  if (status != HTTP_OK) {
    XELOGI("UPnP Local IP Error: HTTP Status Code - {}", status);
  }

  return lan_addr;
}

void UPnP::TrackPort(uint16_t port, std::string protocol) {
  std::lock_guard tracked_lock(mutex_tracked_ports_);
  tracked_ports_[protocol].insert(port);
}

void UPnP::UntrackPort(uint16_t port, std::string protocol) {
  std::lock_guard tracked_lock(mutex_tracked_ports_);

  auto protocol_it = tracked_ports_.find(protocol);
  if (protocol_it == tracked_ports_.end()) {
    return;
  }

  protocol_it->second.erase(port);
  if (protocol_it->second.empty()) {
    tracked_ports_.erase(protocol_it);
  }
}

void UPnP::OpenTrackedPorts() {
  if (!active_) {
    return;
  }

  for (const auto& [protocol, internal_ports] : GetTrackedPorts()) {
    for (const auto& internal_port : internal_ports) {
      AddPort(GetLocalIP(), internal_port, protocol);
    }
  }
}

void UPnP::OpenPorts(
    const std::map<std::string, std::map<uint16_t, uint16_t>>& open_ports) {
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
      RemovePort(internal_port, protocol);
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
  auto run = [this]() {
    if (leases_supported_) {
      RefreshPorts();
    }
  };

  refresh_ports_timer_ = xe::threading::PeriodicCallback::CreateRepeating(
      refresh_ports_interval_, run, "UPnP Refresh Ports");
}

uint16_t UPnP::GetMappedConnectPort(uint16_t external_port) {
  std::lock_guard mapped_lock(mapped_mutex_);

  const auto mapped_port = mapped_connect_ports_.find(external_port);
  if (mapped_port != mapped_connect_ports_.end()) {
    return mapped_port->second;
  }

  const auto wildcard_port = mapped_connect_ports_.find(0);
  if (wildcard_port != mapped_connect_ports_.end()) {
    XELOGNETD("Using wildcard connect port for guest port {}!", external_port);

    return wildcard_port->second;
  }

  XELOGNETD("Using connect port {}", external_port);

  return external_port;
}

uint16_t UPnP::GetMappedBindPort(uint16_t external_port) {
  std::lock_guard mapped_lock(mapped_mutex_);

  const auto mapped_port = mapped_bind_ports_.find(external_port);
  if (mapped_port != mapped_bind_ports_.end()) {
    return mapped_port->second;
  }

  const auto wildcard_port = mapped_bind_ports_.find(0);
  if (wildcard_port != mapped_bind_ports_.end()) {
    XELOGNETD("Using wildcard bind port for guest port {}!", external_port);

    return wildcard_port->second;
  }

  return external_port;
}

std::map<std::string, std::map<uint16_t, uint16_t>> UPnP::GetOpenedPorts()
    const {
  std::lock_guard bindings_lock(mutex_bindings_);
  return port_bindings_;
}

std::map<std::string, std::map<uint16_t, int32_t>> UPnP::GetPortBindingResults()
    const {
  std::lock_guard bindings_lock(mutex_bindings_);
  return port_binding_results_;
}

std::map<std::string, std::set<uint16_t>> UPnP::GetTrackedPorts() const {
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
