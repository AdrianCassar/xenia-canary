/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_LIVE_HIVE_H_
#define XENIA_KERNEL_XAM_XAM_LIVE_HIVE_H_

#include <optional>
#include <string_view>

namespace xe {
namespace kernel {
namespace xam {

std::optional<std::string_view> GetLiveHiveValue(std::string_view name);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_LIVE_HIVE_H_
