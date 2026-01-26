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
#include <vector>

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/net_utils.h"

#ifdef XE_PLATFORM_WIN32
// clang-format off
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <winsock2.h>                    // NOLINT(build/include_order)

#include <Iphlpapi.h>
// clang-format on
#endif

namespace xe {
namespace kernel {

// TODO:
// Forward declaration or abstract type to hide OS details
class PlatformData;

class NetworkAdapterManager {
 public:
  NetworkAdapterManager();

  void RefreshNetworkAdapters();

  void SetSelectedAdapterGUID(const std::string guid);

  std::string GetSelectedAdapaterGUID() const;

  std::optional<IP_ADAPTER_ADDRESSES> GetAdapterFromGUID(
      const std::string guid) const;

  std::string GetAdapterFriendlyName(const IP_ADAPTER_ADDRESSES adapter) const;

  std::vector<std::string> GetAdaptersNames() const;

  MacAddress GetAdapterMacAddressFromGUID(const std::string guid) const;

  std::optional<IP_ADAPTER_ADDRESSES> GetSelectedAdapter() const;

  std::string GetSelectedAdapterName() const;

  sockaddr_in GetSelectedAdapterLocalIP() const { return local_ip_; }

  std::string GetSelectedAdapterLocalIP_Str() const {
    return ip_to_string(local_ip_);
  };

  const std::vector<IP_ADAPTER_ADDRESSES>& GetAdapters() const {
    return adapter_addresses_;
  }

  bool IsSelectedAdapterWANRouting() const { return is_WAN_routing_; }

  bool IsConnectedToLAN() const { return local_ip_.sin_addr.s_addr != 0; }

 private:
  void ResetSelectedAdapter();
  std::vector<IP_ADAPTER_ADDRESSES> DiscoverNetworkAdapters();
  bool UpdateNetworkInterface(sockaddr_in local_ip,
                              IP_ADAPTER_ADDRESSES adapter);

  void AutoSelectNetworkAdapter(
      const std::vector<IP_ADAPTER_ADDRESSES> adapters);

  // Adapter addresses buffer
  std::vector<uint8_t> adapter_addresses_data_;

  // Pointer to adapter_addresses_data_.data()
  std::vector<IP_ADAPTER_ADDRESSES> adapter_addresses_;

  bool is_WAN_routing_;

  sockaddr_in local_ip_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER
