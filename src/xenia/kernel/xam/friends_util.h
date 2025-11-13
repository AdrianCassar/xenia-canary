/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_FRIENDS_UTIL
#define XENIA_KERNEL_XAM_FRIENDS_UTIL

#include <cstdint>
#include <vector>

namespace xe {
namespace kernel {

static uint32_t dummy_friends_count_ = 0;

std::vector<std::string> ParseDelimitedList(std::string_view csv,
                                            uint32_t count = 0);

std::string BuildCSVFromVector(std::vector<std::string>& data,
                               uint32_t count = 0);

std::vector<std::uint64_t> ParseFriendsXUIDs();

void AddFriendToConfig(uint64_t xuid);

void RemoveFriendFromConfig(uint64_t xuid);

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_FRIENDS_UTIL
