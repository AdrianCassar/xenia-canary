/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_
#define XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class LeaderboardObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  LeaderboardObjectJSON();

  LeaderboardObjectJSON(view_properties_unordered_map stats);

  virtual ~LeaderboardObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const view_properties_unordered_map& GetStatsToWrite() const {
    return stats_;
  }

  const X_USER_STATS_READ_RESULTS& GetReadStatsResults() const {
    return read_results_;
  }

 private:
  view_properties_unordered_map stats_;
  X_USER_STATS_READ_RESULTS read_results_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_
