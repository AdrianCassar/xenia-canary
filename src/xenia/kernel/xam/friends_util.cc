/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/rapidcsv/src/rapidcsv.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/xam/friends_util.h"
#include "xenia/kernel/xnet.h"

DEFINE_string(friends_xuids, "", "Comma delimited list of XUIDs. (Max 100)",
              "Live");

namespace xe {
namespace kernel {

std::vector<std::string> ParseDelimitedList(std::string_view csv,
                                            uint32_t count) {
  std::vector<std::string> parsed_list;

  std::stringstream sstream(csv.data());

  rapidcsv::Document delimiter(
      sstream, rapidcsv::LabelParams(-1, -1),
      rapidcsv::SeparatorParams(',', true), rapidcsv::ConverterParams(),
      rapidcsv::LineReaderParams(true /* pSkipCommentLines */,
                                 '#' /* pCommentPrefix */,
                                 true /* pSkipEmptyLines */));

  if (!delimiter.GetRowCount()) {
    return parsed_list;
  }

  parsed_list = delimiter.GetRow<std::string>(0);

  parsed_list.erase(std::remove_if(parsed_list.begin(), parsed_list.end(),
                                   [](const std::string& element) {
                                     return element.empty();
                                   }),
                    parsed_list.end());

  if (count != 0 && parsed_list.size() > count) {
    parsed_list.resize(count);
  }

  return parsed_list;
}

std::string BuildCSVFromVector(std::vector<std::string>& data, uint32_t count) {
  rapidcsv::Document doc(
      "", rapidcsv::LabelParams(-1, -1), rapidcsv::SeparatorParams(',', true),
      rapidcsv::ConverterParams(),
      rapidcsv::LineReaderParams(true /* pSkipCommentLines */,
                                 '#' /* pCommentPrefix */,
                                 true /* pSkipEmptyLines */));

  std::ostringstream csv;

  if (count != 0 && data.size() > count) {
    data.resize(count);
  }

  doc.InsertRow(0, data);
  doc.Save(csv);

  return xe::string_util::trim(csv.str());
}

std::vector<std::uint64_t> ParseFriendsXUIDs() {
  const auto& xuids = cvars::friends_xuids;

  const std::vector<std::string> friends_xuids =
      ParseDelimitedList(xuids, X_ONLINE_MAX_FRIENDS);

  std::vector<std::uint64_t> xuids_parsed;

  uint32_t index = 0;
  for (const auto& friend_xuid : friends_xuids) {
    const uint64_t xuid = string_util::from_string<uint64_t>(
        xe::string_util::trim(friend_xuid), true);

    if (xuid == 0) {
      XELOGI("{}: Skip adding invalid friend XUID!", __func__);
      continue;
    }

    if (index == 0 && xuid <= X_ONLINE_MAX_FRIENDS) {
      XLiveAPI::dummy_friends_count_ = static_cast<uint32_t>(xuid);

      index++;
      continue;
    }

    xuids_parsed.push_back(xuid);

    index++;
  }

  return xuids_parsed;
}

void AddFriendToConfig(uint64_t xuid) {
  const auto delimeter = cvars::friends_xuids.empty() ? "" : ",";
  const auto& xuids =
      cvars::friends_xuids + fmt::format("{}{:016X}", delimeter, xuid);

  std::vector<std::string> friend_xuids =
      ParseDelimitedList(xuids, X_ONLINE_MAX_FRIENDS);

  // Remove duplicate xuids
  std::sort(friend_xuids.begin(), friend_xuids.end());
  friend_xuids.erase(std::unique(friend_xuids.begin(), friend_xuids.end()),
                     friend_xuids.end());

  const std::string friends_list =
      BuildCSVFromVector(friend_xuids, X_ONLINE_MAX_FRIENDS);

  OVERRIDE_string(friends_xuids, friends_list);
}

void RemoveFriendFromConfig(uint64_t xuid) {
  auto xuid_str = fmt::format("{:016X}", xuid);

  std::vector<std::string> friend_xuids =
      ParseDelimitedList(cvars::friends_xuids, X_ONLINE_MAX_FRIENDS);

  friend_xuids.erase(
      std::remove(friend_xuids.begin(), friend_xuids.end(), xuid_str),
      friend_xuids.end());

  const std::string friends_list =
      BuildCSVFromVector(friend_xuids, X_ONLINE_MAX_FRIENDS);

  OVERRIDE_string(friends_xuids, friends_list);
}

}  // namespace kernel
}  // namespace xe
