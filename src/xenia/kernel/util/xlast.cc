/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xlast.h"
#include "third_party/zlib/zlib.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/util/presence_string_builder.h"

DECLARE_int32(user_language);

namespace xe {
namespace kernel {
namespace util {

#pragma region XLastMatchmakingQuery

XLastMatchmakingQuery::XLastMatchmakingQuery() {}
XLastMatchmakingQuery::XLastMatchmakingQuery(
    const pugi::xpath_node query_node) {
  node_ = query_node;
}

pugi::xml_node XLastMatchmakingQuery::GetQuery(uint32_t query_id) const {
  pugi::xml_node query_node;

  std::string xpath = fmt::format("Queries/Query[@id = \"{}\"]", query_id);

  query_node = node_.node().select_node(xpath.c_str()).node();

  return query_node;
}
std::vector<uint32_t> XLastMatchmakingQuery::GetSchema() const {
  return XLast::GetAllValuesFromNode(node_.parent().parent(), "Schema", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetConstants() const {
  return XLast::GetAllValuesFromNode(node_.parent().parent(), "Constants",
                                     "id");
}

std::string XLastMatchmakingQuery::GetName(uint32_t query_id) const {
  return GetQuery(query_id).attribute("friendlyName").value();
}

std::vector<uint32_t> XLastMatchmakingQuery::GetReturns(
    uint32_t query_id) const {
  return XLast::GetAllValuesFromNode(GetQuery(query_id), "Returns", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetParameters(
    uint32_t query_id) const {
  return XLast::GetAllValuesFromNode(GetQuery(query_id), "Parameters", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetFiltersLeft(
    uint32_t query_id) const {
  return XLast::GetAllValuesFromNode(GetQuery(query_id), "Filters", "left");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetFiltersRight(
    uint32_t query_id) const {
  return XLast::GetAllValuesFromNode(GetQuery(query_id), "Filters", "right");
}

#pragma endregion

#pragma region XLastPropertiesQuery

XLastPropertiesQuery::XLastPropertiesQuery() {}
XLastPropertiesQuery::XLastPropertiesQuery(const pugi::xpath_node query_node) {
  node_ = query_node;
}

std::vector<uint32_t> XLastPropertiesQuery::GetPropertyIDs() const {
  std::vector<uint32_t> result = {};

  for (pugi::xml_node child : node_.node().children()) {
    result.push_back(xe::string_util::from_string<uint32_t>(
        child.attribute("id").value(), true));
  }

  return result;
}

pugi::xml_node XLastPropertiesQuery::GetPropertyNode(
    uint32_t property_id) const {
  pugi::xml_node property_node;

  std::string xpath = fmt::format("Property[@id = \"0x{:08X}\"]", property_id);

  property_node = node_.node().select_node(xpath.c_str()).node();

  return property_node;
}

std::optional<std::string> XLastPropertiesQuery::GetPropertyFriendlyName(
    uint32_t property_id) const {
  std::optional<std::string> value = std::nullopt;

  pugi::xml_node property_node = GetPropertyNode(property_id);

  if (property_node) {
    value = property_node.attribute("friendlyName").as_string();
  }

  return value;
}

std::optional<uint32_t> XLastPropertiesQuery::GetPropertySize(
    uint32_t property_id) const {
  std::optional<uint32_t> value = std::nullopt;

  pugi::xml_node property_node = GetPropertyNode(property_id);

  if (property_node) {
    value = property_node.attribute("dataSize").as_uint();
  }

  return value;
}

std::optional<uint32_t> XLastPropertiesQuery::GetPropertyStringID(
    uint32_t property_id) const {
  std::optional<uint32_t> value = std::nullopt;

  pugi::xml_node property_node = GetPropertyNode(property_id);

  if (property_node) {
    value = property_node.attribute("stringId").as_uint();
  }

  return value;
}

pugi::xml_node XLastPropertiesQuery::GetPropertyFormat(
    uint32_t property_id) const {
  return GetPropertyNode(property_id).child("Format");
}

#pragma endregion

#pragma region XLastContextsQuery

XLastContextsQuery::XLastContextsQuery() {}
XLastContextsQuery::XLastContextsQuery(const pugi::xpath_node query_node) {
  node_ = query_node;
}

std::vector<uint32_t> XLastContextsQuery::GetContextsIDs() const {
  std::vector<uint32_t> result = {};

  for (pugi::xml_node child : node_.node().children()) {
    result.push_back(xe::string_util::from_string<uint32_t>(
        child.attribute("id").value(), true));
  }

  return result;
}

pugi::xml_node XLastContextsQuery::GetContextNode(uint32_t property_id) const {
  pugi::xml_node property_node;

  std::string xpath = fmt::format("Context[@id = \"0x{:08X}\"]", property_id);

  property_node = node_.node().select_node(xpath.c_str()).node();

  return property_node;
}

std::optional<std::string> XLastContextsQuery::GetContextFriendlyName(
    uint32_t property_id) const {
  std::optional<std::string> value = std::nullopt;

  pugi::xml_node property_node = GetContextNode(property_id);

  if (property_node) {
    value = property_node.attribute("friendlyName").as_string();
  }

  return value;
}

std::optional<uint32_t> XLastContextsQuery::GetContextDefaultValue(
    uint32_t property_id) const {
  std::optional<uint32_t> value = std::nullopt;

  pugi::xml_node property_node = GetContextNode(property_id);

  if (property_node) {
    value = property_node.attribute("defaultValue").as_uint();
  }

  return value;
}

pugi::xml_node XLastContextsQuery::GetContextValueNode(uint32_t property_id,
                                                       uint32_t value) const {
  pugi::xml_node context_node = GetContextNode(property_id);

  pugi::xml_node context_value_node;

  std::string xpath = fmt::format("ContextValue[@value = \"{}\"]", value);

  if (context_node) {
    context_value_node = context_node.select_node(xpath.c_str()).node();
  }

  return context_value_node;
}

std::optional<uint32_t> XLastContextsQuery::GetContextValueStringID(
    uint32_t property_id, uint32_t value) const {
  pugi::xml_node context_value_node = GetContextValueNode(property_id, value);

  std::optional<uint32_t> string_id = std::nullopt;

  if (context_value_node) {
    string_id = context_value_node.attribute("stringId").as_uint();
  }

  return string_id;
}

#pragma endregion

#pragma region XLastGameModeQuery

XLastGameModeQuery::XLastGameModeQuery() {}
XLastGameModeQuery::XLastGameModeQuery(const pugi::xpath_node query_node) {
  node_ = query_node;
}

std::vector<uint32_t> XLastGameModeQuery::GetGameModeValues() const {
  std::vector<uint32_t> result = {};

  for (pugi::xml_node child : node_.node().children()) {
    result.push_back(xe::string_util::from_string<uint32_t>(
        child.attribute("value").value(), false));
  }

  return result;
}

pugi::xml_node XLastGameModeQuery::GetGameModeNode(
    uint32_t gamemode_value) const {
  pugi::xml_node gamemode_node;

  std::string xpath = fmt::format("GameMode[@value = \"{}\"]", gamemode_value);
  gamemode_node = node_.node().select_node(xpath.c_str()).node();

  return gamemode_node;
}

std::optional<uint32_t> XLastGameModeQuery::GetGameModeDefaultValue() const {
  std::optional<uint32_t> value = std::nullopt;

  value = node_.node().attribute("defaultValue").as_uint();

  return value;
}

std::optional<std::string> XLastGameModeQuery::GetGameModeFriendlyName(
    uint32_t gamemode_value) const {
  std::optional<std::string> value = std::nullopt;

  pugi::xml_node gamemode_node = GetGameModeNode(gamemode_value);

  if (gamemode_node) {
    value = gamemode_node.attribute("friendlyName").as_string();
  }

  return value;
}

std::optional<uint32_t> XLastGameModeQuery::GetGameModeStringID(
    uint32_t value) const {
  pugi::xml_node context_value_node = GetGameModeNode(value);

  std::optional<uint32_t> string_id = std::nullopt;

  if (context_value_node) {
    string_id = context_value_node.attribute("stringId").as_uint();
  }

  return string_id;
}

#pragma endregion

XLast::XLast(const uint8_t* compressed_xml_data,
             const uint32_t compressed_data_size,
             const uint32_t decompressed_data_size) {
  if (!compressed_data_size || !decompressed_data_size) {
    XELOGW("XLast: Current title don't have any XLast XML data!");
    return;
  }

  parsed_xlast_ = std::make_unique<pugi::xml_document>();
  xlast_decompressed_xml_.resize(decompressed_data_size);

  z_stream stream;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;
  stream.avail_in = 0;
  stream.next_in = Z_NULL;

  int ret = inflateInit2(
      &stream, 16 + MAX_WBITS);  // 16 + MAX_WBITS enables gzip decoding
  if (ret != Z_OK) {
    XELOGE("XLast: Error during Zlib stream init");
    return;
  }

  stream.avail_in = compressed_data_size;
  stream.next_in =
      reinterpret_cast<Bytef*>(const_cast<uint8_t*>(compressed_xml_data));
  stream.avail_out = decompressed_data_size;
  stream.next_out = reinterpret_cast<Bytef*>(xlast_decompressed_xml_.data());

  ret = inflate(&stream, Z_NO_FLUSH);
  if (ret == Z_STREAM_ERROR) {
    XELOGE("XLast: Error during XLast decompression");
    inflateEnd(&stream);
    return;
  }
  inflateEnd(&stream);

  parse_result_ = parsed_xlast_->load_buffer(xlast_decompressed_xml_.data(),
                                             xlast_decompressed_xml_.size());
}

std::u16string XLast::GetTitleName() const {
  std::string xpath = "/XboxLiveSubmissionProject/GameConfigProject";

  if (!HasXLast()) {
    return u"";
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return u"";
  }

  return xe::to_utf16(node.node().attribute("titleName").as_string());
}

std::map<ProductInformationEntry, uint32_t>
XLast::GetProductInformationAttributes() const {
  std::map<ProductInformationEntry, uint32_t> attributes;

  std::string xpath =
      "/XboxLiveSubmissionProject/GameConfigProject/ProductInformation";

  if (!HasXLast()) {
    return attributes;
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return attributes;
  }

  const auto node_attributes = node.node().attributes();
  for (const auto& attribute : node_attributes) {
    const auto entry =
        product_information_entry_string_to_enum.find(attribute.name());
    if (entry == product_information_entry_string_to_enum.cend()) {
      XELOGW("GetProductInformationAttributes: Missing attribute: {}",
             attribute.name());
      continue;
    }

    std::string attribute_value = std::string(attribute.value());
    if (attribute_value.empty()) {
      XELOGW(
          "GetProductInformationAttributes: Attribute: {} Contains no value!",
          attribute.name());
      continue;
    }

    attributes.emplace(entry->second,
                       xe::string_util::from_string<uint32_t>(attribute_value));
  }

  return attributes;
}

std::vector<XLanguage> XLast::GetSupportedLanguages() const {
  std::vector<XLanguage> languages;

  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings");

  if (!HasXLast()) {
    return languages;
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return languages;
  }

  const auto locale = node.node().children("SupportedLocale");
  for (auto itr = locale.begin(); itr != locale.end(); itr++) {
    const std::string locale_name = itr->attribute("locale").as_string();

    for (const auto& language : language_mapping) {
      if (language.second == locale_name) {
        languages.push_back(language.first);
      }
    }
  }

  return languages;
}

std::optional<std::uint32_t> XLast::GetGameModeStringId(
    uint32_t game_mode_value) const {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/GameModes/"
      "GameMode[@value = \"{}\"]",
      game_mode_value);

  std::optional<uint32_t> value = std::nullopt;

  if (!HasXLast()) {
    return value;
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (node) {
    value = node.node().attribute("stringId").as_uint();
  }

  return value;
}

std::u16string XLast::GetLocalizedString(uint32_t string_id,
                                         XLanguage language) const {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings/"
      "LocalizedString[@id = \"{}\"]",
      string_id);

  if (!HasXLast()) {
    return u"";
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return u"";
  }

  const std::string locale_name = GetLocaleStringFromLanguage(language);
  const pugi::xml_node locale_node =
      node.node().find_child_by_attribute("locale", locale_name.c_str());

  if (!locale_node) {
    return u"";
  }

  return xe::to_utf16(locale_node.child_value());
}

const std::optional<uint32_t> XLast::GetPresenceStringId(
    const uint32_t context_id) {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/Presence/"
      "PresenceMode[@contextValue = \"{}\"]",
      context_id);

  std::optional<uint32_t> string_id = std::nullopt;

  if (!HasXLast()) {
    return string_id;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());

  if (node) {
    string_id = node.node().attribute("stringId").as_uint();
  }

  return string_id;
}

const std::u16string XLast::GetPresenceRawString(
    const xam::Property* presence_property) {
  const uint32_t presence_value = presence_property->get_data()->data.u32;

  const std::optional<uint32_t> presence_string_id =
      GetPresenceStringId(presence_value);

  std::u16string raw_presence = u"";

  if (presence_string_id.has_value()) {
    raw_presence =
        GetLocalizedString(presence_string_id.value(),
                           static_cast<XLanguage>(cvars::user_language));
  }

  return raw_presence;
}

XLastGameModeQuery* XLast::GetGameModeQuery() const {
  std::string xpath =
      fmt::format("/XboxLiveSubmissionProject/GameConfigProject/GameModes");

  XLastGameModeQuery* query = nullptr;

  if (!HasXLast()) {
    return query;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return query;
  }

  return new XLastGameModeQuery(node);
}

XLastContextsQuery* XLast::GetContextsQuery() const {
  std::string xpath =
      fmt::format("/XboxLiveSubmissionProject/GameConfigProject/Contexts");

  XLastContextsQuery* query = nullptr;

  if (!HasXLast()) {
    return query;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return query;
  }

  return new XLastContextsQuery(node);
}

XLastPropertiesQuery* XLast::GetPropertiesQuery() const {
  std::string xpath =
      fmt::format("/XboxLiveSubmissionProject/GameConfigProject/Properties");

  XLastPropertiesQuery* query = nullptr;

  if (!HasXLast()) {
    return query;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return query;
  }

  return new XLastPropertiesQuery(node);
}

XLastMatchmakingQuery* XLast::GetMatchmakingQuery() const {
  std::string xpath =
      fmt::format("/XboxLiveSubmissionProject/GameConfigProject/Matchmaking");

  XLastMatchmakingQuery* matchmaking = nullptr;

  if (!HasXLast()) {
    return matchmaking;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return matchmaking;
  }

  return new XLastMatchmakingQuery(node);
}

std::vector<uint32_t> XLast::GetAllValuesFromNode(
    const pugi::xpath_node node, const std::string child_name,
    const std::string attribute_name) {
  std::vector<uint32_t> result{};

  const auto searched_child = node.node().child(child_name.c_str());

  for (pugi::xml_node_iterator itr = searched_child.begin();
       itr != searched_child.end(); itr++) {
    result.push_back(xe::string_util::from_string<uint32_t>(
        itr->attribute(attribute_name.c_str()).value(), true));
  }

  return result;
}

void XLast::Dump(std::string file_name) const {
  if (!HasXLast()) {
    return;
  }

  if (file_name.empty()) {
    file_name = xe::to_utf8(GetTitleName());
  }

  const std::string file = fmt::format("{}.xml", file_name);

  if (std::filesystem::exists(file)) {
    return;
  }

  FILE* outfile = xe::filesystem::OpenFile(file.c_str(), "ab");

  if (outfile) {
    fwrite(xlast_decompressed_xml_.data(), 1, xlast_decompressed_xml_.size(),
           outfile);

    XELOGI("XLast file saved {}", file);
  }

  fclose(outfile);
}

std::string XLast::GetLocaleStringFromLanguage(XLanguage language) const {
  const auto value = language_mapping.find(language);
  if (value != language_mapping.cend()) {
    return value->second;
  }

  return language_mapping.at(XLanguage::kEnglish);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
