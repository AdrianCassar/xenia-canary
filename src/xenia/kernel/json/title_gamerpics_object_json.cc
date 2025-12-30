/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/title_gamerpics_object_json.h"
#include "xenia/base/string_util.h"

namespace xe {
namespace kernel {
TitleGamerpicsObjectJSON::TitleGamerpicsObjectJSON() {}

TitleGamerpicsObjectJSON::~TitleGamerpicsObjectJSON() {}

bool TitleGamerpicsObjectJSON::Deserialize(const rapidjson::Value& obj) {
  GameTitle title = {};

  if (obj.HasMember("_id") && !obj["_id"].IsNull()) {
    title.id = string_util::from_string<uint32_t>(obj["_id"].GetString(), true);
  }

  if (obj.HasMember("gamerpics") && obj["gamerpics"].IsArray() &&
      !obj["gamerpics"].IsNull()) {
    const auto gamerpics = obj["gamerpics"].GetArray();

    for (const auto& gamerpic : gamerpics) {
      title.gamerpics.push_back(DeserializeGamerpic(gamerpic));
    }
  }

  if (obj.HasMember("image") && !obj["image"].IsNull()) {
    title.image = obj["image"].GetString();
  }

  if (obj.HasMember("name") && !obj["name"].IsNull()) {
    title.name = obj["name"].GetString();
  }

  if (obj.HasMember("slug") && !obj["slug"].IsNull()) {
    title.slug = obj["slug"].GetString();
  }

  if (obj.HasMember("type") && !obj["type"].IsNull()) {
    title.type = obj["type"].GetString();
  }

  if (obj.HasMember("url") && !obj["url"].IsNull()) {
    title.url = obj["url"].GetString();
  }

  title_ = title;

  return true;
}

Gamerpic TitleGamerpicsObjectJSON::DeserializeGamerpic(
    const rapidjson::Value& gamerpicObj) {
  Gamerpic gamepic = {};

  if (gamerpicObj.HasMember("cdn") && !gamerpicObj["cdn"].IsNull()) {
    gamepic.cdn = gamerpicObj["cdn"].GetString();
  }

  if (gamerpicObj.HasMember("id") && !gamerpicObj["id"].IsNull()) {
    gamepic.id = gamerpicObj["id"].GetString();
  }

  if (gamerpicObj.HasMember("likes") && !gamerpicObj["likes"].IsNull()) {
    gamepic.likes = gamerpicObj["likes"].GetUint();
  }

  if (gamerpicObj.HasMember("name") && !gamerpicObj["name"].IsNull()) {
    gamepic.name = gamerpicObj["name"].GetString();
  }

  if (gamerpicObj.HasMember("url") && !gamerpicObj["url"].IsNull()) {
    gamepic.url = gamerpicObj["url"].GetString();

    std::string big_tile_id_str =
        std::filesystem::path(gamepic.name).stem().string();

    gamepic.big_tile_id =
        string_util::from_string<uint32_t>(big_tile_id_str, true);
  }

  if (gamerpicObj.HasMember("url_small") &&
      !gamerpicObj["url_small"].IsNull()) {
    gamepic.url_small = gamerpicObj["url_small"].GetString();

    std::string small_tile_id_str =
        std::filesystem::path(gamepic.url_small).stem().string();

    gamepic.small_tile_id =
        string_util::from_string<uint32_t>(small_tile_id_str, true);
  }

  if (gamerpicObj.HasMember("cdn_small") &&
      !gamerpicObj["cdn_small"].IsNull()) {
    gamepic.cdn_small = gamerpicObj["cdn_small"].GetString();
  }

  assert_not_zero(gamepic.small_tile_id);
  assert_not_zero(gamepic.big_tile_id);

  return gamepic;
}

bool TitleGamerpicsObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return false;
}
}  // namespace kernel
}  // namespace xe
