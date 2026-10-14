/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_READ_USER_STATS_OBJECT_JSON_H_
#define XENIA_KERNEL_READ_USER_STATS_OBJECT_JSON_H_

#include <optional>
#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class ReadUserStatsObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  struct EnumerateParams {
    X_STATS_ENUMERATOR_TYPE type;
    uint64_t xuid;
    uint32_t rank;
    int64_t rating;
    uint32_t num_rows;
  };

  // XUserReadStats
  ReadUserStatsObjectJSON(const XGI_XUSER_READ_STATS& read_stats);

  // XUserCreateStatsEnumeratorByXuid/ByRank/ByRating
  ReadUserStatsObjectJSON(uint32_t title_id,
                          std::vector<X_USER_STATS_SPEC> specs,
                          EnumerateParams params);

  virtual ~ReadUserStatsObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::vector<uint64_t>& XUIDs() const { return xuids_; }
  const std::vector<X_USER_STATS_SPEC>& Specs() const { return specs_; }

 private:
  uint32_t title_id_ = 0;
  std::vector<uint64_t> xuids_;
  std::vector<X_USER_STATS_SPEC> specs_;
  std::optional<EnumerateParams> enumerate_params_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_READ_USER_STATS_OBJECT_JSON_H_
