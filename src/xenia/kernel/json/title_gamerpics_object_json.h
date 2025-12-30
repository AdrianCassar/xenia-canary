/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_TITLE_GAMERPICS_OBJECT_JSON_H_
#define XENIA_KERNEL_TITLE_GAMERPICS_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"

namespace xe {
namespace kernel {
struct Gamerpic {
  std::string cdn;
  std::string id;
  uint32_t likes;
  std::string name;
  std::string url;
  std::string url_small;
  std::string cdn_small;
  uint32_t big_tile_id;
  uint32_t small_tile_id;
};

struct GameTitle {
  uint32_t id;
  std::vector<Gamerpic> gamerpics;
  std::string image;
  std::string name;
  std::string slug;
  std::string type;
  std::string url;
};

class TitleGamerpicsObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  TitleGamerpicsObjectJSON();
  virtual ~TitleGamerpicsObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const GameTitle& GetTitle() const { return title_; }

 private:
  Gamerpic DeserializeGamerpic(const rapidjson::Value& gamerpic);

  uint32_t next_page_ = 0;
  uint32_t page_ = 0;
  uint32_t pages_ = 0;
  uint32_t prev_page_ = 0;
  uint32_t total_titles_ = 0;

  GameTitle title_ = {};
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_TITLE_GAMERPICS_OBJECT_JSON_H_
