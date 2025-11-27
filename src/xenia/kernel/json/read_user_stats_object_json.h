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

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class ReadUserStatsObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  ReadUserStatsObjectJSON(XGI_XUSER_READ_STATS read_stats);

  virtual ~ReadUserStatsObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const XGI_XUSER_READ_STATS& ReadStats() const { return read_stats_; }
  void ReadStats(const XGI_XUSER_READ_STATS& stats) { read_stats_ = stats; }

 private:
  XGI_XUSER_READ_STATS read_stats_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_READ_USER_STATS_OBJECT_JSON_H_
