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

#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class LeaderboardObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  struct Column {
    uint16_t attribute_id;
    xam::Property property;
  };

  struct Row {
    uint64_t xuid;
    std::vector<Column> columns;

    const Column* FindColumn(uint16_t attribute_id) const;
  };

  struct View {
    uint32_t view_id;
    uint32_t total_rows;
    std::vector<Row> rows;
  };

  LeaderboardObjectJSON();

  // Stats to write
  LeaderboardObjectJSON(view_properties_unordered_map stats);

  virtual ~LeaderboardObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const view_properties_unordered_map& GetStatsToWrite() const {
    return stats_;
  }

  const std::vector<View>& GetViews() const { return views_; }

  // Lays out the views, rows and columns read from the backend into guest
  // memory.
  bool WriteResults(uint32_t results_address, uint32_t results_size) const;

 private:
  view_properties_unordered_map stats_;
  std::vector<View> views_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_
