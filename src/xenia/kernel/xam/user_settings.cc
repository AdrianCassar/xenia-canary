/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

extern "C" {
#include "third_party/FFmpeg/libavutil/base64.h"
}

#include "xenia/kernel/xam/user_settings.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xam {

UserSetting::UserSetting(const UserSetting& setting) : UserData(setting) {
  setting_id_ = setting.setting_id_;
  setting_source_ = setting.setting_source_;
}

UserSetting::UserSetting(UserSettingId setting_id, UserDataTypes setting_data)
    : UserData(get_type(static_cast<uint32_t>(setting_id)), setting_data),
      setting_id_(setting_id),
      setting_source_(X_USER_PROFILE_SETTING_SOURCE::DEFAULT) {}

UserSetting::UserSetting(const X_USER_PROFILE_SETTING* profile_setting)
    : UserData(UserSetting::get_type(profile_setting->setting_id),
               &profile_setting->data),
      setting_id_(
          static_cast<UserSettingId>(profile_setting->setting_id.get())),
      setting_source_(profile_setting->source) {}

UserSetting::UserSetting(const X_XDBF_GPD_SETTING_HEADER* profile_setting,
                         std::span<const uint8_t> extended_data)
    : UserData(profile_setting->setting_type, &profile_setting->base_data,
               extended_data),
      setting_id_(
          static_cast<UserSettingId>(profile_setting->setting_id.get())),
      setting_source_(X_USER_PROFILE_SETTING_SOURCE::TITLE) {}

std::optional<UserSetting> UserSetting::GetDefaultSetting(uint32_t setting_id) {
  for (const auto& setting : default_setting_values) {
    if (setting.get_setting_id() == setting_id) {
      return std::make_optional<UserSetting>(setting);
    }
  }

  const auto type = UserData::get_type(setting_id);

  switch (type) {
    case X_USER_DATA_TYPE::CONTEXT:
    case X_USER_DATA_TYPE::INT32:
    case X_USER_DATA_TYPE::UNSET:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), 0);
    case X_USER_DATA_TYPE::INT64:
    case X_USER_DATA_TYPE::DATETIME:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), static_cast<int64_t>(0));
    case X_USER_DATA_TYPE::DOUBLE:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), 0.0);
    case X_USER_DATA_TYPE::WSTRING:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), std::u16string());
    case X_USER_DATA_TYPE::FLOAT:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), 0.0f);
    case X_USER_DATA_TYPE::BINARY:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), std::vector<uint8_t>());
    default:
      assert_always();
  }

  XELOGE("{}: Unknown X_USER_DATA_TYPE: {}", __func__,
         static_cast<uint8_t>(type));
  return std::nullopt;
}

void UserSetting::WriteToGuest(X_USER_PROFILE_SETTING* setting_ptr,
                               uint32_t& extended_data_address) {
  if (!setting_ptr) {
    return;
  }

  memcpy(&setting_ptr->data.data, &data_.data, sizeof(X_USER_DATA_UNION));
  setting_ptr->data.type = data_.type;

  if (requires_additional_data()) {
    const auto extended_data = get_extended_data();

    if (extended_data.empty()) {
      return;
    }

    setting_ptr->data.data.binary.size =
        static_cast<uint32_t>(extended_data_.size());
    setting_ptr->data.data.binary.ptr = extended_data_address;

    memcpy(kernel_memory()->TranslateVirtual(extended_data_address),
           extended_data_.data(), extended_data_.size());

    extended_data_address +=
        static_cast<uint32_t>(get_max_size(get_setting_id()));
  }
}

std::vector<uint8_t> UserSetting::Serialize() const {
  std::vector<uint8_t> data(sizeof(X_XDBF_GPD_SETTING_HEADER) +
                            extended_data_.size());

  X_XDBF_GPD_SETTING_HEADER header = {};

  header.setting_id = static_cast<uint32_t>(setting_id_);
  header.setting_type = data_.type;

  memcpy(&header.base_data, &data_.data, sizeof(X_USER_DATA_UNION));

  // Copy header to vector
  memcpy(data.data(), &header, sizeof(X_XDBF_GPD_SETTING_HEADER));

  memcpy(data.data() + sizeof(X_XDBF_GPD_SETTING_HEADER), extended_data_.data(),
         extended_data_.size());

  return data;
}

std::optional<std::string> UserSetting::SerializeToBase64() const {
  const auto setting_data = Serialize();

  const uint32_t size = static_cast<uint32_t>(setting_data.size());
  const uint32_t out_size = AV_BASE64_SIZE(size);

  std::vector<char> serialized_data(out_size);

  const char* out = av_base64_encode(serialized_data.data(), out_size,
                                     setting_data.data(), size);

  if (!out) {
    return std::nullopt;
  }

  const std::string base64_out = std::string(serialized_data.data());

  return base64_out;
}

std::optional<UserSetting> UserSetting::DeserializeBase64(std::string base64) {
  const std::uint32_t size = static_cast<uint32_t>(base64.size());
  const std::uint32_t decode_size = AV_BASE64_DECODE_SIZE(size);

  std::vector<uint8_t> setting_out(decode_size);

  const int out_decode_size =
      av_base64_decode(setting_out.data(), base64.c_str(), decode_size);

  if (out_decode_size < 0) {
    return std::nullopt;
  }

  const std::span<uint8_t> setting_data =
      std::span<uint8_t>(setting_out.data(), out_decode_size);

  xam::X_XDBF_GPD_SETTING_HEADER header = {};
  std::span<uint8_t> extended_data = {};

  memcpy(&header, setting_data.data(), sizeof(xam::X_XDBF_GPD_SETTING_HEADER));

  extended_data = setting_data.subspan(sizeof(xam::X_XDBF_GPD_SETTING_HEADER));

  const xam::UserSetting setting(&header, extended_data);

  return setting;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
