/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/leaderboard_object_json.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_data.h"

namespace xe {
namespace kernel {
LeaderboardObjectJSON::LeaderboardObjectJSON(
    XGI_STATS_WRITE stats,
    std::vector<XSESSION_VIEW_PROPERTIES> view_properties) {
  stats_ = stats;
  view_properties_ = view_properties;
}

LeaderboardObjectJSON::~LeaderboardObjectJSON() {}

bool LeaderboardObjectJSON::Deserialize(const rapidjson::Value& obj) {
  return false;
}

bool LeaderboardObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  const std::string xuid = fmt::format("{:016X}", stats_.xuid.get());

  writer->StartObject();

  writer->Key("leaderboards");
  writer->StartObject();

  for (auto& user_property : view_properties_) {
    const std::string leaderboard_id = std::to_string(user_property.view_id);

    writer->Key(leaderboard_id);
    writer->StartObject();

    writer->Key("stats");
    writer->StartObject();

    for (uint32_t i = 0; i < user_property.properties_count; i++) {
      const xam::XUSER_PROPERTY* statistics_ptr =
          kernel_state()->memory()->TranslateVirtual<xam::XUSER_PROPERTY*>(
              user_property.properties_ptr);

      const xam::XUSER_PROPERTY& stat = statistics_ptr[i];

      const std::string property_id =
          fmt::format("{:08X}", stat.property_id.get());

      // Write each stat ID
      writer->Key(property_id);
      writer->StartObject();

      writer->Key("type");
      writer->Int(static_cast<uint32_t>(stat.data.type));

      switch (stat.data.type) {
        case xam::X_USER_DATA_TYPE::CONTEXT: {
          writer->String("value");
          writer->Uint(stat.data.data.u32);
        } break;
        case xam::X_USER_DATA_TYPE::INT32: {
          writer->String("value");
          writer->Int(stat.data.data.s32);
        } break;
        case xam::X_USER_DATA_TYPE::INT64: {
          writer->String("value");
          writer->Uint64(stat.data.data.s64);
        } break;
        case xam::X_USER_DATA_TYPE::DOUBLE: {
          writer->String("value");
          writer->Double(stat.data.data.f64);
        } break;
        case xam::X_USER_DATA_TYPE::FLOAT: {
          XELOGW("Unimplemented statistic type: FLOAT");
        } break;
        case xam::X_USER_DATA_TYPE::DATETIME: {
          XELOGW("Unimplemented statistic type: DATETIME");
        } break;
        case xam::X_USER_DATA_TYPE::UNSET: {
          // Ignore
        } break;
        case xam::X_USER_DATA_TYPE::WSTRING:
        case xam::X_USER_DATA_TYPE::BINARY:
        default:
          XELOGW("Unsupported statistic type for write {}",
                 static_cast<uint32_t>(stat.data.type));
          break;
      }

      writer->EndObject();
    }

    writer->EndObject();

    writer->EndObject();
  }

  writer->EndObject();

  writer->Key("xuid");
  writer->String(xuid);

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe