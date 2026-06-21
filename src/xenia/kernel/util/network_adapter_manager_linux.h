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

#include "xenia/kernel/util/network_adapter_manager_interface.h"

#include <ifaddrs.h>
#include <libudev.h>

namespace xe {
namespace kernel {

class NetworkAdapterManager : public NetworkAdapterManagerInterface {
 public:
  NetworkAdapterManager();

  void Initialize() override;

  void SelectBestInterface() override;

  void SetSelectedAdapterIdentifier(
      const std::string adapter_identifier) override;

  std::string GetSelectedAdapterIdentifier() const override;

  std::string GetSelectedAdapterDescription() const override;

  std::optional<std::string> SearchUdevDevices(udev_enumerate* enumerate) const;

  std::optional<std::string> GetAdapterName(
      std::string interface_identifier) const;

  std::optional<std::string> GetSelectedAdapter() const;

  std::string GetSelectedAdapterLocalIPString() const override;

  std::unordered_map<std::string, std::string> GetAdaptersIdentifiers()
      const override;

  bool IsInterfaceSelected() const override;

  std::optional<std::string> GetAdapterIdentifier(std::string name);

 private:
  void ResetSelectedAdapter() override;

  std::vector<std::string> DiscoverNetworkAdapters();

  bool UpdateNetworkInterface(std::string name);

  std::optional<std::string> GetBestInterface();

  bool IsInterfaceWANRouting(const sockaddr_in interface_addr) override;

  std::optional<sockaddr_in> GetInterfaceIPFromName(
      const std::string name) const;

  bool SelectSavedNetworkAdapter() override;

  void AutoSelectNetworkAdapter(const std::string best_interface_name);

  std::vector<std::string> adapter_identifiers_;

  std::string best_interface_name_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_NETWORK_ADAPTER_MANAGER_LINUX
