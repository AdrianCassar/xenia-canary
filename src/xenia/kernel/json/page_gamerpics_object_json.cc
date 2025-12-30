/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/page_gamerpics_object_json.h"
#include "xenia/base/string_util.h"

namespace xe {
namespace kernel {
PageGamerpicsObjectJSON::PageGamerpicsObjectJSON() {}

PageGamerpicsObjectJSON::~PageGamerpicsObjectJSON() {}

bool PageGamerpicsObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsObject()) {
    return false;
  }

  if (obj.HasMember("next_page") && !obj["next_page"].IsNull()) {
    next_page_ = obj["next_page"].GetInt();
  }

  if (obj.HasMember("page") && !obj["page"].IsNull()) {
    page_ = obj["page"].GetInt();
  }

  if (obj.HasMember("pages") && !obj["pages"].IsNull()) {
    pages_ = obj["pages"].GetInt();
  }

  if (obj.HasMember("prev_page") && !obj["prev_page"].IsNull()) {
    prev_page_ = obj["prev_page"].GetInt();
  }

  if (obj.HasMember("total") && !obj["total"].IsNull()) {
    total_titles_ = obj["total"].GetInt();
  }

  if (obj.HasMember("titles") && obj["titles"].IsArray() &&
      !obj["titles"].IsNull()) {
    const auto titles = obj["titles"].GetArray();

    for (const auto& title : titles) {
      TitleGamerpicsObjectJSON title_object = {};
      title_object.Deserialize(title);
      titles_.push_back(title_object.GetTitle());
    }
  }

  return true;
}

bool PageGamerpicsObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return false;
}
}  // namespace kernel
}  // namespace xe
