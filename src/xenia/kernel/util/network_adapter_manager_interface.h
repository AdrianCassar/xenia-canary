/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_INTERFACE
#define XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_INTERFACE

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {

class NetworkAdapterManagerInterface {
 public:
  virtual ~NetworkAdapterManagerInterface() = default;

  virtual void Initialize() = 0;

  virtual void SelectBestInterface() = 0;

  virtual void SetSelectedAdapterIdentifier(
      const std::string adapter_identifier) = 0;

  virtual std::string GetSelectedAdapterIdentifier() const = 0;

  virtual std::string GetSelectedAdapterDescription() const = 0;

  virtual std::string GetSelectedAdapterLocalIPString() const = 0;

  virtual std::unordered_map<std::string, std::string> GetAdaptersIdentifiers()
      const = 0;

  virtual bool IsInterfaceSelected() const = 0;

  sockaddr_in GetSelectedAdapterLocalIP() const { return local_ip_; };

  bool IsSelectedAdapterWANRouting() const { return is_WAN_routing_; }

 protected:
  virtual void ResetSelectedAdapter() = 0;

  virtual bool IsInterfaceWANRouting(const sockaddr_in interface_addr) = 0;

  virtual bool SelectSavedNetworkAdapter() = 0;

  sockaddr_in local_ip_ = {};

  bool is_WAN_routing_ = false;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_INTERFACE
