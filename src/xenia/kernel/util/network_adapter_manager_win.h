/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_WIN
#define XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_WIN

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xenia/kernel/util/network_adapter_manager_interface.h"

#ifdef XE_PLATFORM_WIN32
#include <Iphlpapi.h>
#endif

namespace xe {
namespace kernel {

class NetworkAdapterManager : public NetworkAdapterManagerInterface {
 public:
  NetworkAdapterManager();

  void Initialize() override;

  void SelectBestInterface() override;

  void SetSelectedAdapterIdentifier(const std::string guid) override;

  std::string GetSelectedAdapterIdentifier() const override;

  std::string GetSelectedAdapterDescription() const override;

  std::optional<IP_ADAPTER_ADDRESSES> GetAdapterFromGUID(
      const std::string guid) const;

  std::optional<IP_ADAPTER_ADDRESSES> GetAdapterFromIfIndex(
      int32_t IfIndex) const;

  std::string GetAdapterFriendlyName(const IP_ADAPTER_ADDRESSES adapter) const;

  std::vector<std::string> GetAdaptersNames() const;

  MacAddress GetAdapterMacAddressFromGUID(const std::string guid) const;

  std::optional<IP_ADAPTER_ADDRESSES> GetSelectedAdapter() const;

  std::string GetSelectedAdapterName() const;

  std::string GetSelectedAdapterLocalIPString() const override;

  std::unordered_map<std::string, std::string> GetAdaptersIdentifiers()
      const override;

  const std::vector<IP_ADAPTER_ADDRESSES>& GetAdapters() const {
    return adapter_addresses_;
  }

  bool IsInterfaceSelected() const override;

 private:
  void ResetSelectedAdapter() override;

  std::vector<IP_ADAPTER_ADDRESSES> DiscoverNetworkAdapters();

  bool UpdateNetworkInterface(const IP_ADAPTER_ADDRESSES adapter);

  int32_t GetBestInterfaceIfIndex();

  bool IsInterfaceWANRouting(const sockaddr_in interface_addr) override;

  std::optional<sockaddr_in> GetInterfaceIPFromIfIndex(
      const int32_t IfIndex) const;

  bool SelectSavedNetworkAdapter() override;

  void AutoSelectNetworkAdapter(const int32_t best_interface_IfIndex);

  // Adapter addresses buffer
  std::vector<uint8_t> adapter_addresses_data_;

  // Pointer to adapter_addresses_data_.data()
  std::vector<IP_ADAPTER_ADDRESSES> adapter_addresses_;

  int32_t best_interface_IfIndex_ = -1;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_WIN
