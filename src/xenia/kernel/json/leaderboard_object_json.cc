/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/leaderboard_object_json.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/string.h"
#include "xenia/emulator.h"
#include "xenia/kernel/util/game_info_database.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {

namespace {

// Bump allocator over the guest provided results buffer.
class ResultsAllocator {
 public:
  ResultsAllocator(uint32_t address, uint32_t size)
      : address_(address),
        size_(size),
        base_(kernel_memory()->TranslateVirtual<uint8_t*>(address)) {}

  // Returns the guest address of the allocation or 0 if it does not fit.
  uint32_t Allocate(size_t size, size_t alignment = 8) {
    const size_t offset = xe::round_up(offset_, alignment, false);

    if (!size || offset + size > size_) {
      return 0;
    }

    offset_ = offset + size;

    return address_ + static_cast<uint32_t>(offset);
  }

  // Number of elements of size that fit in the remaining space.
  size_t Available(size_t size, size_t alignment = 8) const {
    const size_t offset = xe::round_up(offset_, alignment, false);

    if (offset >= size_) {
      return 0;
    }

    return (size_ - offset) / size;
  }

  template <typename T>
  T* Translate(uint32_t address) const {
    return reinterpret_cast<T*>(base_ + (address - address_));
  }

 private:
  uint32_t address_;
  size_t size_;
  size_t offset_ = 0;
  uint8_t* base_;
};

// System columns are returned through X_USER_STATS_ROW rank, i64Rating and
// gamertag instead of the row's columns.
// 5841085C explicitly requests AttachmentSizeAttributeId as a column.
bool IsSystemColumn(uint16_t column_id) {
  switch (column_id) {
    case xam::RankAttributeId:
    case xam::RatingAttributeId:
    case xam::GamertagAttributeId:
      return true;
    default:
      return false;
  }
}

int64_t GetRatingValue(const xam::Property& property) {
  const xam::X_USER_DATA* data = property.get_data();

  switch (data->type) {
    case xam::X_USER_DATA_TYPE::CONTEXT:
      return data->data.u32.get();
    case xam::X_USER_DATA_TYPE::INT32:
      return data->data.s32.get();
    case xam::X_USER_DATA_TYPE::INT64:
    case xam::X_USER_DATA_TYPE::DATETIME:
      // 58410A3B provides data in a custom format for i64Rating.
      return data->data.s64.get();
    case xam::X_USER_DATA_TYPE::DOUBLE:
      return static_cast<int64_t>(data->data.f64.get());
    case xam::X_USER_DATA_TYPE::FLOAT:
      return static_cast<int64_t>(data->data.f32.get());
    default:
      return 0;
  }
}

std::string GetGamertag(const xam::Property& property) {
  if (property.get_type() != xam::X_USER_DATA_TYPE::WSTRING ||
      property.get_extended_data().empty()) {
    return "";
  }

  const char16_t* data =
      reinterpret_cast<const char16_t*>(property.get_extended_data().data());

  std::u16string gamertag = xe::string_util::read_u16string_and_swap(data);

  return xe::to_utf8(gamertag);
}

}  // namespace

const LeaderboardObjectJSON::Column* LeaderboardObjectJSON::Row::FindColumn(
    uint16_t attribute_id) const {
  for (const auto& column : columns) {
    if (column.attribute_id == attribute_id) {
      return &column;
    }
  }

  return nullptr;
}

LeaderboardObjectJSON::LeaderboardObjectJSON() : stats_({}) {}

LeaderboardObjectJSON::LeaderboardObjectJSON(
    view_properties_unordered_map stats)
    : stats_(stats) {}

LeaderboardObjectJSON::~LeaderboardObjectJSON() {}

