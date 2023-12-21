/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/upnp.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

#include <third_party/miniupnp/miniupnpc/include/miniwget.h>
#include <third_party/miniupnp/miniupnpc/include/upnpcommands.h>

DEFINE_string(upnp_root, "", "UPnP Root Device", "Live");

DEFINE_bool(upnp, false, "Automatically port forward using UPnP", "Live");

DECLARE_bool(logging);

using namespace xe::threading;

// Credit:
// https://github.com/RPCS3/rpcs3/blob/05b6108c66b2cb5bdbb97ff2412d4d39aa7dc331/rpcs3/Emu/NP/upnp_handler.cpp
namespace xe {
namespace kernel {

upnp::~upnp() {
  std::lock_guard lock(m_mutex);

  for (const auto& [protocol, m_prot_bindings] : m_port_bindings) {
    for (const auto& [internal_port, external_port] : m_prot_bindings) {
      remove_port_external(external_port, protocol);
    }
  }

  m_active = false;
}

void upnp::upnp_init() {
  std::lock_guard lock(m_mutex);

  auto check_igd = [&](const char* url) -> bool {
    int desc_xml_size = 0;
    int status_code = 0;

    m_igd_data = {};
    m_igd_urls = {};

    char* desc_xml =
        static_cast<char*>(miniwget(url, &desc_xml_size, 1, &status_code));

    if (!desc_xml) return false;

    parserootdesc(desc_xml, desc_xml_size, &m_igd_data);
    free(desc_xml);
    desc_xml = nullptr;
    GetUPNPUrls(&m_igd_urls, &m_igd_data, url, 1);

    return true;
  };

  std::string dev_url = cvars::upnp_root;

  if (!dev_url.empty()) {
    if (check_igd(dev_url.c_str())) {
      XELOGI("Saved UPNP({}) enabled", dev_url);
      refresh_ports_timer();

      m_active = true;
      return;
    }

    XELOGI("Saved UPNP({}) isn't available anymore", dev_url);
  }

  XELOGI("Starting UPNP search");

  int error;
  UPNPDev* devlist = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);

  if (error) {
    XELOGE("UPNP Error Code: {}", error);
    return;
  }

  if (!devlist) {
    XELOGE("No UPNP device was found");
    return;
  }

  const UPNPDev* dev = devlist;

  for (; dev; dev = dev->pNext) {
    if (dev != NULL) {
      if (strstr(dev->st, "InternetGatewayDevice")) break;
    }
  }

  if (dev == NULL) {
    XELOGE("No UPNP device was found");
    return;
  }

  int desc_xml_size = 0;
  int status_code = 0;

  char* desc_xml = static_cast<char*>(
      miniwget(dev->descURL, &desc_xml_size, 1, &status_code));

  if (dev) {
    int desc_xml_size = 0;
    int status_code = 0;
    char* desc_xml = static_cast<char*>(
        miniwget(dev->descURL, &desc_xml_size, 1, &status_code));

    if (desc_xml) {
      m_igd_data = {};
      m_igd_urls = {};
      parserootdesc(desc_xml, desc_xml_size, &m_igd_data);
      free(desc_xml);
      desc_xml = nullptr;
      GetUPNPUrls(&m_igd_urls, &m_igd_data, dev->descURL, 1);

      XELOGI("Found UPnP device type : {} at {}", dev->st, dev->descURL);

      cvars::upnp_root = dev->descURL;
      OVERRIDE_string(upnp_root, cvars::upnp_root);

      refresh_ports_timer();

      m_active = true;
    } else {
      XELOGE("Failed to retrieve UPNP xml for {}", dev->descURL);
    }
  } else {
    XELOGE("No UPNP IGD device was found", dev->descURL);
  }

