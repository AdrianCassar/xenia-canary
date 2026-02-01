/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UPNP_H_
#define XENIA_KERNEL_UPNP_H_

#include <future>
#include <set>
#include <shared_mutex>

#include <third_party/miniupnp/miniupnpc/include/miniupnpc.h>

#include "xenia/base/threading.h"

using namespace std::chrono_literals;

namespace xe {
namespace kernel {

class UPnP {
 public:
  // https://openconnectivity.org/developer/specifications/upnp-resources/upnp/internet-gateway-device-igd-v-2-0/
  // http://upnp.org/specs/gw/UPnP-gw-WANIPConnection-v2-Service.pdf
  enum class UPnPErrorCodes : int32_t {
    // General
    Success = 0,

    // Client-side Issues (400-499)
    HttpUnauthorized = 401,

    // Common Action Errors (600-699)
    ActionNotAuthorized = 606,

    // Action-specific errors for standard actions (700-799)
    InactiveConnectionStateRequired = 703,
    ConnectionSetupFailed = 704,
    ConnectionSetupInProgress = 705,
    ConnectionNotConfigured = 706,
    DisconnectInProgress = 707,
    InvalidLayer2Address = 708,
    InternetAccessDisabled = 709,
    InvalidConnectionType = 710,
    ConnectionAlreadyTerminated = 711,
    SpecifiedArrayIndexInvalid = 713,
    NoSuchEntryInArray = 714,
    WildcardNotPermittedInSourceIP = 715,
    WildcardNotPermittedInExternalPort = 716,
    ConflictInMappingEntry = 718,
    SamePortValuesRequired = 724,
    OnlyPermanentLeasesSupported = 725,
    RemoteHostOnlySupportsRawTcp = 726,
    ExternalPortOnlySupportsWildcard = 727,
    NoPortMappingsAvailable = 728,
    ConflictWithOtherMechanisms = 729,
    PortMappingNotFound = 730,
    InconsistentParameters = 733
  };

  UPnP();

  ~UPnP();

  bool IsActive() const { return active_; }

  bool IsVariableLeaseSupported() const { return leases_supported_; }

  static void SetUPnPState(bool upnp_state);

  void Initialize();

  void Start();

  std::optional<std::string> GetValidIGD();

  std::optional<std::string> DiscoverValidIGD();

  UPNPDev* DiscoverUPnPDevices();

  bool LoadIGD(std::string igd_root);

  std::future<int32_t> AddPortAsync(std::string addr, uint16_t internal_port,
                                    std::string protocol);

  // Internal port is in BE notation.
  int32_t AddPort(std::string addr, uint16_t internal_port,
                  std::string protocol);

  std::future<int32_t> RemovePortAsync(uint16_t port, std::string protocol);

  // Internal port is in BE notation.
  int32_t RemovePort(uint16_t internal_port, std::string protocol);

  std::string GetLocalIP();

  static std::string GetLocalIP_wget();

  void TrackPort(uint16_t port, std::string protocol);

  void OpenTrackedPorts();

  void OpenPorts(
      std::map<std::string, std::map<uint16_t, uint16_t>> open_ports);

  void CloseOpenPorts();

  void RefreshPorts();

  uint16_t GetMappedConnectPort(uint16_t external_port);

  uint16_t GetMappedBindPort(uint16_t external_port);

  const std::map<std::string, std::map<uint16_t, uint16_t>> GetOpenedPorts();

  const std::map<std::string, std::map<uint16_t, int32_t>>
  GetPortBindingResults();

  const std::map<std::string, std::set<uint16_t>> GetTrackedPorts();

  static std::string_view GetMiniUPnPcErrorCodeToDesc(int32_t error) noexcept;

  static std::string_view GetUPnPErrorCodeToDesc(int32_t error) noexcept;

  static std::string_view GetUPnPErrorCodeToDesc(UPnPErrorCodes error) noexcept;

  void AddMappedConnectPort(uint16_t port, uint16_t mapped_port) {
    mapped_connect_ports_.insert({port, mapped_port});
  }

  void AddMappedBindPort(uint16_t port, uint16_t mapped_port) {
    mapped_bind_ports_.insert({port, mapped_port});
  }

 private:
  void CleanupIGD();
  void StartPeriodicPortsRefresher();

  std::future<std::optional<std::string>> get_valid_IGD_;

  std::atomic<bool> active_ = false;
  std::atomic<bool> leases_supported_ = true;

  std::mutex igd_mutex_;
  IGDdatas igd_data_ = {};
  UPNPUrls igd_urls_ = {};
  char lan_addr_[64] = {};

  const std::chrono::seconds default_lease_time_ = 1h;
  const std::chrono::minutes refresh_ports_interval_ = 45min;
  std::unique_ptr<xe::threading::PeriodicCallback> refresh_ports_timer_;

  std::mutex mutex_tracked_ports_;
  std::map<std::string, std::set<uint16_t>> tracked_ports_;

  std::mutex mutex_bindings_;
  std::map<std::string, std::map<uint16_t, uint16_t>> port_bindings_;
  std::map<std::string, std::map<uint16_t, int32_t>> port_binding_results_;

  std::mutex mapped_mutex_;
  std::map<uint16_t, uint16_t> mapped_connect_ports_;
  std::map<uint16_t, uint16_t> mapped_bind_ports_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UPNP_H_
