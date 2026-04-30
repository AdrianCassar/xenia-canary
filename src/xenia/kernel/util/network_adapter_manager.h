/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER
#define XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xenia/kernel/util/net_utils.h"

#ifdef XE_PLATFORM_WIN32
#include <Iphlpapi.h>
#endif

namespace xe {
namespace kernel {

// TODO:
// Forward declaration or abstract type to hide OS details
class PlatformData;

class NetworkAdapterManager {
 public:
  NetworkAdapterManager();

  void Initialize();

  void SelectBestInterface();

  void SetSelectedAdapterGUID(const std::string guid);

  std::string GetSelectedAdapaterGUID() const;

  std::string GetSelectedAdapterDesciption() const;

  std::optional<IP_ADAPTER_ADDRESSES> GetAdapterFromGUID(
      const std::string guid) const;

  std::optional<IP_ADAPTER_ADDRESSES> GetAdapterFromIfIndex(
      int32_t IfIndex) const;

  std::string GetAdapterFriendlyName(const IP_ADAPTER_ADDRESSES adapter) const;

  std::vector<std::string> GetAdaptersNames() const;

  MacAddress GetAdapterMacAddressFromGUID(const std::string guid) const;

  std::optional<IP_ADAPTER_ADDRESSES> GetSelectedAdapter() const;

  std::string GetSelectedAdapterName() const;

  sockaddr_in GetSelectedAdapterLocalIP() const { return local_ip_; }

  std::string GetSelectedAdapterLocalIPString() const;

  const std::vector<IP_ADAPTER_ADDRESSES>& GetAdapters() const {
    return adapter_addresses_;
  }

  bool IsSelectedAdapterWANRouting() const { return is_WAN_routing_; }

  bool IsInterfaceSelected() const;

 private:
  void ResetSelectedAdapter();

  std::vector<IP_ADAPTER_ADDRESSES> DiscoverNetworkAdapters();

  bool UpdateNetworkInterface(const IP_ADAPTER_ADDRESSES adapter);

  int32_t GetBestInterfaceIfIndex();

  bool IsInterfaceWANRouting(const sockaddr_in interface_addr);

  std::optional<sockaddr_in> GetInterfaceIPFromIfIndex(
      const int32_t IfIndex) const;

  bool SelectSavedNetworkAdapter();

  void AutoSelectNetworkAdapter(const int32_t best_interface_IfIndex);

  // Adapter addresses buffer
  std::vector<uint8_t> adapter_addresses_data_;

  // Pointer to adapter_addresses_data_.data()
  std::vector<IP_ADAPTER_ADDRESSES> adapter_addresses_;

  int32_t best_interface_IfIndex_ = -1;

  sockaddr_in local_ip_ = {};

  bool is_WAN_routing_ = false;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER
