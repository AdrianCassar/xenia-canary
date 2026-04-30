/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/network_adapter_manager.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

DEFINE_string(network_guid, "", "Network Interface GUID", "Live");

namespace xe {
namespace kernel {

NetworkAdapterManager::NetworkAdapterManager() {}

void NetworkAdapterManager::Initialize() {
  adapter_addresses_ = DiscoverNetworkAdapters();

  if (!adapter_addresses_.empty()) {
    // In-case our selected adapter was removed during runtime reselect best
    // interface index.
    best_interface_IfIndex_ = GetBestInterfaceIfIndex();

    AutoSelectNetworkAdapter(best_interface_IfIndex_);
  }
}

void NetworkAdapterManager::SelectBestInterface() {
  ResetSelectedAdapter();
  Initialize();
}

void NetworkAdapterManager::SetSelectedAdapterGUID(std::string guid) {
  ResetSelectedAdapter();

  const auto adapter = GetAdapterFromGUID(guid);

  if (adapter.has_value()) {
    UpdateNetworkInterface(adapter.value());
  }

  XELOGI(GetSelectedAdapterDesciption());
}

std::string NetworkAdapterManager::GetSelectedAdapaterGUID() const {
  return cvars::network_guid;
}

std::string NetworkAdapterManager::GetSelectedAdapterDesciption() const {
  return fmt::format("Network Interface: {} {} {}", GetSelectedAdapterName(),
                     GetSelectedAdapterLocalIPString(),
                     is_WAN_routing_ ? "(WAN Routing)" : "(Non-WAN Routing)");
}

std::optional<IP_ADAPTER_ADDRESSES> NetworkAdapterManager::GetAdapterFromGUID(
    std::string guid) const {
  const auto found_adapter =
      std::find_if(adapter_addresses_.cbegin(), adapter_addresses_.cend(),
                   [guid](const IP_ADAPTER_ADDRESSES& adapter) {
                     return guid == adapter.AdapterName;
                   });

  std::optional<IP_ADAPTER_ADDRESSES> adapter = std::nullopt;

  if (found_adapter != adapter_addresses_.cend()) {
    adapter = *found_adapter;
  }

  return adapter;
}

std::optional<IP_ADAPTER_ADDRESSES>
NetworkAdapterManager::GetAdapterFromIfIndex(const int32_t IfIndex) const {
  const auto found_adapter = std::find_if(
      adapter_addresses_.cbegin(), adapter_addresses_.cend(),
      [IfIndex](auto adapter) { return adapter.IfIndex == IfIndex; });

  std::optional<IP_ADAPTER_ADDRESSES> adapter = std::nullopt;

  if (found_adapter != adapter_addresses_.cend()) {
    adapter = *found_adapter;
  }

  return adapter;
}

std::string NetworkAdapterManager::GetAdapterFriendlyName(
    IP_ADAPTER_ADDRESSES adapter) const {
  const std::string interface_name =
      xe::to_utf8(reinterpret_cast<char16_t*>(adapter.FriendlyName));

  return interface_name;
}

std::vector<std::string> NetworkAdapterManager::GetAdaptersNames() const {
  std::vector<std::string> adapter_names = {};

  for (const auto& adapter : adapter_addresses_) {
    adapter_names.push_back(GetAdapterFriendlyName(adapter));
  }

  return adapter_names;
};

MacAddress NetworkAdapterManager::GetAdapterMacAddressFromGUID(
    std::string guid) const {
  const auto adapter = GetAdapterFromGUID(guid);

  if (adapter.has_value() &&
      adapter->PhysicalAddressLength == MacAddress::MacAddressSize) {
    uint8_t mac_address[MacAddress::MacAddressSize];
    memcpy(mac_address, adapter->PhysicalAddress, MacAddress::MacAddressSize);

    return mac_address;
  } else {
    // If there are no adapters generate a mac address.
    return GenerateMacAddress();
  }
}

std::optional<IP_ADAPTER_ADDRESSES> NetworkAdapterManager::GetSelectedAdapter()
    const {
  return GetAdapterFromGUID(cvars::network_guid);
}

std::string NetworkAdapterManager::GetSelectedAdapterName() const {
  const auto adapter = GetSelectedAdapter();
  return adapter.has_value() ? GetAdapterFriendlyName(adapter.value()) : "";
}

std::string NetworkAdapterManager::GetSelectedAdapterLocalIPString() const {
  return ip_to_string(local_ip_);
}

bool NetworkAdapterManager::IsInterfaceSelected() const {
  return local_ip_.sin_addr.s_addr;
}

void NetworkAdapterManager::ResetSelectedAdapter() {
  local_ip_ = {};
  is_WAN_routing_ = false;
  OVERRIDE_string(network_guid, "");
}

std::vector<IP_ADAPTER_ADDRESSES>
NetworkAdapterManager::DiscoverNetworkAdapters() {
  XELOGI("Discovering network interfaces...");

  uint32_t ret = 0;
  ULONG buffer_length = 0;

  adapter_addresses_data_.clear();

  std::vector<IP_ADAPTER_ADDRESSES> adapter_addresses = {};

  IP_ADAPTER_ADDRESSES* adapters_ptr = nullptr;

  const uint32_t flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                         GAA_FLAG_SKIP_DNS_SERVER;

  ret = GetAdaptersAddresses(AF_INET, flags, 0, 0, &buffer_length);

  if (ret != ERROR_BUFFER_OVERFLOW) {
    return adapter_addresses;
  }

  adapter_addresses_data_.resize(buffer_length);

  adapters_ptr =
      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapter_addresses_data_.data());

