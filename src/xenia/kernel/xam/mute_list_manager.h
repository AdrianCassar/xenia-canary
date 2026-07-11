/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_MUTE_LIST_MANAGER_H_
#define XENIA_KERNEL_XAM_MUTE_LIST_MANAGER_H_

#include <optional>
#include <set>

#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {
class ProfileManager;
}  // namespace xam
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

class MuteListManager {
 public:
  MuteListManager(KernelState* kernel_state, ProfileManager* profile_manager);

  ~MuteListManager() = default;

  bool AddMuteListUser(const uint64_t xuid, const uint64_t remote_xuid) const;

  bool RemoveMuteListUser(const uint64_t xuid,
                          const uint64_t remote_xuid) const;

  bool QueryMuteListUser(const uint64_t xuid,
                         const uint64_t remote_talker) const;

  void ResetMuteList(const uint64_t xuid) const;

  void RebuildMuteList(const uint64_t xuid) const;

  bool FindMuteListUser(uint64_t xuid, const uint64_t remote_xuid) const;

  KernelState* kernel_state_;
  ProfileManager* profile_manager_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_MUTE_LIST_MANAGER_H_
