/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_NET_H_
#define XENIA_KERNEL_XAM_XAM_NET_H_

#include <future>
#include <mutex>

namespace xe {
namespace kernel {
namespace xam {

// Very hacky
bool EXPLICIT_XBOXLIVE_KEY = false;

std::vector<std::future<int32_t>> upnp_actions_;

std::map<uint32_t, std::stop_source> qos_lookup_threads;
std::mutex qos_lookup_mutex;

static void CleanupUPnPActions();

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_NET_H_
