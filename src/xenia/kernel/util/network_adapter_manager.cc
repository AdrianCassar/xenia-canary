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
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/upnp.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xnet.h"

DEFINE_string(network_guid, "", "Network Interface GUID", "Live");

DECLARE_bool(logging);

DECLARE_bool(upnp);

DECLARE_string(upnp_root);

namespace xe {
namespace kernel {

NetworkAdapterManager::NetworkAdapterManager()
    : adapter_addresses_({}),
      adapter_addresses_data_({}),
      is_WAN_routing_(false),
      local_ip_({}) {
  RefreshNetworkAdapters();
}

// Automatically select an adapter
void NetworkAdapterManager::RefreshNetworkAdapters() {
  adapter_addresses_ = DiscoverNetworkAdapters();
  AutoSelectNetworkAdapter(adapter_addresses_);
}

void NetworkAdapterManager::SetSelectedAdapterGUID(const std::string guid) {
  if (!kernel_state()->emulator()->is_title_open()) {
    if (guid.empty()) {
      ResetSelectedAdapter();
    } else {
      OVERRIDE_string(network_guid, guid);
    }
  }
}

std::string NetworkAdapterManager::GetSelectedAdapaterGUID() const {
  return cvars::network_guid;
}

std::optional<IP_ADAPTER_ADDRESSES> NetworkAdapterManager::GetAdapterFromGUID(
    const std::string guid) const {
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

std::string NetworkAdapterManager::GetAdapterFriendlyName(
    const IP_ADAPTER_ADDRESSES adapter) const {
  char interface_name[MAX_ADAPTER_NAME_LENGTH];
  size_t bytes_out =
      wcstombs(interface_name, adapter.FriendlyName, sizeof(interface_name));

  // Fallback to adapter GUID if name failed to convert
  if (bytes_out == -1) {
    strcpy(interface_name, adapter.AdapterName);
  }

  return interface_name;
}

std::vector<std::string> NetworkAdapterManager::GetAdaptersNames() const {
  std::vector<std::string> adapter_names = {};

  for (const auto& adapter : adapter_addresses_) {
    adapter_names.push_back(GetAdapterFriendlyName(adapter));
  }

  return adapter_names;
};

std::unique_ptr<MacAddress> NetworkAdapterManager::GetAdapterMacAddressFromGUID(
    const std::string guid) const {
  const auto adapter = GetAdapterFromGUID(guid);

  if (adapter.has_value() &&
      adapter->PhysicalAddressLength == MacAddress::MacAddressSize) {
    uint8_t mac_address[MacAddress::MacAddressSize];
    memcpy(mac_address, adapter->PhysicalAddress, MacAddress::MacAddressSize);

    return std::make_unique<MacAddress>(mac_address);
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

  if (adapter.has_value()) {
    return GetAdapterFriendlyName(adapter.value());
  }

  return "";
}

void NetworkAdapterManager::ResetSelectedAdapter() {
  if (!kernel_state()->emulator()->is_title_open()) {
    local_ip_ = {};
    is_WAN_routing_ = false;
    OVERRIDE_string(network_guid, "");
  }
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
  }

  if (cvars::logging) {
    XELOGI("{}", xe::string_util::trim(networks));
  }

  return adapter_addresses;
}

bool NetworkAdapterManager::UpdateNetworkInterface(
    sockaddr_in local_ip, IP_ADAPTER_ADDRESSES adapter) {
  for (PIP_ADAPTER_UNICAST_ADDRESS_LH address = adapter.FirstUnicastAddress;
       address != NULL; address = address->Next) {
    sockaddr_in adapter_addr =
        *reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);

    if (adapter_addr.sin_family == AF_INET) {
      if (cvars::network_guid.empty()) {
        if (local_ip.sin_addr.s_addr == adapter_addr.sin_addr.s_addr ||
            local_ip.sin_addr.s_addr == 0) {
          is_WAN_routing_ =
              (local_ip.sin_addr.s_addr == adapter_addr.sin_addr.s_addr);
          local_ip_ = adapter_addr;
          OVERRIDE_string(network_guid, adapter.AdapterName);
          return true;
        }
      } else {
        is_WAN_routing_ =
            local_ip.sin_addr.s_addr == adapter_addr.sin_addr.s_addr;
        local_ip_ = adapter_addr;
        OVERRIDE_string(network_guid, adapter.AdapterName);
        return true;
      }
    }
  }

  return false;
}

// Sets network_guid
void NetworkAdapterManager::AutoSelectNetworkAdapter(
    const std::vector<IP_ADAPTER_ADDRESSES> adapters) {
  sockaddr_in local_ip = {};

  // If upnp is disabled or upnp_root is empty fallback to winsock
  if (cvars::upnp && !cvars::upnp_root.empty()) {
    local_ip = ip_to_sockaddr(UPnP::GetLocalIP());
  } else {
    local_ip = WinsockGetLocalIP();
  }

  XELOGI("Checking for interface: {}", cvars::network_guid);

  bool updated = false;

  // If existing network GUID exists use it
  for (auto const& adapter : adapters) {
    if (cvars::network_guid == adapter.AdapterName) {
      if (UpdateNetworkInterface(local_ip, adapter)) {
        updated = true;
        break;
      }
    }
  }

  // Find interface that has local_ip
  if (!updated) {
    XELOGI("Network Interface GUID: {} not found!",
           cvars::network_guid.empty() ? "N\\A" : cvars::network_guid);

    for (auto const& adapter : adapters) {
      if (UpdateNetworkInterface(local_ip, adapter)) {
        updated = true;
        break;
      }
    }
  }

  // Use first interface from adapter_addresses, otherwise unspecified network
  if (!updated) {
    // Reset the GUID
    OVERRIDE_string(network_guid, "");

    XELOGI("Interface GUID: {} not found!",
           cvars::network_guid.empty() ? "N\\A" : cvars::network_guid);

    if (cvars::network_guid.empty()) {
      if (!adapters.empty()) {
        auto& adapter = adapters.front();

        UpdateNetworkInterface(local_ip, adapter);
      } else {
        local_ip_ = local_ip;
        // Unspecified Network
      }
    } else {
      // Unspecified Network
    }
  }

  std::string WAN_interface = is_WAN_routing_ ? "(Default)" : "(Non Default)";

  XELOGI("Set network interface: {} {} {} {}", GetSelectedAdapterName(),
         cvars::network_guid, GetSelectedAdapterLocalIP_Str(), WAN_interface);

  assert_false(cvars::network_guid.empty());
}

}  // namespace kernel
}  // namespace xe
