/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/network_adapter_manager_linux.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/xbox.h"

#include <net/if.h>

#include <arpa/inet.h>
#include <net/if_arp.h>
#include <netinet/in.h>

#include <linux/if_packet.h>
#include <linux/rtnetlink.h>

DEFINE_string(network_interface, "", "Network Interface Identifier", "Live");

namespace xe {
namespace kernel {

NetworkAdapterManager::NetworkAdapterManager() {}

void NetworkAdapterManager::Initialize() {
  adapter_identifiers_ = DiscoverNetworkAdapters();

  if (!adapter_identifiers_.empty()) {
    // In-case our selected adapter was removed during runtime reselect best
    // interface index.
    std::optional<std::string> best_interface_name = GetBestInterface();

    if (best_interface_name.has_value()) {
      best_interface_name_ = best_interface_name.value();
      AutoSelectNetworkAdapter(best_interface_name_);
    }
  }
}

void NetworkAdapterManager::SelectBestInterface() {
  ResetSelectedAdapter();
  Initialize();
}

void NetworkAdapterManager::SetSelectedAdapterIdentifier(
    std::string interface_identifier) {
  ResetSelectedAdapter();

  const auto adapter = GetAdapterName(interface_identifier);

  if (adapter.has_value()) {
    UpdateNetworkInterface(adapter.value());
  }

  XELOGI(GetSelectedAdapterDescription());
}

std::string NetworkAdapterManager::GetSelectedAdapterIdentifier() const {
  return cvars::network_interface;
}

std::string NetworkAdapterManager::GetSelectedAdapterDescription() const {
  return fmt::format("Network Interface: {} {} {}",
                     GetSelectedAdapter().value(),
                     GetSelectedAdapterLocalIPString(),
                     is_WAN_routing_ ? "(WAN Routing)" : "(Non-WAN Routing)");
}

std::optional<std::string> NetworkAdapterManager::SearchUdevDevices(
    udev_enumerate* enumerate) const {
  udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry* entry = nullptr;

  udev_list_entry_foreach(entry, devices) {
    const char* syspath = udev_list_entry_get_name(entry);
    struct udev* udev_ctx = udev_enumerate_get_udev(enumerate);
    struct udev_device* dev = udev_device_new_from_syspath(udev_ctx, syspath);

    if (!dev) {
      continue;
    }

    const char* sysname = udev_device_get_sysname(dev);

    if (sysname) {
      std::string result(sysname);
      udev_device_unref(dev);

      return result;
    }

    udev_device_unref(dev);
  }

  return std::nullopt;
}

std::optional<std::string> NetworkAdapterManager::GetAdapterName(
    std::string interface_identifier) const {
  udev* udev = udev_new();

  if (!udev) {
    return std::nullopt;
  }

  udev_enumerate* enumerate = udev_enumerate_new(udev);
  if (!enumerate) {
    udev_unref(udev);
    return std::nullopt;
  }

  // Filter by ID Path
  udev_enumerate_add_match_subsystem(enumerate, "net");
  udev_enumerate_add_match_property(enumerate, "ID_NET_NAME",
                                    interface_identifier.c_str());
  udev_enumerate_scan_devices(enumerate);

  std::optional<std::string> result = SearchUdevDevices(enumerate);

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  return result;
};

std::optional<std::string> NetworkAdapterManager::GetSelectedAdapter() const {
  return GetAdapterName(cvars::network_interface);
}

std::string NetworkAdapterManager::GetSelectedAdapterLocalIPString() const {
  return ip_to_string(local_ip_);
}

std::unordered_map<std::string, std::string>
NetworkAdapterManager::GetAdaptersIdentifiers() const {
  std::unordered_map<std::string, std::string> identifiers = {};

  for (const auto& interface_identifier : adapter_identifiers_) {
    const std::optional<std::string> name =
        GetAdapterName(interface_identifier);

    if (name.has_value()) {
      identifiers[interface_identifier] = name.value();
    }
  }

  return identifiers;
}

bool NetworkAdapterManager::IsInterfaceSelected() const {
  return local_ip_.sin_addr.s_addr;
}

void NetworkAdapterManager::ResetSelectedAdapter() {
  local_ip_ = {};
  is_WAN_routing_ = false;
  OVERRIDE_string(network_interface, "");
}

std::optional<std::string> NetworkAdapterManager::GetAdapterIdentifier(
    std::string name) {
  std::optional<std::string> identifier = std::nullopt;

  udev* udev = udev_new();
  udev_device* dev =
      udev_device_new_from_subsystem_sysname(udev, "net", name.c_str());

  if (dev) {
    const char* id_net_name =
        udev_device_get_property_value(dev, "ID_NET_NAME");

    if (id_net_name) {
      identifier = id_net_name;
      udev_device_unref(dev);
    }
  }

  udev_unref(udev);

  return identifier;
}

std::vector<std::string> NetworkAdapterManager::DiscoverNetworkAdapters() {
  ifaddrs* interfaces = nullptr;

  if (getifaddrs(&interfaces) == -1) {
    return {};
  }

  std::vector<std::string> adapters = {};

  std::string networks = "Network Interfaces:\n";

  for (ifaddrs* interface = interfaces; interface;
       interface = interface->ifa_next) {
    // Check if the interface if up
    if (!(interface->ifa_flags & IFF_UP)) {
      continue;
    }

    if ((interface->ifa_flags & IFF_LOOPBACK)) {
      continue;
    }

    if (interface->ifa_addr == nullptr) {
      continue;
    }

    if (interface->ifa_addr->sa_family == AF_INET6) {
      continue;
    }

    // Use AF_PACKET so we can get lower-level details about the interface and
    // its physical layer
    if (interface->ifa_addr->sa_family == AF_PACKET) {
      // const sockaddr_ll* sockaddr =
      //     reinterpret_cast<const sockaddr_ll*>(interface->ifa_addr);
      // MacAddress mac(reinterpret_cast<const uint8_t*>(sockaddr->sll_addr));

      // if (mac.to_uint64()) {
      //   XELOGI(mac.to_printable_form());
      // }

      continue;
    }

    const char* name = interface->ifa_name;

    if (interface->ifa_addr->sa_family == AF_INET) {
      const auto identifier = GetAdapterIdentifier(name);

      if (identifier.has_value()) {
        const sockaddr_in* sockaddr =
            reinterpret_cast<const sockaddr_in*>(interface->ifa_addr);

        networks +=
            fmt::format("{}: {}\n", name, ip_to_string(sockaddr->sin_addr));

        adapters.push_back(identifier.value());
      } else {
        XELOGI("Found valid interface {} without unique identifier!", name);
      }
    }
  }

  networks = xe::string_util::trim(networks);

  if (adapters.empty()) {
    XELOGI("No network interfaces detected!\n");
  } else {
    XELOGI("Found {} network interface(s)!\n", adapters.size());
    XELOGI(networks);
  }

  freeifaddrs(interfaces);

  return adapters;
}

bool NetworkAdapterManager::UpdateNetworkInterface(std::string name) {
  const std::optional<sockaddr_in> adapter_addr = GetInterfaceIPFromName(name);
  const std::optional<std::string> identifier = GetAdapterIdentifier(name);

  bool updated = adapter_addr.has_value() && identifier.has_value();

  if (updated) {
    local_ip_ = adapter_addr.value();
    is_WAN_routing_ = IsInterfaceWANRouting(adapter_addr.value());
    OVERRIDE_string(network_interface, identifier.value());
  }

  return updated;
}

std::optional<std::string> NetworkAdapterManager::GetBestInterface() {
  const size_t NetlinkBufferSize = 8192;
  const size_t RequestBufferSize = 256;

  int nl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

  if (nl_fd < 0) {
    return std::nullopt;
  }

  alignas(NLMSG_ALIGNTO) std::array<char, RequestBufferSize> req_buf = {0};

  auto* nlh = reinterpret_cast<nlmsghdr*>(req_buf.data());
  nlh->nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
  nlh->nlmsg_flags = NLM_F_REQUEST;
  nlh->nlmsg_type = RTM_GETROUTE;

  auto* rtm = reinterpret_cast<rtmsg*>(NLMSG_DATA(nlh));
  rtm->rtm_family = AF_INET;

  // Append destination IP attribute (RTA_DST)
  auto* rta =
      reinterpret_cast<rtattr*>(req_buf.data() + NLMSG_ALIGN(nlh->nlmsg_len));
  rta->rta_type = RTA_DST;
  rta->rta_len = RTA_LENGTH(sizeof(in_addr));

  if (inet_pton(AF_INET, "8.8.8.8", RTA_DATA(rta)) <= 0) {
    close(nl_fd);
    return std::nullopt;
  }

  nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rta->rta_len);

  // Send route query to the kernel (req_buf.data() passes the correct data
  // pointer)
  if (send(nl_fd, req_buf.data(), nlh->nlmsg_len, 0) < 0) {
    close(nl_fd);
    return std::nullopt;
  }

  // Allocate receiving buffer using a vector for safety
  std::vector<char> reply_buf(NetlinkBufferSize);
  ssize_t len = recv(nl_fd, reply_buf.data(), reply_buf.size(), 0);

  // Always close the socket descriptor immediately after network operations
  // complete
  close(nl_fd);

  if (len < 0) {
    return std::nullopt;
  }

  auto* reply_nlh = reinterpret_cast<nlmsghdr*>(reply_buf.data());

  if (reply_nlh->nlmsg_type == RTM_NEWROUTE) {
    auto* reply_rtm = reinterpret_cast<rtmsg*>(NLMSG_DATA(reply_nlh));
    int rlen = RTM_PAYLOAD(reply_nlh);

    for (auto* attr = reinterpret_cast<rtattr*>(RTM_RTA(reply_rtm));
         RTA_OK(attr, rlen); attr = RTA_NEXT(attr, rlen)) {
      if (attr->rta_type == RTA_OIF) {
        int if_index = 0;
        std::memcpy(&if_index, RTA_DATA(attr), sizeof(int));

        char name_buf[IF_NAMESIZE] = {};

        if (if_indextoname(if_index, name_buf) != nullptr) {
          return std::string(name_buf);
        }

        return std::nullopt;
      }
    }
  }

