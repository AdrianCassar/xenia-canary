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

namespace xe {
namespace kernel {
LeaderboardObjectJSON::LeaderboardObjectJSON() : read_results_({}) {}

LeaderboardObjectJSON::LeaderboardObjectJSON(
    view_properties_unordered_map stats)
    : stats_(stats), read_results_({}) {}

LeaderboardObjectJSON::~LeaderboardObjectJSON() {}

bool LeaderboardObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsArray()) {
    return false;
  }

  if (!obj.GetArray().Begin()->IsObject()) {
    return false;
  }

  const auto leaderboards = obj.GetArray();
  const uint32_t views_count = leaderboards.Size();

  const uint32_t views_address = kernel_state()->memory()->SystemHeapAlloc(
      sizeof(X_USER_STATS_VIEW) * views_count);

  X_USER_STATS_VIEW* views_ptr =
      kernel_state()->memory()->TranslateVirtual<X_USER_STATS_VIEW*>(
          views_address);

  read_results_.num_views = views_count;
  read_results_.views_ptr = views_address;

  for (uint32_t view_index = 0; const auto& view : leaderboards) {
    X_USER_STATS_VIEW* view_ptr = views_ptr + view_index;

    const uint32_t view_id = view["id"].GetUint();
    const auto players = view["players"].GetArray();
    const uint32_t rows_count = players.Size();

    const uint32_t rows_address = kernel_state()->memory()->SystemHeapAlloc(
        sizeof(X_USER_STATS_ROW) * rows_count);

    X_USER_STATS_ROW* rows_ptr =
        kernel_state()->memory()->TranslateVirtual<X_USER_STATS_ROW*>(
            rows_address);

    view_ptr->view_id = view_id;
    view_ptr->rows_ptr = rows_address;
    view_ptr->total_view_rows = rows_count;
    view_ptr->num_rows = rows_count;

    for (uint32_t row_index = 0; const auto& player : players) {
      X_USER_STATS_ROW* row_ptr = rows_ptr + row_index;

      const uint64_t xuid = xe::string_util::from_string<uint64_t>(
          player["xuid"].GetString(), true);
      const std::string gamertag = player["gamertag"].GetString();
      const auto columns = player["stats"].GetArray();
      const uint32_t columns_count = columns.Size();

      assert_true(IsValidXUID(xuid));

      row_ptr->xuid = xuid;
      std::memcpy(row_ptr->gamertag, gamertag.c_str(),
                  static_cast<uint32_t>(gamertag.size()));
      row_ptr->num_columns = columns_count;

      // 0 if not in leaderboard
      row_ptr->rank = static_cast<uint32_t>(row_index) + 1;
      row_ptr->i64Rating = 0;

      uint32_t columns_address = columns_address =
          kernel_state()->memory()->SystemHeapAlloc(
              sizeof(X_USER_STATS_COLUMN) * columns_count);

      X_USER_STATS_COLUMN* columns_ptr = columns_ptr =
          kernel_state()->memory()->TranslateVirtual<X_USER_STATS_COLUMN*>(
              columns_address);

      row_ptr->columns_ptr = columns_address;

      for (uint32_t column_index = 0; const auto& column_data : columns) {
        X_USER_STATS_COLUMN* column_ptr = columns_ptr + column_index;

        // Ordinal
        const uint32_t column_id = column_data["id"].GetUint();
        const xam::X_USER_DATA_TYPE type =
            static_cast<xam::X_USER_DATA_TYPE>(column_data["type"].GetUint());

        if (columns_ptr) {
          column_ptr->column_id = column_id;
          column_ptr->value.type = type;

          // WSTRING and BINARY are unsupported
          switch (type) {
            case xam::X_USER_DATA_TYPE::CONTEXT:
              column_ptr->value.data.u32 = column_data["value"].GetUint();
              break;
            case xam::X_USER_DATA_TYPE::INT32:
              column_ptr->value.data.u32 = column_data["value"].GetInt();
              break;
            case xam::X_USER_DATA_TYPE::INT64:
              column_ptr->value.data.s64 = column_data["value"].GetInt64();
              break;
            case xam::X_USER_DATA_TYPE::DOUBLE:
              column_ptr->value.data.f64 = column_data["value"].GetDouble();
              break;
            case xam::X_USER_DATA_TYPE::FLOAT:
              column_ptr->value.data.f32 = column_data["value"].GetFloat();
              break;
            case xam::X_USER_DATA_TYPE::DATETIME:
              column_ptr->value.data.filetime = column_data["value"].GetInt64();
              break;
            case xam::X_USER_DATA_TYPE::UNSET:
              // Ignore don't read missing/placeholder stat
              break;
            default:
              XELOGW("Unimplemented stat type for read: {}",
                     static_cast<uint32_t>(type));
              assert_always();
          }
        }

        column_index++;
      }

      row_index++;
    }

    view_index++;
  }

  return true;
}

bool LeaderboardObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  if (stats_.empty()) {
    return false;
  }

  // TODO: Support multiple local players in a session

  for (uint32_t view_index = 0; const auto& [xuid, views] : stats_) {
    const std::string xuid_str = fmt::format("{:016X}", xuid);

    if (view_index >= 1) {
      // Sending multiple players stats is unsupported
      assert_always();
      continue;
    }

    writer->StartObject();

    writer->String("leaderboards");
    writer->StartObject();

    for (const auto& [view_id, properties] : views) {
      const std::string leaderboard_id = std::to_string(view_id);

      assert_false(view_id == kTrueSkillViewId);

      writer->String(leaderboard_id);
      writer->StartObject();

      writer->String("stats");
      writer->StartObject();

      for (const auto& [property_id, property] : properties) {
        const std::string property_id_str = fmt::format("{:08X}", property_id);

        // 41560901 doesn't set property type
        xam::X_USER_DATA_TYPE property_type =
            xam::UserData::get_type(property_id);

        // Write each stat ID
        writer->String(property_id_str);
        writer->StartObject();

        writer->String("type");
        writer->Int(static_cast<uint32_t>(property_type));

        switch (property_type) {
          case xam::X_USER_DATA_TYPE::CONTEXT: {
            writer->String("value");
            writer->Uint(property.get_data()->data.u32);
          } break;
          case xam::X_USER_DATA_TYPE::INT32: {
            writer->String("value");
            writer->Int(property.get_data()->data.s32);
          } break;
          case xam::X_USER_DATA_TYPE::DATETIME:
          case xam::X_USER_DATA_TYPE::INT64: {
            writer->String("value");
            writer->Uint64(property.get_data()->data.s64);
          } break;
          case xam::X_USER_DATA_TYPE::FLOAT:
          case xam::X_USER_DATA_TYPE::DOUBLE: {
            writer->String("value");
            writer->Double(property.get_data()->data.f64);
          } break;
          case xam::X_USER_DATA_TYPE::UNSET:
          case xam::X_USER_DATA_TYPE::WSTRING:
          case xam::X_USER_DATA_TYPE::BINARY:
          default:
            XELOGW("Unsupported stat type for write {}",
                   static_cast<uint32_t>(property.get_data()->type));
            break;
        }

        writer->EndObject();
      }

      writer->EndObject();

      writer->EndObject();
    }

    writer->EndObject();

    writer->String("xuid");
    writer->String(xuid_str);

    writer->EndObject();

    view_index++;
  }

  return true;
}
}  // namespace kernel
}  // namespace xe
