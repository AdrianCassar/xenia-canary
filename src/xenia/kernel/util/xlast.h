/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XLAST_H_
#define XENIA_KERNEL_UTIL_XLAST_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "third_party/pugixml/src/pugixml.hpp"
#include "xenia/kernel/xam/user_property.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

enum class ProductInformationEntry {
  MaxOfflinePlayers,
  MaxSystemLinkPlayers,
  MaxLivePlayers,
  PublisherString,
  DeveloperString,
  MarketingString,
  GenreTypeString
};

inline const std::map<std::string, ProductInformationEntry>
    product_information_entry_string_to_enum = {
        {"offlinePlayersMax", ProductInformationEntry::MaxOfflinePlayers},
        {"systemLinkPlayersMax", ProductInformationEntry::MaxSystemLinkPlayers},
        {"livePlayersMax", ProductInformationEntry::MaxLivePlayers},
        {"publisherStringId", ProductInformationEntry::PublisherString},
        {"developerStringId", ProductInformationEntry::DeveloperString},
        {"sellTextStringId", ProductInformationEntry::MarketingString},
        {"genreTextStringId", ProductInformationEntry::GenreTypeString},
};

inline const std::map<XLanguage, std::string> language_mapping = {
    {XLanguage::kEnglish, "en-US"},    {XLanguage::kJapanese, "ja-JP"},
    {XLanguage::kGerman, "de-DE"},     {XLanguage::kFrench, "fr-FR"},
    {XLanguage::kSpanish, "es-ES"},    {XLanguage::kItalian, "it-IT"},
    {XLanguage::kKorean, "ko-KR"},     {XLanguage::kTChinese, "zh-CHT"},
    {XLanguage::kPortuguese, "pt-PT"}, {XLanguage::kPolish, "pl-PL"},
    {XLanguage::kRussian, "ru-RU"}};

class XLastMatchmakingQuery {
 public:
  XLastMatchmakingQuery();
  XLastMatchmakingQuery(const pugi::xpath_node query_node);

  pugi::xml_node GetQuery(uint32_t query_id) const;

  std::vector<uint32_t> GetSchema() const;
  std::vector<uint32_t> GetConstants() const;
  std::string GetName(uint32_t query_id) const;
  std::vector<uint32_t> GetReturns(uint32_t query_id) const;
  std::vector<uint32_t> GetParameters(uint32_t query_id) const;
  std::vector<uint32_t> GetFiltersLeft(uint32_t query_id) const;
  std::vector<uint32_t> GetFiltersRight(uint32_t query_id) const;

 private:
  pugi::xpath_node node_;
};

class XLastPropertiesQuery {
 public:
  XLastPropertiesQuery();
  XLastPropertiesQuery(const pugi::xpath_node query_node);

  std::vector<uint32_t> GetPropertyIDs() const;
  pugi::xml_node GetPropertyNode(uint32_t property_id) const;
  std::optional<std::string> GetPropertyFriendlyName(
      uint32_t property_id) const;
  std::optional<uint32_t> GetPropertySize(uint32_t property_id) const;
  std::optional<uint32_t> GetPropertyStringID(uint32_t property_id) const;
  pugi::xml_node GetPropertyFormat(uint32_t property_id) const;

 private:
  pugi::xpath_node node_;
};

class XLastContextsQuery {
 public:
  XLastContextsQuery();
  XLastContextsQuery(const pugi::xpath_node query_node);

  std::vector<uint32_t> GetContextsIDs() const;
  pugi::xml_node GetContextNode(uint32_t property_id) const;
  std::optional<std::string> GetContextFriendlyName(uint32_t property_id) const;
  std::optional<uint32_t> GetContextDefaultValue(uint32_t property_id) const;
  pugi::xml_node GetContextValueNode(uint32_t property_id,
                                     uint32_t value) const;
  std::optional<uint32_t> GetContextValueStringID(uint32_t property_id,
                                                  uint32_t value) const;

 private:
  pugi::xpath_node node_;
};

class XLastGameModeQuery {
 public:
  XLastGameModeQuery();
  XLastGameModeQuery(const pugi::xpath_node query_node);

  std::vector<uint32_t> GetGameModeValues() const;
  pugi::xml_node GetGameModeNode(uint32_t value) const;
  std::optional<std::string> GetGameModeFriendlyName(uint32_t value) const;
  std::optional<uint32_t> GetGameModeDefaultValue() const;
  std::optional<uint32_t> GetGameModeStringID(uint32_t value) const;

 private:
  pugi::xpath_node node_;
};

class XLast {
 public:
  XLast() = default;
  XLast(const uint8_t* compressed_xml_data, const uint32_t compressed_data_size,
        const uint32_t decompressed_data_size);
  ~XLast() = default;

  std::u16string GetTitleName() const;
  std::map<ProductInformationEntry, uint32_t> GetProductInformationAttributes()
      const;

  std::vector<XLanguage> GetSupportedLanguages() const;
  std::optional<std::uint32_t> GetGameModeStringId(
      uint32_t game_mode_value) const;
  std::u16string GetLocalizedString(uint32_t string_id,
                                    XLanguage language) const;
  const std::optional<uint32_t> GetPresenceStringId(const uint32_t context_id);
  const std::optional<uint32_t> GetPropertyStringId(const uint32_t property_id);
  const std::u16string GetPresenceRawString(
      const xam::Property* presence_property);
  XLastGameModeQuery* GetGameModeQuery() const;
  XLastContextsQuery* GetContextsQuery() const;
  XLastPropertiesQuery* GetPropertiesQuery() const;
  XLastMatchmakingQuery* GetMatchmakingQuery() const;
  static std::vector<uint32_t> GetAllValuesFromNode(
      const pugi::xpath_node node, const std::string child_name,
      const std::string attribute_name);

  void Dump(std::string file_name) const;

  const bool HasXLast() const { return !xlast_decompressed_xml_.empty(); };

 private:
  std::string GetLocaleStringFromLanguage(XLanguage language) const;

  std::vector<uint8_t> xlast_decompressed_xml_;
  std::unique_ptr<pugi::xml_document> parsed_xlast_ = nullptr;
  pugi::xml_parse_result parse_result_ = {};
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif
