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
LeaderboardObjectJSON::LeaderboardObjectJSON()
    : stats_({}), view_properties_({}), read_results_({}) {}

LeaderboardObjectJSON::LeaderboardObjectJSON(
    XGI_STATS_WRITE stats,
    std::vector<XSESSION_VIEW_PROPERTIES> view_properties)
    : stats_({}), view_properties_({}), read_results_({}) {
  stats_ = stats;
  view_properties_ = view_properties;
}

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
  const std::string xuid = fmt::format("{:016X}", stats_.xuid.get());

  writer->StartObject();

  writer->Key("leaderboards");
  writer->StartObject();

  for (auto& user_property : view_properties_) {
    const std::string leaderboard_id = std::to_string(user_property.view_id);

    assert_false(user_property.view_id == kTrueSkillViewId);

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

      // 41560901 doesn't set property type
      xam::X_USER_DATA_TYPE property_type =
          xam::UserData::get_type(stat.property_id);

      // Write each stat ID
      writer->Key(property_id);
      writer->StartObject();

      writer->Key("type");
      writer->Int(static_cast<uint32_t>(property_type));

      switch (property_type) {
        case xam::X_USER_DATA_TYPE::CONTEXT: {
          writer->String("value");
          writer->Uint(stat.data.data.u32);
        } break;
        case xam::X_USER_DATA_TYPE::INT32: {
          writer->String("value");
          writer->Int(stat.data.data.s32);
        } break;
        case xam::X_USER_DATA_TYPE::DATETIME:
        case xam::X_USER_DATA_TYPE::INT64: {
          writer->String("value");
          writer->Uint64(stat.data.data.s64);
        } break;
        case xam::X_USER_DATA_TYPE::FLOAT:
        case xam::X_USER_DATA_TYPE::DOUBLE: {
          writer->String("value");
          writer->Double(stat.data.data.f64);
        } break;
        case xam::X_USER_DATA_TYPE::UNSET:
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
