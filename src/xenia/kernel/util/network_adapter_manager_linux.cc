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
#include "xenia/base/string_util.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <linux/rtnetlink.h>

DEFINE_string(network_interface, "", "Network Interface Identifier", "Live");

namespace xe {
namespace kernel {

namespace {

constexpr size_t kNetlinkBufferSize = 32768;
constexpr const char* kWANTestAddress = "8.8.8.8";  // Google DNS

// Opens a NETLINK_ROUTE socket with a receive timeout so a missing reply can't
// hang the caller.
int OpenNetlinkRouteSocket() {
  int nl_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);

  if (nl_fd < 0) {
    return -1;
  }

  timeval timeout = {};
  timeout.tv_sec = 3;
  setsockopt(nl_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  return nl_fd;
}

// Returns true if the route is a unicast route out of if_index that covers
// dest_addr (host byte order).
bool RouteReachesAddressViaInterface(const nlmsghdr* nlh, uint32_t if_index,
                                     uint32_t dest_addr) {
  const auto* rtm = reinterpret_cast<const rtmsg*>(NLMSG_DATA(nlh));

  if (rtm->rtm_family != AF_INET || rtm->rtm_type != RTN_UNICAST) {
    return false;
  }

  uint32_t route_dst = 0;
  bool uses_interface = false;

  int rlen = RTM_PAYLOAD(nlh);

  for (auto* attr = RTM_RTA(rtm); RTA_OK(attr, rlen);
       attr = RTA_NEXT(attr, rlen)) {
    switch (attr->rta_type) {
      case RTA_DST:
        std::memcpy(&route_dst, RTA_DATA(attr), sizeof(route_dst));
        break;
      case RTA_OIF: {
        int oif = 0;
        std::memcpy(&oif, RTA_DATA(attr), sizeof(oif));
        uses_interface |= static_cast<uint32_t>(oif) == if_index;
        break;
      }
      case RTA_MULTIPATH: {
        // ECMP routes list each next hop's interface separately.
        auto* nh = reinterpret_cast<rtnexthop*>(RTA_DATA(attr));
        int nh_len = RTA_PAYLOAD(attr);

        while (RTNH_OK(nh, nh_len)) {
          uses_interface |= static_cast<uint32_t>(nh->rtnh_ifindex) == if_index;
          nh_len -= RTNH_ALIGN(nh->rtnh_len);
          nh = RTNH_NEXT(nh);
        }
        break;
      }
    }
  }

  if (!uses_interface) {
    return false;
  }

  if (rtm->rtm_dst_len == 0) {
    return true;
  }

  const uint32_t mask =
      rtm->rtm_dst_len >= 32 ? 0xFFFFFFFF : ~(0xFFFFFFFFu >> rtm->rtm_dst_len);

  return (ntohl(route_dst) & mask) == (dest_addr & mask);
}

}  // namespace

NetworkAdapterManager::NetworkAdapterManager() {}

void NetworkAdapterManager::Initialize() {
  adapter_identifiers_ = DiscoverNetworkAdapters();

  if (!adapter_identifiers_.empty()) {
    // In-case our selected adapter was removed during runtime reselect best
    // interface index.
    std::optional<std::string> best_interface_name = GetBestInterface();

    // If we cannot find a best interface then default to first interface.
    if (!best_interface_name.has_value()) {
      best_interface_name = GetAdapterName(adapter_identifiers_.front());
    }

    best_interface_name_ = best_interface_name.value_or("");
    AutoSelectNetworkAdapter(best_interface_name_);
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

  XELOGI("{}", GetSelectedAdapterDescription());
}

std::string NetworkAdapterManager::GetSelectedAdapterIdentifier() const {
  return cvars::network_interface;
}

std::string NetworkAdapterManager::GetSelectedAdapterDescription() const {
  return fmt::format("Network Interface: {} {} {}",
                     GetSelectedAdapter().value_or(""),
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
  if (interface_identifier.empty()) {
    return std::nullopt;
  }

  std::optional<std::string> result = std::nullopt;

  udev* udev = udev_new();

  if (udev) {
    udev_enumerate* enumerate = udev_enumerate_new(udev);

    if (enumerate) {
      // Filter by ID Path
      udev_enumerate_add_match_subsystem(enumerate, "net");
      udev_enumerate_add_match_property(enumerate, "ID_NET_NAME",
                                        interface_identifier.c_str());
      udev_enumerate_scan_devices(enumerate);

      result = SearchUdevDevices(enumerate);

      udev_enumerate_unref(enumerate);
    }

    udev_unref(udev);
  }

  // Interfaces without an ID_NET_NAME (VPNs, containers, net.ifnames=0) use
  // their interface name as the identifier.
  if (!result.has_value() && if_nametoindex(interface_identifier.c_str())) {
    result = interface_identifier;
  }

  return result;
}

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

// Prefer udev's ID_NET_NAME, otherwise fallback to the interface name.
std::optional<std::string> NetworkAdapterManager::GetAdapterIdentifier(
    std::string name) {
  if (name.empty()) {
    return std::nullopt;
  }

  std::optional<std::string> identifier = std::nullopt;

  udev* udev = udev_new();

  if (udev) {
    udev_device* dev =
        udev_device_new_from_subsystem_sysname(udev, "net", name.c_str());

    if (dev) {
      const char* id_net_name =
          udev_device_get_property_value(dev, "ID_NET_NAME");

      if (id_net_name) {
        identifier = id_net_name;
      }

      udev_device_unref(dev);
    }

    udev_unref(udev);
  }

  if (!identifier.has_value() && if_nametoindex(name.c_str())) {
    identifier = name;
  }

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
    // Check if the interface is up and has a link
    if (!(interface->ifa_flags & IFF_UP) ||
        !(interface->ifa_flags & IFF_RUNNING)) {
      continue;
    }

    if ((interface->ifa_flags & IFF_LOOPBACK)) {
      continue;
    }

    if (interface->ifa_addr == nullptr ||
        interface->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    const char* name = interface->ifa_name;
    const auto identifier = GetAdapterIdentifier(name);

    if (!identifier.has_value()) {
      continue;
    }

    // Interfaces with multiple IPv4 addresses are listed once per address.
    if (std::find(adapters.cbegin(), adapters.cend(), identifier.value()) !=
        adapters.cend()) {
      continue;
    }

    const sockaddr_in* sockaddr =
        reinterpret_cast<const sockaddr_in*>(interface->ifa_addr);

    networks += fmt::format("{}: {}\n", name, ip_to_string(sockaddr->sin_addr));

    adapters.push_back(identifier.value());
  }

  freeifaddrs(interfaces);

  if (adapters.empty()) {
    XELOGI("No network interfaces detected!\n");
  } else {
    XELOGI("Found {} network interface(s)!\n", adapters.size());
    XELOGI("{}", xe::string_util::trim(networks));
  }

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

// Equivalent of "ip route get 8.8.8.8", returns the outgoing interface name.
std::optional<std::string> NetworkAdapterManager::GetBestInterface() {
  int nl_fd = OpenNetlinkRouteSocket();

  if (nl_fd < 0) {
    return std::nullopt;
  }

  struct {
    nlmsghdr nlh;
    rtmsg rtm;
    alignas(RTA_ALIGNTO) char attrs[RTA_SPACE(sizeof(in_addr))];
  } req = {};

  req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
  req.nlh.nlmsg_flags = NLM_F_REQUEST;
  req.nlh.nlmsg_type = RTM_GETROUTE;
  req.nlh.nlmsg_seq = 1;
  req.rtm.rtm_family = AF_INET;
  req.rtm.rtm_dst_len = 32;

  // Append destination IP attribute (RTA_DST)
  static_assert(offsetof(decltype(req), attrs) ==
                NLMSG_ALIGN(NLMSG_LENGTH(sizeof(rtmsg))));
  auto* rta = reinterpret_cast<rtattr*>(req.attrs);
  rta->rta_type = RTA_DST;
  rta->rta_len = RTA_LENGTH(sizeof(in_addr));

  const in_addr wan_addr = ip_to_in_addr(kWANTestAddress);
  std::memcpy(RTA_DATA(rta), &wan_addr, sizeof(wan_addr));

  req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

  if (send(nl_fd, &req, req.nlh.nlmsg_len, 0) < 0) {
    close(nl_fd);
    return std::nullopt;
  }

  std::vector<char> reply_buf(kNetlinkBufferSize);
  ssize_t len = recv(nl_fd, reply_buf.data(), reply_buf.size(), 0);

  close(nl_fd);

  auto* reply_nlh = reinterpret_cast<nlmsghdr*>(reply_buf.data());

  // Also rejects NLMSG_ERROR replies e.g. no route to host.
  if (len < 0 || !NLMSG_OK(reply_nlh, len) ||
      reply_nlh->nlmsg_type != RTM_NEWROUTE) {
    return std::nullopt;
  }

  auto* reply_rtm = reinterpret_cast<rtmsg*>(NLMSG_DATA(reply_nlh));
  int rlen = RTM_PAYLOAD(reply_nlh);

  for (auto* attr = RTM_RTA(reply_rtm); RTA_OK(attr, rlen);
       attr = RTA_NEXT(attr, rlen)) {
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

  return std::nullopt;
}

// An interface is WAN routing if it has a unicast route that reaches the WAN
// test address, e.g. a default route.
bool NetworkAdapterManager::IsInterfaceWANRouting(sockaddr_in interface_addr) {
  const std::optional<uint32_t> if_index =
      GetInterfaceIndexFromIP(interface_addr);

  if (!if_index.has_value()) {
    return false;
  }

  int nl_fd = OpenNetlinkRouteSocket();

  if (nl_fd < 0) {
    return false;
  }

  struct {
    nlmsghdr nlh;
    rtmsg rtm;
  } req = {};

  req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
  req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.nlh.nlmsg_type = RTM_GETROUTE;
  req.nlh.nlmsg_seq = 1;
  req.rtm.rtm_family = AF_INET;

  if (send(nl_fd, &req, req.nlh.nlmsg_len, 0) < 0) {
    close(nl_fd);
    return false;
  }

  const uint32_t wan_addr = ntohl(ip_to_in_addr(kWANTestAddress).s_addr);

  std::vector<char> reply_buf(kNetlinkBufferSize);

  bool routing = false;
  bool done = false;

  // Route dumps are multipart and can span multiple reads.
  while (!done && !routing) {
    ssize_t len = recv(nl_fd, reply_buf.data(), reply_buf.size(), 0);

    if (len <= 0) {
      break;
    }

    for (auto* nlh = reinterpret_cast<nlmsghdr*>(reply_buf.data());
         NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
      if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) {
        done = true;
        break;
      }

      if (nlh->nlmsg_type == RTM_NEWROUTE &&
          RouteReachesAddressViaInterface(nlh, if_index.value(), wan_addr)) {
        routing = true;
        break;
      }
    }
  }

  close(nl_fd);

  return routing;
}

std::optional<uint32_t> NetworkAdapterManager::GetInterfaceIndexFromIP(
    sockaddr_in interface_addr) const {
  ifaddrs* interfaces = nullptr;

  if (getifaddrs(&interfaces) == -1) {
    return std::nullopt;
  }

  std::optional<uint32_t> if_index = std::nullopt;

  for (ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    const auto* addr = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);

    if (addr->sin_addr.s_addr == interface_addr.sin_addr.s_addr) {
      const uint32_t index = if_nametoindex(ifa->ifa_name);

      if (index) {
        if_index = index;
      }

      break;
    }
  }

  freeifaddrs(interfaces);

  return if_index;
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
// interface, then the first discovered interface.
void NetworkAdapterManager::AutoSelectNetworkAdapter(
    std::string best_interface_name) {
  bool selected = SelectSavedNetworkAdapter();

  // Fallback to best interface.
  if (!selected && !best_interface_name.empty()) {
    selected = UpdateNetworkInterface(best_interface_name);
  }

  // Best interface may not be one we discovered e.g. it has no IPv4 address.
  if (!selected) {
    for (const auto& identifier : adapter_identifiers_) {
      const auto name = GetAdapterName(identifier);

      if (name.has_value() && UpdateNetworkInterface(name.value())) {
        selected = true;
        break;
      }
    }
  }

  if (selected) {
    XELOGI("{}", GetSelectedAdapterDescription());
  } else {
    XELOGI("Unspecified Network Interface!");
  }
}

}  // namespace kernel
}  // namespace xe