  return std::nullopt;
}

bool NetworkAdapterManager::IsInterfaceWANRouting(sockaddr_in interface_addr) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (sock == X_SOCKET_ERROR) {
    return false;
  }

  // Force socket to use interface address for connection attempt.
  if (bind(sock, reinterpret_cast<sockaddr*>(&interface_addr),
           sizeof(sockaddr)) == X_SOCKET_ERROR) {
    close(sock);
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
                         sizeof(remoteAddr)) != X_SOCKET_ERROR;

  close(sock);

  return success;
}

std::optional<sockaddr_in> NetworkAdapterManager::GetInterfaceIPFromName(
    std::string name) const {
  ifaddrs* interfaces = nullptr;

  if (getifaddrs(&interfaces) == -1) {
    return std::nullopt;
  }

  std::optional<sockaddr_in> adapter_sockaddr = std::nullopt;

  for (ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (strcmp(ifa->ifa_name, name.c_str()) == 0 &&
        ifa->ifa_addr->sa_family == AF_INET) {
      adapter_sockaddr = *reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
      break;
    }
  }

  freeifaddrs(interfaces);

  return adapter_sockaddr;
}

// Select our saved network interface if it's available.
bool NetworkAdapterManager::SelectSavedNetworkAdapter() {
  const std::optional<std::string> adapter = GetSelectedAdapter();

  if (adapter.has_value()) {
    return UpdateNetworkInterface(adapter.value());
  } else if (!cvars::network_interface.empty()) {
    XELOGI("Interface Identifier: {} not found!", cvars::network_interface);
  }

  return false;
}

// Select saved network interface if available, otherwise fallback to best
// interface.
void NetworkAdapterManager::AutoSelectNetworkAdapter(
    std::string best_interface_name) {
  bool selected = SelectSavedNetworkAdapter();

  // Fallback to best interface.
  if (!selected) {
    selected = UpdateNetworkInterface(best_interface_name);
  }

  if (selected) {
    XELOGI(GetSelectedAdapterDescription());
  } else {
    XELOGI("Unspecified Network Interface!");
  }
}

}  // namespace kernel
}  // namespace xe
