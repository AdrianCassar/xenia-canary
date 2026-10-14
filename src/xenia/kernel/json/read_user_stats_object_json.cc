/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <ranges>

#include "xenia/emulator.h"
#include "xenia/kernel/json/read_user_stats_object_json.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
ReadUserStatsObjectJSON::ReadUserStatsObjectJSON(
    const XGI_XUSER_READ_STATS& read_stats) {
  title_id_ = read_stats.title_id ? read_stats.title_id.get()
                                  : kernel_state()->title_id();

  const xe::be<uint64_t>* xuids_ptr =
      kernel_memory()->TranslateVirtual<xe::be<uint64_t>*>(
          read_stats.xuids_ptr);

  const uint32_t xuids_count =
      std::min<uint32_t>(read_stats.xuids_count, X_STATS_MAX_USER_COUNT);

  xuids_ = std::vector<uint64_t>(xuids_ptr, xuids_ptr + xuids_count);

  const X_USER_STATS_SPEC* specs_ptr =
      kernel_memory()->TranslateVirtual<X_USER_STATS_SPEC*>(
          read_stats.specs_ptr);

  specs_ = std::vector<X_USER_STATS_SPEC>(specs_ptr,
                                          specs_ptr + read_stats.specs_count);
}

ReadUserStatsObjectJSON::ReadUserStatsObjectJSON(
    uint32_t title_id, std::vector<X_USER_STATS_SPEC> specs,
    EnumerateParams params)
    : title_id_(title_id ? title_id : kernel_state()->title_id()),
      specs_(specs),
      enumerate_params_(params) {}

ReadUserStatsObjectJSON::~ReadUserStatsObjectJSON() {}

bool ReadUserStatsObjectJSON::Deserialize(const rapidjson::Value& obj) {
  return false;
}

bool ReadUserStatsObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("title_id");
  writer->String(fmt::format("{:08X}", title_id_));

  if (enumerate_params_.has_value()) {
    const EnumerateParams& params = enumerate_params_.value();

    writer->String("type");

    switch (params.type) {
      case X_STATS_ENUMERATOR_TYPE::XUID: {
        writer->String("xuid");

        writer->String("xuid");
        writer->String(fmt::format("{:016X}", params.xuid));
      } break;
      case X_STATS_ENUMERATOR_TYPE::RANK:
      case X_STATS_ENUMERATOR_TYPE::RANK_PER_SPEC: {
        writer->String("rank");

        writer->String("rank");
        writer->Uint(params.rank);
      } break;
      case X_STATS_ENUMERATOR_TYPE::BY_RATING: {
        writer->String("rating");

        // Signed 64-bit integers are sent as strings to avoid precision loss.
        writer->String("rating");
        writer->String(std::to_string(params.rating));
      } break;
    }

    writer->String("num_rows");
    writer->Uint(params.num_rows);
  } else {
    writer->String("players");

    writer->StartArray();

    for (const uint64_t& xuid : xuids_) {
      // 41560817 and 584107F2 provide null XUID therefore skip them.
      if (xuid) {
        writer->String(fmt::format("{:016X}", xuid));
      }
    }

    writer->EndArray();
  }

  writer->String("leaderboards");

  writer->StartArray();
  for (const auto& spec : specs_) {
    writer->StartObject();

    writer->String("view_id");
    writer->Uint(spec.view_id);

    writer->String("column_ids");
    writer->StartArray();
    const uint32_t num_column_ids = std::min<uint32_t>(
        spec.num_column_ids, X_USER_STATS_ATTRIBUTES_IN_SPEC);

    // Write columns attributes in order.
    const std::vector<uint32_t> column_ids(
        std::begin(spec.column_ids),
        std::begin(spec.column_ids) + num_column_ids);

    for (const auto& column_id : column_ids) {
      writer->Uint(column_id);
    }

    // System columns used to populate each row's rank, rating and gamertag.
    for (const uint32_t system_column_id :
         {xam::RankAttributeId, xam::RatingAttributeId,
          xam::GamertagAttributeId}) {
      if (std::ranges::find(column_ids, system_column_id) == column_ids.end()) {
        writer->Uint(system_column_id);
      }
    }

    writer->EndArray();

    writer->EndObject();
  }
  writer->EndArray();

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe
