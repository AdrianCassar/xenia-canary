/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_PROPERTIES_OBJECT_JSON_H_
#define XENIA_KERNEL_PROPERTIES_OBJECT_JSON_H_

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/util/property.h"

namespace xe {
namespace kernel {
class PropertiesObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  PropertiesObjectJSON();
  virtual ~PropertiesObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::vector<Property>& Properties() const { return properties_; }
  void Properties(const std::vector<Property>& properties) {
    properties_ = properties;
  }

 private:
  std::vector<Property> properties_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_PROPERTIES_OBJECT_JSON_H_