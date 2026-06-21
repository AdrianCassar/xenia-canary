/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_LINUX
#define XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_LINUX

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/kernel/util/net_utils.h"

#include <ifaddrs.h>
#include <libudev.h>

namespace xe {
namespace kernel {

class NetworkAdapterManager {
 public:
  NetworkAdapterManager();

  void Initialize();

  void SelectBestInterface();

  void SetSelectedAdapterIdentifier(const std::string adapter_identifier);

  std::string GetSelectedAdapterIdentifier() const;

  std::string GetSelectedAdapterDescription() const;

  std::optional<std::string> SearchUdevDevices(udev_enumerate* enumerate) const;

  std::optional<std::string> GetAdapterName(
      std::string interface_identifier) const;

  std::optional<std::string> GetSelectedAdapter() const;

  sockaddr_in GetSelectedAdapterLocalIP() const { return local_ip_; }

  std::string GetSelectedAdapterLocalIPString() const;

  std::unordered_map<std::string, std::string> GetAdaptersIdentifiers() const;

  const std::vector<std::string>& GetAdapters() const {
    return adapter_identifiers_;
  }

  bool IsSelectedAdapterWANRouting() const { return is_WAN_routing_; }

  bool IsInterfaceSelected() const;

  std::optional<std::string> GetAdapterIdentifier(std::string name);

 private:
  void ResetSelectedAdapter();

  std::vector<std::string> DiscoverNetworkAdapters();

  bool UpdateNetworkInterface(std::string name);

  std::optional<std::string> GetBestInterface();

  bool IsInterfaceWANRouting(const sockaddr_in interface_addr);

  std::optional<sockaddr_in> GetInterfaceIPFromName(
      const std::string name) const;

  bool SelectSavedNetworkAdapter();

  void AutoSelectNetworkAdapter(const std::string best_interface_name);

  std::vector<std::string> adapter_identifiers_;

  std::string best_interface_name_;

  sockaddr_in local_ip_ = {};

  bool is_WAN_routing_ = false;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_LINUX
