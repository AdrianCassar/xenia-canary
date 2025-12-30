/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_PAGE_GAMERPICS_OBJECT_JSON_H_
#define XENIA_KERNEL_PAGE_GAMERPICS_OBJECT_JSON_H_

#include <cstdint>
#include <string>
#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/json/title_gamerpics_object_json.h"

namespace xe {
namespace kernel {
class PageGamerpicsObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  PageGamerpicsObjectJSON();
  virtual ~PageGamerpicsObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const uint32_t& GetNextPage() const { return next_page_; }
  const uint32_t& GetCurrentPage() const { return page_; }
  const uint32_t& GetTotalPages() const { return pages_; }
  const uint32_t& GetPreviousPage() const { return prev_page_; }
  const uint32_t& GetTotalTitles() const { return total_titles_; }

  const std::vector<GameTitle>& GetTitles() const { return titles_; }

 private:
  uint32_t next_page_ = 0;
  uint32_t page_ = 0;
  uint32_t pages_ = 0;
  uint32_t prev_page_ = 0;
  uint32_t total_titles_ = 0;

  std::vector<GameTitle> titles_ = {};
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_PAGE_GAMERPICS_OBJECT_JSON_H_
