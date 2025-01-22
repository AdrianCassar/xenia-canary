/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_SERVICEINFO_OBJECT_JSON_H_
#define XENIA_KERNEL_SERVICEINFO_OBJECT_JSON_H_

#include "xenia/kernel/json/base_object_json.h"

namespace xe {
namespace kernel {
class ServiceInfoObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  ServiceInfoObjectJSON();
  virtual ~ServiceInfoObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::string& Address() const { return hostAddress_; }
  void Address(const std::string& hostAddress) { hostAddress_ = hostAddress; }

  const uint16_t& Port() const { return port_; }
  void Port(const uint16_t& port) { port_ = port; }

 private:
  std::string hostAddress_;
  uint16_t port_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_SERVICEINFO_OBJECT_JSON_H_