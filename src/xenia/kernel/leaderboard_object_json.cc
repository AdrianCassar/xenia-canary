/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/leaderboard_object_json.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
LeaderboardObjectJSON::LeaderboardObjectJSON() : stats_{}, view_properties_{} {}

LeaderboardObjectJSON::~LeaderboardObjectJSON() {}

bool LeaderboardObjectJSON::Deserialize(const rapidjson::Value& obj) {
  return true;
}

bool LeaderboardObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  const std::string xuid = fmt::format("{:016X}", stats_.xuid);

  writer->StartObject();

  writer->Key("leaderboards");
  writer->StartObject();

  for (auto& user_property : view_properties_) {
    const std::string leaderboard_id =
        std::to_string(user_property.leaderboard_id);

    writer->Key(leaderboard_id);
    writer->StartObject();

    writer->Key("stats");
    writer->StartObject();

    for (uint32_t i = 0; i < user_property.properties_count; i++) {
      const XUSER_PROPERTY* statistics_ptr =
          kernel_state()->memory()->TranslateVirtual<XUSER_PROPERTY*>(
              user_property.properties_guest_address);

      const XUSER_PROPERTY& stat = statistics_ptr[i];

      const std::string property_id = fmt::format("{:08X}", stat.property_id);

      // Write each stat ID
      writer->Key(property_id);
      writer->StartObject();

      writer->Key("type");
      writer->Int(stat.value.type);

      switch (stat.value.type) {
        case 1: {
          writer->String("value");
          writer->Uint(stat.value.dword_data);
        } break;
        case 2: {
          writer->String("value");
          writer->Uint64(stat.value.qword_data);
        } break;
        case 3: {
          writer->String("value");
          writer->Double(stat.value.double_data);
        } break;
        default:
          XELOGW("Unimplemented statistic type for write", stat.value.type);
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