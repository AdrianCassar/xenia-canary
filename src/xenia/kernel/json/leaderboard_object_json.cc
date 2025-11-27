/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/leaderboard_object_json.h"
#include "xenia/emulator.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
LeaderboardObjectJSON::LeaderboardObjectJSON()
    : stats_({}), read_results_({}) {}

LeaderboardObjectJSON::LeaderboardObjectJSON(
    view_properties_unordered_map stats)
    : stats_(stats), read_results_({}) {}

LeaderboardObjectJSON::~LeaderboardObjectJSON() {}

bool LeaderboardObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsArray()) {
    return false;
  }

  if (obj.GetArray().Empty()) {
    return false;
  }

  if (!obj.GetArray().Begin()->IsObject()) {
    return false;
  }

  const auto leaderboards = obj.GetArray();
  const uint32_t views_count = leaderboards.Size();

  // TODO(Adrian)
  // Instead of allocating more memory use provided buffer from XGI call.
  const uint32_t views_address =
      kernel_memory()->SystemHeapAlloc(sizeof(X_USER_STATS_VIEW) * views_count);

  X_USER_STATS_VIEW* views_ptr =
      kernel_memory()->TranslateVirtual<X_USER_STATS_VIEW*>(views_address);

  read_results_.num_views = views_count;
  read_results_.views_ptr = views_address;

  for (uint32_t view_index = 0; const auto& view : leaderboards) {
    X_USER_STATS_VIEW& view_ptr = views_ptr[view_index];

    const uint32_t view_id = view["id"].GetUint();
    const auto players = view["players"].GetArray();
    const uint32_t rows_count = players.Size();

    const auto spa_stats_view =
        kernel_state()->emulator()->game_info_database()->GetStatsView(view_id);

    const uint32_t rows_address =
        kernel_memory()->SystemHeapAlloc(sizeof(X_USER_STATS_ROW) * rows_count);

    X_USER_STATS_ROW* rows_ptr =
        kernel_memory()->TranslateVirtual<X_USER_STATS_ROW*>(rows_address);

    view_ptr.view_id = view_id;
    view_ptr.rows_ptr = rows_address;
    view_ptr.total_view_rows = rows_count;
    view_ptr.num_rows = rows_count;

    for (uint32_t row_index = 0; const auto& player : players) {
      X_USER_STATS_ROW& row_ptr = rows_ptr[row_index];

      const uint64_t xuid = xe::string_util::from_string<uint64_t>(
          player["xuid"].GetString(), true);
      const std::string gamertag = player["gamertag"].GetString();
      const auto columns = player["stats"].GetArray();
      const uint32_t columns_count = columns.Size();

      assert_true(IsValidXUID(xuid));

      row_ptr.xuid = xuid;

      // 58410968 and 555307DB use gamertags from XFriendsCreateEnumerator.
      // xam::GamertagAttributeId
      xe::string_util::copy_truncating(row_ptr.gamertag, gamertag.c_str(),
                                       sizeof(row_ptr.gamertag));

      row_ptr.num_columns = columns_count;

      // If rank or i64Rating is 0 then gamer is not in leaderboard.

      // xam::RankAttributeId
      row_ptr.rank = row_index + 1;

      // 58410A3B provides data in a custom format for i64Rating therefore,
      // placeholder data is interpreted as corrupted.

      // xam::RatingAttributeId
      row_ptr.i64Rating = 1;

      // In such case request system attributes Rank, i64Rating and Gamertag.
      // 545107FC, 454108D4, 58410A57, 584108FF, 41560834 do not use columns.
      // 58410A3B expects no columns to read i64Rating.
      if (!columns_count) {
        row_index++;
        continue;
      }

      const uint32_t columns_size = sizeof(X_USER_STATS_COLUMN) * columns_count;

      const uint32_t columns_address =
          kernel_memory()->SystemHeapAlloc(columns_size);

      X_USER_STATS_COLUMN* columns_ptr =
          kernel_memory()->TranslateVirtual<X_USER_STATS_COLUMN*>(
              columns_address);

      row_ptr.columns_ptr = columns_address;

      for (uint32_t column_index = 0; const auto& column_data : columns) {
        X_USER_STATS_COLUMN& column_ptr = columns_ptr[column_index];

        // Attribute ID
        const uint32_t column_id = column_data["id"].GetUint();
        xam::X_USER_DATA_TYPE type =
            static_cast<xam::X_USER_DATA_TYPE>(column_data["type"].GetUint());

        if (IsTrueSkillViewID(view_id)) {
          type = GetTrueSkillColumnType(column_id);
        }

        column_ptr.column_id = column_id;
        column_ptr.value.type = type;

        // Placeholder data may be interpreted as corrupted.
        column_ptr.value.data = {};

        // WSTRING and BINARY are unsupported
        switch (type) {
          case xam::X_USER_DATA_TYPE::CONTEXT:
            column_ptr.value.data.u32 = column_data["value"].GetUint();
            break;
          case xam::X_USER_DATA_TYPE::INT32:
            column_ptr.value.data.u32 = column_data["value"].GetInt();
            break;
          case xam::X_USER_DATA_TYPE::INT64:
            column_ptr.value.data.s64 = column_data["value"].GetInt64();
            break;
          case xam::X_USER_DATA_TYPE::DOUBLE:
            column_ptr.value.data.f64 = column_data["value"].GetDouble();
            break;
          case xam::X_USER_DATA_TYPE::FLOAT:
            column_ptr.value.data.f32 = column_data["value"].GetFloat();
            break;
          case xam::X_USER_DATA_TYPE::DATETIME:
            column_ptr.value.data.filetime = column_data["value"].GetInt64();
            break;
          case xam::X_USER_DATA_TYPE::UNSET: {
            // Ignore don't read missing/placeholder stat
            // Currently we do not store this information on the backend so
            // instead we can determine the property type locally ourself.
            // 5454082B expects correct type otherwise stats appear corrupted.
            if (spa_stats_view.has_value()) {
              for (const auto& column :
                   spa_stats_view.value().shared_view.column_entries) {
                if (column.attribute_id == column_id) {
                  column_ptr.value.type =
                      xam::UserData::get_type(column.property_id);
                }
              }
            } else {
              column_ptr.value.type = xam::X_USER_DATA_TYPE::INT32;
              assert_always();
            }
          } break;
          default: {
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

  // TODO(Adrian):
  // Support multiple local players in a session
  // Write default system defined columns
  // Write TrueSkill defined columns
  //
  // If columns are not written then we cannot read them.

  for (uint32_t view_index = 0; const auto& [xuid, views] : stats_) {
    const std::string xuid_str = fmt::format("{:016X}", xuid);

    if (view_index >= 1) {
      // Sending multiple players stats is unsupported
      XELOGI("Flushing multiple players stats is currently unsupported!");
      assert_always();
      continue;
    }

    writer->StartObject();

    writer->String("leaderboards");
    writer->StartObject();

    for (const auto& [view_id, properties] : views) {
      const std::string leaderboard_id = std::to_string(view_id);

      if (IsTrueSkillViewID(view_id)) {
        XELOGI("Flush Stats: TrueSkill View ID: {:08X}", view_id);
      }

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
