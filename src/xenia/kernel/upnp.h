/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <map>
#include <shared_mutex>

#include <third_party/miniupnp/miniupnpc/include/miniupnpc.h>
#include <xenia/base/threading_timer_queue.h>

namespace xe {
namespace kernel {
class upnp {
 public:
  ~upnp();

  void upnp_init();

  void add_port(std::string_view addr, uint16_t internal_port,
                std::string_view protocol);

  void remove_port(uint16_t internal_port, std::string_view protocol);

  void remove_port_external(uint16_t external_port, std::string_view protocol,
                            bool verbose = true);

  void refresh_ports(std::string_view addr);

  bool is_active() const;

  uint16_t upnp::get_mapped_connect_port(uint16_t port);

  uint16_t upnp::get_mapped_bind_port(uint16_t external_port);

  std::map<uint16_t, uint16_t>* mapped_connect_ports() {
    return &m_mapped_connect_ports;
  };

  std::map<uint16_t, uint16_t>* mapped_bind_ports() {
    return &m_mapped_bind_ports;
  };

 private:
  std::atomic<bool> m_active = false;

  std::weak_ptr<xe::threading::TimerQueueWaitItem> wait_item_;

  std::shared_mutex m_mutex;
  IGDdatas m_igd_data{};
  UPNPUrls m_igd_urls{};

  std::map<std::string, std::map<uint16_t, uint16_t>> m_port_bindings;

  std::map<uint16_t, uint16_t> m_mapped_connect_ports;
  std::map<uint16_t, uint16_t> m_mapped_bind_ports;

  void refresh_ports_timer();
};
}  // namespace kernel
}  // namespace xe
