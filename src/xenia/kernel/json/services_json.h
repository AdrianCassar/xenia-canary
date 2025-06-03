/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_SERVICES_OBJECT_JSON_H_
#define XENIA_KERNEL_SERVICES_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class ServicesObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  ServicesObjectJSON();
  virtual ~ServicesObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::vector<X_ONLINE_SERVICE_INFO>& ServicesResults() const {
    return services_results_;
  }
  void ServicesResults(const std::vector<X_ONLINE_SERVICE_INFO>& services) {
    services_results_ = services;
  }

  const std::vector<TSADDR>& QuerySearchResults() const {
    return query_search_results_;
  }
  void QuerySearchResults(const std::vector<TSADDR>& query_search_results) {
    query_search_results_ = query_search_results;
  }

 private:
  std::vector<X_ONLINE_SERVICE_INFO> services_results_;
  std::vector<TSADDR> query_search_results_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_SERVICES_OBJECT_JSON_H_