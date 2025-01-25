/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/presence_string_builder.h"
#include "xenia/kernel/util/shim_utils.h"

DECLARE_int32(user_language);

namespace xe {
namespace kernel {
namespace util {

AttributeStringFormatter::~AttributeStringFormatter() {}

AttributeStringFormatter::AttributeStringFormatter(
    std::u16string_view attribute_string, XLast* title_xlast,
    uint32_t user_index)
    : attribute_string_(attribute_string), attribute_to_string_mapping_() {
  auto const profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  contexts_ = profile->contexts_;
  properties_ = profile->properties_;

  title_xlast_ = title_xlast;

  presence_string_ = u"";

  if (!ParseAttributeString()) {
    return;
  }

  BuildPresenceString();
}

bool AttributeStringFormatter::ParseAttributeString() {
  auto specifiers = GetPresenceFormatSpecifiers();

  if (specifiers.empty()) {
    return true;
  }

  std::string specifier;
  while (!specifiers.empty()) {
    std::string specifier = specifiers.front();
    attribute_to_string_mapping_[specifier] = GetStringFromSpecifier(specifier);
    specifiers.pop();
  }
  return true;
}

void AttributeStringFormatter::BuildPresenceString() {
  presence_string_ = attribute_string_;

  for (const auto& entry : attribute_to_string_mapping_) {
    presence_string_.replace(presence_string_.find(xe::to_utf16(entry.first)),
                             entry.first.length(), entry.second);
  }
}

AttributeStringFormatter::AttributeType
AttributeStringFormatter::GetAttributeTypeFromSpecifier(
    std::string_view specifier) const {
  if (specifier.length() < 3) {
    return AttributeStringFormatter::AttributeType::Unknown;
  }

  const char presence_type = specifier.at(1);
  if (presence_type == 'c') {
    return AttributeStringFormatter::AttributeType::Context;
  }
  if (presence_type == 'p') {
    return AttributeStringFormatter::AttributeType::Property;
  }
  return AttributeStringFormatter::AttributeType::Unknown;
}

std::optional<uint32_t> AttributeStringFormatter::GetAttributeIdFromSpecifier(
    const std::string& specifier,
    const AttributeStringFormatter::AttributeType specifier_type) const {
  std::smatch string_match;
  if (std::regex_search(specifier, string_match,
                        presence_id_extract_from_specifier)) {
    uint32_t id = 0;

    if (specifier_type == AttributeStringFormatter::AttributeType::Context) {
      id = string_util::from_string<uint32_t>(string_match[2].str());
    }

    if (specifier_type == AttributeStringFormatter::AttributeType::Property) {
      id = string_util::from_string<uint32_t>(string_match[4].str(), true);
    }

    return std::make_optional<uint32_t>(id);
  }

  return std::nullopt;
}

std::u16string AttributeStringFormatter::GetStringFromSpecifier(
    std::string_view specifier) const {
  const AttributeStringFormatter::AttributeType attribute_type =
      GetAttributeTypeFromSpecifier(specifier);

  if (attribute_type == AttributeStringFormatter::AttributeType::Unknown) {
    return u"";
  }

  const auto attribute_id =
      GetAttributeIdFromSpecifier(std::string(specifier), attribute_type);

  if (!attribute_id) {
    return u"";
  }

  if (attribute_type == AttributeStringFormatter::AttributeType::Context) {
    const auto itr = contexts_.find(attribute_id.value());

    if (itr == contexts_.cend()) {
      const auto attribute_placeholder =
          xe::to_utf16(fmt::format("{{c{}}}", attribute_id.value()));

      return attribute_placeholder;
    }

    std::optional<uint32_t> attribute_string_id = std::nullopt;

    if (attribute_id == X_CONTEXT_GAME_MODE &&
        contexts_.contains(X_CONTEXT_GAME_MODE)) {
      attribute_string_id =
          title_xlast_->GetGameModeStringId(contexts_.at(X_CONTEXT_GAME_MODE));
    } else {
      attribute_string_id =
          title_xlast_->GetContextStringId(attribute_id.value(), itr->second);
    }

    if (!attribute_string_id.has_value()) {
      return u"";
    }

    const auto attribute_string = title_xlast_->GetLocalizedString(
        attribute_string_id.value(),
        static_cast<XLanguage>(cvars::user_language));

    return attribute_string;
  }

  if (attribute_type == AttributeStringFormatter::AttributeType::Property) {
    auto itr = std::find_if(
        properties_.begin(), properties_.end(), [attribute_id](Property prop) {
          if (prop.GetPropertyId().value == attribute_id.value()) {
            return true;
          }

          return false;
        });

    if (itr == properties_.end()) {
      const auto attribute_placeholder =
          xe::to_utf16(fmt::format("{{p0x{:08X}}}", attribute_id.value()));

      return attribute_placeholder;
    }

    // Support properties: INT32, INT64, float, double
    const uint64_t value = std::get<uint64_t>(itr->GetValueGuest());

    return xe::to_utf16(fmt::format("{}", value));
  }

  return u"";
}

std::queue<std::string> AttributeStringFormatter::GetPresenceFormatSpecifiers()
    const {
  std::queue<std::string> format_specifiers;

  std::wsmatch match;

  std::wstring attribute_string =
      std::wstring(attribute_string_.begin(), attribute_string_.end());

  while (std::regex_search(attribute_string, match,
                           format_specifier_replace_fragment_regex_)) {
    for (const auto& presence : match) {
      const std::wstring u16presence = presence.str();

      format_specifiers.emplace(
          xe::to_utf8(std::u16string(u16presence.begin(), u16presence.end())));
    }

    attribute_string = match.suffix().str();
  }

  return format_specifiers;
}
}  // namespace util
}  // namespace kernel
}  // namespace xe