  ret = GetAdaptersAddresses(AF_INET, flags, 0, adapters_ptr, &buffer_length);

  if (ret != NO_ERROR) {
    return adapter_addresses;
  }

  std::string networks = "Network Interfaces:\n";

  for (IP_ADAPTER_ADDRESSES* adapter_ptr = adapters_ptr; adapter_ptr != nullptr;
       adapter_ptr = adapter_ptr->Next) {
    if (adapter_ptr->OperStatus == IfOperStatusUp &&
        (adapter_ptr->IfType == IF_TYPE_IEEE80211 ||
         adapter_ptr->IfType == IF_TYPE_ETHERNET_CSMACD ||
         adapter_ptr->IfType == IF_TYPE_PROP_VIRTUAL ||
         adapter_ptr->IfType == IF_TYPE_TUNNEL)) {
      if (adapter_ptr->PhysicalAddress != nullptr) {
        for (PIP_ADAPTER_UNICAST_ADDRESS_LH adapter_address =
                 adapter_ptr->FirstUnicastAddress;
             adapter_address != nullptr;
             adapter_address = adapter_address->Next) {
          sockaddr_in addr_ptr = *reinterpret_cast<sockaddr_in*>(
              adapter_address->Address.lpSockaddr);

          if (addr_ptr.sin_family == AF_INET) {
            std::string friendlyName = GetAdapterFriendlyName(*adapter_ptr);
            std::string guid = adapter_ptr->AdapterName;

            IP_ADAPTER_ADDRESSES adapter = *adapter_ptr;

            adapter_addresses.push_back(adapter);

            networks += fmt::format("{} {}: {}\n", friendlyName, guid,
                                    ip_to_string(addr_ptr));
          }
        }
      }
    }
  }

  if (adapter_addresses.empty()) {
    XELOGI("No network interfaces detected!\n");
  } else {
    XELOGI("Found {} network interfaces!\n", adapter_addresses.size());
    XELOGI("{}", xe::string_util::trim(networks));
  }