bool LeaderboardObjectJSON::Deserialize(const rapidjson::Value& obj) {
  views_.clear();

  if (!obj.IsObject()) {
    return false;
  }

  if (!obj.HasMember("views") || !obj["views"].IsArray()) {
    return false;
  }

  for (const auto& view_obj : obj["views"].GetArray()) {
    if (!view_obj.IsObject() || !view_obj.HasMember("id") ||
        !view_obj["id"].IsUint()) {
      continue;
    }

    View view = {};
    view.view_id = view_obj["id"].GetUint();

    if (view_obj.HasMember("total_rows") && view_obj["total_rows"].IsUint()) {
      view.total_rows = view_obj["total_rows"].GetUint();
    }

    if (view_obj.HasMember("rows") && view_obj["rows"].IsArray()) {
      for (const auto& row_obj : view_obj["rows"].GetArray()) {
        if (!row_obj.IsObject() || !row_obj.HasMember("xuid") ||
            !row_obj["xuid"].IsString()) {
          continue;
        }

        Row row = {};
        row.xuid = xe::string_util::from_string<uint64_t>(
            row_obj["xuid"].GetString(), true);

        if (row_obj.HasMember("columns") && row_obj["columns"].IsArray()) {
          for (const auto& column_obj : row_obj["columns"].GetArray()) {
            if (!column_obj.IsObject() ||
                !column_obj.HasMember("attribute_id") ||
                !column_obj["attribute_id"].IsUint() ||
                !column_obj.HasMember("property") ||
                !column_obj["property"].IsString()) {
              continue;
            }

            const std::optional<xam::Property> property =
                xam::Property::DeserializeBase64(
                    column_obj["property"].GetString());

            if (!property.has_value()) {
              continue;
            }

            // UNSET is returned for requested columns without stats.
            if (!property->is_valid_type() &&
                property->get_type() != xam::X_USER_DATA_TYPE::UNSET) {
              continue;
            }

            row.columns.push_back(
                {static_cast<uint16_t>(column_obj["attribute_id"].GetUint()),
                 property.value()});
          }
        }

        view.rows.push_back(row);
      }
    }

    views_.push_back(view);
  }

  return true;
}

bool LeaderboardObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  if (stats_.empty()) {
    return false;
  }

  const auto game_info_db = kernel_state()->emulator()->game_info_database();

  writer->StartObject();

  writer->String("title_id");
  writer->String(fmt::format("{:08X}", kernel_state()->title_id()));

  writer->String("views");
  writer->StartArray();

  for (const auto& [view_id, xuids] : stats_) {
    // TODO(Adrian): Write TrueSkill defined columns
    if (IsTrueSkillViewID(view_id)) {
      XELOGI("Flush Stats: Skipping TrueSkill View ID: {:08X}", view_id);
      continue;
    }

    if (!game_info_db->GetStatsView(view_id).has_value()) {
      XELOGW("Flush Stats: Unknown View ID: {:08X}", view_id);
      continue;
    }

    writer->StartObject();

    writer->String("id");
    writer->Uint(view_id);

    writer->String("reset_type");
    writer->String(game_info_db->GetStatsViewResetType(view_id));

    writer->String("rows");
    writer->StartArray();

    for (const auto& [xuid, properties] : xuids) {
      writer->StartObject();

      writer->String("xuid");
      writer->String(fmt::format("{:016X}", xuid));

      // System columns such as rank and gamertag are derived by the backend.
      writer->String("columns");
      writer->StartArray();

      for (const auto& [property_id, property] : properties) {
        const auto stats_view_column =
            game_info_db->GetStatsViewColumn(view_id, property_id);

        if (!stats_view_column.has_value()) {
          continue;
        }

        const auto property_base64 = property.SerializeToBase64();

        if (!property_base64.has_value()) {
          continue;
        }

        writer->StartObject();

        writer->String("attribute_id");
        writer->Uint(stats_view_column->attribute_id);

        writer->String("aggregation_type");
        writer->Uint(stats_view_column->aggregation_type);

        writer->String("property");
        writer->String(property_base64.value());

        writer->EndObject();
      }

      writer->EndArray();

      writer->EndObject();
    }

    writer->EndArray();

    writer->EndObject();
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}

