/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <map>
#include <shared_mutex>

#include <third_party/miniupnp/miniupnpc/include/miniupnpc.h>
#include <xenia/base/threading_timer_queue.h>

namespace xe {
namespace kernel {

class UPnP {
 public:
  UPnP();
  ~UPnP();

  void Initialize();
  bool is_active() const { return active_; }

  // internal port is in BE notation.
  void AddPort(std::string_view addr, uint16_t internal_port,
               std::string_view protocol);

  // internal port is in BE notation.
  void RemovePort(uint16_t internal_port, std::string_view protocol);

  void RefreshPorts(std::string_view addr);

  void AddMappedConnectPort(uint16_t port, uint16_t mapped_port) {
    mapped_connect_ports_.insert({port, mapped_port});
  }

  void AddMappedBindPort(uint16_t port, uint16_t mapped_port) {
    mapped_bind_ports_.insert({port, mapped_port});
  }

  uint16_t get_mapped_connect_port(uint16_t port);

  uint16_t get_mapped_bind_port(uint16_t external_port);

  std::map<std::string, std::map<uint16_t, int32_t>>* port_binding_results() {
    return &port_binding_results_;
  };

  static const std::string GetLocalIP();

 private:
  // https://openconnectivity.org/developer/specifications/upnp-resources/upnp/internet-gateway-device-igd-v-2-0/
  // http://upnp.org/specs/gw/UPnP-gw-WANIPConnection-v2-Service.pdf
  enum UPnPErrorCodes : int { OnlyPermanentLeasesSupported = 725 };

  typedef std::map<uint16_t, uint16_t> port_binding;

  void RemovePortExternal(uint16_t external_port, std::string_view protocol,
                          bool verbose = true);
  void RefreshPortsTimer();

  bool LoadSavedUPnPDevice();
  const UPNPDev* SearchUPnPDevice();
  const UPNPDev* GetDeviceByName(const UPNPDev* device_list,
                                 std::string device_name);
  bool GetAndParseUPnPXmlData(std::string url);

  std::shared_mutex mutex_;
  std::atomic<bool> active_ = false;
  std::atomic<bool> leases_supported_ = true;

  IGDdatas* igd_data_ = new IGDdatas();
  UPNPUrls* igd_urls_ = new UPNPUrls();

  std::weak_ptr<xe::threading::TimerQueueWaitItem> wait_item_;

  std::map<std::string, port_binding> port_bindings_;
  std::map<std::string, std::map<uint16_t, int32_t>> port_binding_results_;

  port_binding mapped_connect_ports_;
  port_binding mapped_bind_ports_;
};
}  // namespace kernel
}  // namespace xe
