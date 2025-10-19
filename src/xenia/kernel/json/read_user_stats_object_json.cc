/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/read_user_stats_object_json.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
ReadUserStatsObjectJSON::ReadUserStatsObjectJSON(XGI_XUSER_READ_STATS stats) {
  read_stats_ = stats;
}

ReadUserStatsObjectJSON::~ReadUserStatsObjectJSON() {}

bool ReadUserStatsObjectJSON::Deserialize(const rapidjson::Value& obj) {
  return false;
}

bool ReadUserStatsObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("players");

  writer->StartArray();

  const xe::be<uint64_t>* xuids_ptr =
      kernel_state()->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          read_stats_.xuids_ptr);

  const std::vector<xe::be<uint64_t>> xuids(
      xuids_ptr, xuids_ptr + read_stats_.xuids_count);

  for (const uint64_t& xuid : xuids) {
    writer->String(fmt::format("{:016X}", xuid).c_str());
  }

  writer->EndArray();

  const uint32_t title_id = read_stats_.titleId ? read_stats_.titleId.get()
                                                : kernel_state()->title_id();

  writer->String("titleId");
  writer->String(fmt::format("{:08x}", title_id).c_str());

  writer->String("queries");

  const X_USER_STATS_SPEC* specs_ptr =
      kernel_state()->memory()->TranslateVirtual<X_USER_STATS_SPEC*>(
          read_stats_.specs_ptr);

  const std::vector<X_USER_STATS_SPEC> specs(
      specs_ptr, specs_ptr + read_stats_.specs_count);

  writer->StartArray();
  for (const auto& spec : specs) {
    writer->StartObject();

    writer->String("id");
    writer->Uint(spec.view_id);

    writer->String("statisticIds");
    writer->StartArray();
    const uint32_t num_column_ids =
        std::min<uint32_t>(spec.num_column_ids, kXUserMaxStatsAttributes);

    for (uint32_t column_index = 0; column_index < num_column_ids;
         column_index++) {
      writer->Uint(spec.column_ids[column_index]);
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
