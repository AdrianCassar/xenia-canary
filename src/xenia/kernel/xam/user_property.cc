/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

extern "C" {
#include "third_party/FFmpeg/libavutil/base64.h"
}

#include "xenia/base/logging.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_property.h"

namespace xe {
namespace kernel {
namespace xam {

Property::Property(const Property& property)
    : UserData(property), property_id_(property.property_id_) {}

Property::Property(uint32_t property_id, UserDataTypes property_data)
    : UserData(get_type(property_id), property_data) {
  property_id_ = static_cast<AttributeKey>(property_id);
}

Property::Property(uint32_t property_id, uint32_t value_size,
                   uint8_t* value_ptr)
    : UserData(get_type(property_id),
               std::span<const uint8_t>(value_ptr, value_size)) {
  property_id_.value = property_id;
}

Property::Property(const uint8_t* serialized_data, size_t data_size)
    : UserData(std::span<const uint8_t>(serialized_data, data_size)) {
  property_id_.value = *reinterpret_cast<const uint32_t*>(serialized_data);
}

Property::Property(std::span<const uint8_t> serialized_data)
    : UserData(serialized_data) {
  property_id_.value =
      *reinterpret_cast<const uint32_t*>(serialized_data.data());
}

std::vector<uint8_t> Property::Serialize() const {
  std::vector<uint8_t> serialized_property(
      sizeof(AttributeKey) + sizeof(X_USER_DATA) + extended_data_.size());

  memcpy(serialized_property.data(), &property_id_, sizeof(AttributeKey));
  memcpy(serialized_property.data() + sizeof(AttributeKey), &data_,
         sizeof(X_USER_DATA));

  if (requires_additional_data()) {
    memcpy(
        serialized_property.data() + sizeof(AttributeKey) + sizeof(X_USER_DATA),
        extended_data_.data(), extended_data_.size());
  }
  return serialized_property;
}

std::optional<std::string> Property::SerializeToBase64() const {
  const auto property_data = Serialize();

  const uint32_t size = static_cast<uint32_t>(property_data.size());
  const uint32_t out_size = AV_BASE64_SIZE(size);

  std::vector<char> serialized_data(out_size);

  const char* out = av_base64_encode(serialized_data.data(), out_size,
                                     property_data.data(), size);

  if (!out) {
    return std::nullopt;
  }

  const std::string base64 = std::string(serialized_data.data());

  return base64;
}

std::optional<Property> Property::DeserializeBase64(std::string base64) {
  const std::uint32_t size = static_cast<uint32_t>(base64.size());
  const std::uint32_t decode_size = AV_BASE64_DECODE_SIZE(size);

  std::vector<uint8_t> property_data(decode_size);

  const int out =
      av_base64_decode(property_data.data(), base64.c_str(), decode_size);

  if (out < 0) {
    return std::nullopt;
  }

  const xam::Property property = xam::Property(property_data);

  return property;
}

void Property::WriteToGuest(XUSER_PROPERTY* property) const {
  if (!property) {
    return;
  }

  property->property_id = property_id_.value;

  if (requires_additional_data()) {
    property->data.type = data_.type;
    property->data.data.binary.size =
        static_cast<uint32_t>(extended_data_.size());

    memcpy(kernel_memory()->TranslateVirtual(property->data.data.binary.ptr),
           extended_data_.data(), extended_data_.size());
  } else {
    memcpy(&property->data, &data_, sizeof(X_USER_DATA));
  }
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