  return adapter_addresses;
}

bool NetworkAdapterManager::UpdateNetworkInterface(
    IP_ADAPTER_ADDRESSES adapter) {
  const std::optional<sockaddr_in> adapter_addr =
      GetInterfaceIPFromIfIndex(adapter.IfIndex);

  bool updated = adapter_addr.has_value();

  if (updated) {
    local_ip_ = adapter_addr.value();
    is_WAN_routing_ = IsInterfaceWANRouting(adapter_addr.value());
    OVERRIDE_string(network_guid, adapter.AdapterName);
  }

  return updated;
}

// Get the index of the best interface to reach that WAN address
int32_t NetworkAdapterManager::GetBestInterfaceIfIndex() {
  const in_addr destAddr = ip_to_in_addr("8.8.8.8");

  DWORD bestIfIndex = -1;
  DWORD result = GetBestInterface(destAddr.S_un.S_addr, &bestIfIndex);

  if (result != NO_ERROR) {
    XELOGI("Error finding best interface: {}", result);
  }

  return bestIfIndex;
}

bool NetworkAdapterManager::IsInterfaceWANRouting(sockaddr_in interface_addr) {
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (sock == INVALID_SOCKET) {
    return false;
  }

  // Force socket to use interface address for connection attempt.
  if (bind(sock, reinterpret_cast<sockaddr*>(&interface_addr),
           sizeof(sockaddr)) == SOCKET_ERROR) {
    closesocket(sock);
    return false;
  }

  int timeout = 3000;
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout,
             sizeof(timeout));

  sockaddr_in remoteAddr{};
  remoteAddr.sin_family = AF_INET;
  remoteAddr.sin_port = htons(53);
  remoteAddr.sin_addr = ip_to_in_addr("8.8.8.8");  // Google DNS

  bool success = connect(sock, reinterpret_cast<sockaddr*>(&remoteAddr),
                         sizeof(remoteAddr)) != SOCKET_ERROR;

  closesocket(sock);

  return success;
}

std::optional<sockaddr_in> NetworkAdapterManager::GetInterfaceIPFromIfIndex(
    int32_t IfIndex) const {
  if (IfIndex < 0) {
    return std::nullopt;
  }

  std::optional<sockaddr_in> adapter_sockaddr = std::nullopt;

  for (const auto& adapter : adapter_addresses_) {
    if (adapter.IfIndex == IfIndex && !adapter_sockaddr.has_value()) {
      if (adapter.PhysicalAddress != nullptr) {
        for (PIP_ADAPTER_UNICAST_ADDRESS_LH adapter_address =
                 adapter.FirstUnicastAddress;
             adapter_address != nullptr;
             adapter_address = adapter_address->Next) {
          sockaddr_in addr_ptr = *reinterpret_cast<sockaddr_in*>(
              adapter_address->Address.lpSockaddr);

          if (addr_ptr.sin_family == AF_INET) {
            adapter_sockaddr = addr_ptr;
            break;
          }
        }
      }
    }
  }

  return adapter_sockaddr;
}

// Select our saved network GUID if it's available.
bool NetworkAdapterManager::SelectSavedNetworkAdapter() {
  const std::optional<IP_ADAPTER_ADDRESSES> adapter = GetSelectedAdapter();

  if (adapter.has_value()) {
    return UpdateNetworkInterface(adapter.value());
  } else {
    XELOGI("Interface GUID: {} not found!", cvars::network_guid);
  }

  return false;
}

// Select saved network GUID interface if available, otherwise fallback to best
// interface IfIndex.
void NetworkAdapterManager::AutoSelectNetworkAdapter(
    int32_t best_interface_IfIndex) {
  bool selected = SelectSavedNetworkAdapter();

  // Fallback to best interface.
  if (!selected) {
    const auto adapter = GetAdapterFromIfIndex(best_interface_IfIndex);

    if (adapter.has_value()) {
      selected = UpdateNetworkInterface(adapter.value());
    }
  }

  if (selected) {
    XELOGI(GetSelectedAdapterDesciption());
  } else {
    XELOGI("Unspecified Network Interface!");
  }
}

}  // namespace kernel
}  // namespace xe