bool LeaderboardObjectJSON::WriteResults(uint32_t results_address,
                                         uint32_t results_size) const {
  if (!results_address || results_size < sizeof(X_USER_STATS_READ_RESULTS)) {
    return false;
  }

  // Memory Layout:
  // X_USER_STATS_READ_RESULTS
  // X_USER_STATS_VIEW[]
  // For each view:
  //   X_USER_STATS_ROW[]
  //   X_USER_STATS_COLUMN[] for each row

  ResultsAllocator allocator(results_address, results_size);

  std::memset(kernel_memory()->TranslateVirtual(results_address), 0,
              results_size);

  const uint32_t results_ptr =
      allocator.Allocate(sizeof(X_USER_STATS_READ_RESULTS));
  X_USER_STATS_READ_RESULTS* results =
      allocator.Translate<X_USER_STATS_READ_RESULTS>(results_ptr);

  if (views_.empty()) {
    return true;
  }

  const uint32_t views_ptr =
      allocator.Allocate(sizeof(X_USER_STATS_VIEW) * views_.size());

  if (!views_ptr) {
    XELOGW("{}: Results buffer too small for views", __func__);
    return false;
  }

  results->num_views = static_cast<uint32_t>(views_.size());
  results->views_ptr = views_ptr;

  X_USER_STATS_VIEW* views = allocator.Translate<X_USER_STATS_VIEW>(views_ptr);

  bool truncated = false;

  for (size_t view_index = 0; view_index < views_.size(); view_index++) {
    const View& view_data = views_[view_index];
    X_USER_STATS_VIEW& view = views[view_index];

    if (IsTrueSkillViewID(view_data.view_id)) {
      XELOGI("TrueSkill View ID: {:08X}", view_data.view_id);
    }

    // 4B5607E8 expects view id otherwise crashes.
    view.view_id = view_data.view_id;
    view.total_view_rows = view_data.total_rows;

    const size_t rows_count = std::min(
        view_data.rows.size(), allocator.Available(sizeof(X_USER_STATS_ROW)));

    if (rows_count != view_data.rows.size()) {
      truncated = true;
    }

    view.num_rows = static_cast<uint32_t>(rows_count);

    if (!rows_count) {
      continue;
    }

    const uint32_t rows_ptr =
        allocator.Allocate(sizeof(X_USER_STATS_ROW) * rows_count);
    X_USER_STATS_ROW* rows = allocator.Translate<X_USER_STATS_ROW>(rows_ptr);

    view.rows_ptr = rows_ptr;

    // Columns returned by the backend for each row.
    std::vector<std::vector<const Column*>> rows_columns(rows_count);

    for (size_t row_index = 0; row_index < rows_count; row_index++) {
      for (const auto& column_data : view_data.rows[row_index].columns) {
        if (IsSystemColumn(column_data.attribute_id)) {
          continue;
        }

        // Columns do not support additional data such as WSTRING or BINARY.
        if (column_data.property.requires_additional_data()) {
          continue;
        }

        rows_columns[row_index].push_back(&column_data);
      }
    }

    std::vector<uint32_t> columns_ptrs(rows_count, 0);

    for (size_t row_index = 0; row_index < rows_count; row_index++) {
      if (rows_columns[row_index].empty()) {
        continue;
      }

      columns_ptrs[row_index] = allocator.Allocate(
          sizeof(X_USER_STATS_COLUMN) * rows_columns[row_index].size());

      if (!columns_ptrs[row_index]) {
        truncated = true;
      }
    }

    for (size_t row_index = 0; row_index < rows_count; row_index++) {
      X_USER_STATS_ROW& row = rows[row_index];
      const Row& row_data = view_data.rows[row_index];

      row.xuid = row_data.xuid;

      // If rank or i64Rating is 0 then gamer is not in leaderboard.
      if (const Column* rank = row_data.FindColumn(xam::RankAttributeId)) {
        row.rank = rank->property.get_data()->data.u32;
      }

      if (const Column* rating = row_data.FindColumn(xam::RatingAttributeId)) {
        row.i64Rating = static_cast<uint64_t>(GetRatingValue(rating->property));
      }

      // 58410968 and 555307DB use gamertags from XFriendsCreateEnumerator.
      if (const Column* gamertag =
              row_data.FindColumn(xam::GamertagAttributeId)) {
        xe::string_util::copy_truncating(row.gamertag,
                                         GetGamertag(gamertag->property),
                                         xe::countof(row.gamertag));
      }

      // 545107FC, 454108D4, 58410A57, 584108FF, 41560834 do not use columns.
      // 58410A3B expects no columns to read i64Rating.
      // Rows without stats have no columns.
      if (!columns_ptrs[row_index]) {
        continue;
      }

      const std::vector<const Column*>& row_columns = rows_columns[row_index];

      row.num_columns = static_cast<uint32_t>(row_columns.size());
      row.columns_ptr = columns_ptrs[row_index];

      X_USER_STATS_COLUMN* columns =
          allocator.Translate<X_USER_STATS_COLUMN>(columns_ptrs[row_index]);

      for (size_t column_index = 0; column_index < row_columns.size();
           column_index++) {
        X_USER_STATS_COLUMN& column = columns[column_index];
        const Column* column_data = row_columns[column_index];

        column.column_id = column_data->attribute_id;
        column.value = *column_data->property.get_data();
      }
    }
  }

  if (truncated) {
    XELOGW("{}: Results buffer of {} bytes too small, results truncated",
           __func__, results_size);
  }

  return true;
}
}  // namespace kernel
}  // namespace xe