  freeUPNPDevlist(devlist);
};

void upnp::add_port(std::string_view addr, uint16_t internal_port,
                    std::string_view protocol) {
  if (!m_active) return;

  std::lock_guard lock(m_mutex);

  internal_port = get_mapped_bind_port(internal_port);

  const uint16_t external_port = internal_port;
  const std::string internal_port_str = fmt::format("{}", internal_port);
  const std::string external_port_str = fmt::format("{}", external_port);

  // https://openconnectivity.org/developer/specifications/upnp-resources/upnp/internet-gateway-device-igd-v-2-0/
  // http://upnp.org/specs/gw/UPnP-gw-WANIPConnection-v2-Service.pdf
  const uint32_t OnlyPermanentLeasesSupported = 725;

  int res = 0;

  auto run = [&]() {
    const std::string lease_duration = m_leases_supported ? "3600" : "0";  // 1h

    res = UPNP_AddPortMapping(
        m_igd_urls.controlURL, m_igd_data.first.servicetype,
        external_port_str.c_str(), internal_port_str.c_str(), addr.data(),
        "Xenia", protocol.data(), nullptr, lease_duration.c_str());

    if (res == OnlyPermanentLeasesSupported) {
      XELOGI("Router only supports permanent lease times on port mappings.");

      m_leases_supported = false;
      return;
    };

    if (res == UPNPCOMMAND_SUCCESS) {
      const auto& is_bound =
          m_port_bindings[std::string(protocol)].find(internal_port);

      bool update = false;

      if (update = (is_bound == m_port_bindings[std::string(protocol)].end())) {
        m_port_bindings[std::string(protocol)][internal_port] = external_port;
      }

      XELOGI("Successfully {} {}:{}({}) to IGD:{}",
             update ? "bound" : "updated", addr, internal_port, protocol,
             external_port);
    } else {
      XELOGI("Failed to bind port!!! {}:{}({}) to IGD:{}", addr, internal_port,
             protocol, external_port);

      XELOGI("UPnP error code {}", res);
    }

    m_port_binding_results[std::string(protocol)][external_port] = res;
  };

  run();

  if (res == OnlyPermanentLeasesSupported) {
    run();
  }
}

void upnp::remove_port(uint16_t internal_port, std::string_view protocol) {
  if (!m_active) return;

  std::lock_guard lock(m_mutex);

  internal_port = get_mapped_bind_port(internal_port);

  const std::string str_protocol(protocol);

  if (&m_port_bindings.at(str_protocol) ||
      m_port_bindings.at(str_protocol).at(internal_port)) {
    XELOGE("Tried to unbind port mapping {} to IGD({}) but it isn't bound",
           internal_port, protocol);
    return;
  }

  const uint16_t external_port =
      m_port_bindings.at(str_protocol).at(internal_port);

  remove_port_external(external_port, protocol);

  // assert_true(m_port_bindings.at(str_protocol).erase(internal_port));
  // assert_true(m_mapped_bind_ports.erase(internal_port));

  XELOGE("Successfully deleted port mapping {} to IGD:{}({})", internal_port,
         external_port, protocol);
}

void upnp::remove_port_external(uint16_t external_port,
                                std::string_view protocol, bool verbose) {
  const std::string str_ext_port = fmt::format("{}", external_port);

  if (int res = UPNP_DeletePortMapping(
          m_igd_urls.controlURL, m_igd_data.first.servicetype,
          str_ext_port.c_str(), protocol.data(), nullptr);
      res != 0 && verbose)

    XELOGE("Failed to delete port mapping IGD:{}({}): {}", str_ext_port,
           protocol, res);
}

void upnp::refresh_ports(std::string_view addr) {
  if (!m_leases_supported) {
    return;
  }

  for (const auto& [protocol, m_prot_bindings] : m_port_bindings) {
    for (const auto& [internal_port, external_port] : m_prot_bindings) {
      add_port(addr, external_port, protocol);
    }
  }
}

// Update the UPnP lease time every 45 minutes
// Not using HighResolutionTimer because it's only used for small tasks.
void upnp::refresh_ports_timer() {
  // Only setup timer once
  if (m_active) return;

  char lanaddr[64] = "";
  int size = 0;
  miniwget_getaddr(cvars::upnp_root.c_str(), &size, lanaddr, sizeof(lanaddr), 0,
                   NULL);

  // 45 minutes
  const auto interval = std::chrono::milliseconds(2700 * 1000);

  wait_item_ =
      QueueTimerRecurring([&](void*) { refresh_ports(lanaddr); }, nullptr,
                          TimerQueueWaitItem::clock::now(), interval);
}

uint16_t upnp::get_mapped_connect_port(uint16_t external_port) {
  if (m_mapped_connect_ports.find(external_port) !=
      m_mapped_connect_ports.end()) {
    return m_mapped_connect_ports[external_port];
  }

  if (m_mapped_connect_ports.find(0) != m_mapped_connect_ports.end()) {
    if (cvars::logging) {
      XELOGI("Using wildcard connect port for guest port {}!", external_port);
    }

    return m_mapped_connect_ports[0];
  }

  if (cvars::logging) {
    XELOGW("No mapped connect port found for {}!", external_port);
  }

  return external_port;
}

uint16_t upnp::get_mapped_bind_port(uint16_t external_port) {
  if (m_mapped_bind_ports.find(external_port) != m_mapped_bind_ports.end()) {
    return m_mapped_bind_ports[external_port];
  }

  if (m_mapped_bind_ports.find(0) != m_mapped_bind_ports.end()) {
    if (cvars::logging) {
      XELOGI("Using wildcard bind port for guest port {}!", external_port);
    }

    return m_mapped_bind_ports[0];
  }

  if (cvars::logging) {
    XELOGW("No mapped bind port found for {}!", external_port);
  }

  return external_port;
}

bool upnp::is_active() const { return m_active; }

}  // namespace kernel
}  // namespace xe