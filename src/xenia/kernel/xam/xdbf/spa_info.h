/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_SPA_INFO_H_
#define XENIA_KERNEL_XAM_XDBF_SPA_INFO_H_

#include "xenia/kernel/xam/xdbf/xdbf_io.h"

#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "xenia/avatars/asset_pack.h"
#include "xenia/base/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.h
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.cpp

enum class SpaSection : uint16_t {
  kMetadata = 0x0001,
  kImage = 0x0002,
  kStringTable = 0x0003,
};

enum class TitleType : uint32_t {
  kSystem = 0,
  kFull = 1,
  kDemo = 2,
  kDownload = 3,
  kUnknown = 4,
  kApp = 5
};

enum class TitleFlags {
  kAlwaysIncludeInProfile = 1,
  kNeverIncludeInProfile = 2,
};

constexpr uint32_t kViewTypeMask = 0xF;

enum class ViewType : uint32_t {
  kLeaderboard = 0,
  kContextByProperty = 1,
  kContextByContext = 2
};

enum class StatsViewFlags : uint16_t {
  kArbitrated = 16,
  kHidden = 32,
  kTeamView = 64,
  kOnlineOnly = 128,
};

enum class AggregationType : uint16_t {
  kLast = 0x8001,
  kMax = 0x800B,
  kSum = 0x8003,
  kMin = 0x8009,
};

enum class ViewFieldEntryFlags : uint32_t {
  kHidden = 1,
};

enum class ViewFieldType : uint8_t { kContextField, kPropertyField };

// https://github.com/hetelek/Velocity/blob/cf0b84cc8bbfad09c655476c6a3c762836ce1246/XboxInternals/AvatarAsset/AvatarAssetDefinintions.h#L5
enum AssetSubcategory {
  CarryableCarryable = 0x44c,
  CarryableFirst = 0x44c,
  CarryableLast = 0x44c,
  CostumeCasualSuit = 0x68,
  CostumeCostume = 0x69,
  CostumeFirst = 100,
  CostumeFormalSuit = 0x67,
  CostumeLast = 0x6a,
  CostumeLongDress = 0x65,
  CostumeShortDress = 100,
  EarringsDanglers = 0x387,
  EarringsFirst = 900,
  EarringsLargehoops = 0x38b,
  EarringsLast = 0x38b,
  EarringsSingleDangler = 0x386,
  EarringsSingleLargeHoop = 0x38a,
  EarringsSingleSmallHoop = 0x388,
  EarringsSingleStud = 900,
  EarringsSmallHoops = 0x389,
  EarringsStuds = 0x385,
  GlassesCostume = 0x2be,
  GlassesFirst = 700,
  GlassesGlasses = 700,
  GlassesLast = 0x2be,
  GlassesSunglasses = 0x2bd,
  GlovesFingerless = 600,
  GlovesFirst = 600,
  GlovesFullFingered = 0x259,
  GlovesLast = 0x259,
  HatBaseballCap = 0x1f6,
  HatBeanie = 500,
  HatBearskin = 0x1fc,
  HatBrimmed = 0x1f8,
  HatCostume = 0x1fb,
  HatFez = 0x1f9,
  HatFirst = 500,
  HatFlatCap = 0x1f5,
  HatHeadwrap = 0x1fa,
  HatHelmet = 0x1fd,
  HatLast = 0x1fd,
  HatPeakCap = 0x1f7,
  RingFirst = 0x3e8,
  RingLast = 0x3ea,
  RingLeft = 0x3e9,
  RingRight = 0x3e8,
  ShirtCoat = 210,
  ShirtFirst = 200,
  ShirtHoodie = 0xd0,
  ShirtJacket = 0xd1,
  ShirtLast = 210,
  ShirtLongSleeveShirt = 0xce,
  ShirtLongSleeveTee = 0xcc,
  ShirtPolo = 0xcb,
  ShirtShortSleeveShirt = 0xcd,
  ShirtSportsTee = 200,
  ShirtSweater = 0xcf,
  ShirtTee = 0xc9,
  ShirtVest = 0xca,
  ShoesCostume = 0x197,
  ShoesFirst = 400,
  ShoesFormal = 0x193,
  ShoesHeels = 0x191,
  ShoesHighBoots = 0x196,
  ShoesLast = 0x197,
  ShoesPumps = 0x192,
  ShoesSandals = 400,
  ShoesShortBoots = 0x195,
  ShoesTrainers = 0x194,
  TrousersCargo = 0x131,
  TrousersFirst = 300,
  TrousersHotpants = 300,
  TrousersJeans = 0x132,
  TrousersKilt = 0x134,
  TrousersLast = 0x135,
  TrousersLeggings = 0x12f,
  TrousersLongShorts = 0x12e,
  TrousersLongSkirt = 0x135,
  TrousersShorts = 0x12d,
  TrousersShortSkirt = 0x133,
  TrousersTrousers = 0x130,
  WristwearBands = 0x322,
  WristwearBracelet = 800,
  WristwearFirst = 800,
  WristwearLast = 0x323,
  WristwearSweatbands = 0x323,
  WristwearWatch = 0x321
};

// System Attribute Ids
constexpr uint32_t RankAttributeId = 0xFFFF;
constexpr uint32_t RatingAttributeId = 0xFFFE;
constexpr uint32_t GamertagAttributeId = 0xFFFD;
constexpr uint32_t AttachmentSizeAttributeId = 0xFFFA;

constexpr inline std::string GetAggregationTypeName(
    const uint32_t aggregation_type) {
  switch (static_cast<AggregationType>(aggregation_type)) {
    case AggregationType::kLast:
      return "Last";
    case AggregationType::kMax:
      return "Max";
    case AggregationType::kSum:
      return "Sum";
    case AggregationType::kMin:
      return "Min";
    default:
      return "";
  }
}

constexpr inline std::string GetViewTypeName(const uint32_t view_type) {
  switch (static_cast<ViewType>(view_type)) {
    case ViewType::kLeaderboard:
      return "Leaderboard";
    case ViewType::kContextByProperty:
      return "Context by Property";
    case ViewType::kContextByContext:
      return "Context by Context";
    default:
      return "";
  }
}

constexpr inline std::string AttributeIdToName(const uint16_t id) {
  switch (id) {
    case RankAttributeId:
      return "Rank";
    case RatingAttributeId:
      return "Rating";
    case GamertagAttributeId:
      return "Gamertag";
    case AttachmentSizeAttributeId:
      return "Attachment Size";
    default:
      return "";
  }
}

constexpr inline std::string AssetSubcategoryToString(
    AssetSubcategory category) {
  switch (category) {
    case CarryableCarryable:
      return "Carryable, Carryable";
    case CostumeCasualSuit:
      return "Costume, Casual Suit";
    case CostumeCostume:
      return "Costume, Costume";
    case CostumeFormalSuit:
      return "Costume, Formal Suit";
    case CostumeLast:
      return "Costume, Last";
    case CostumeLongDress:
      return "Costume, Long Dress";
    case CostumeShortDress:
      return "Costume, Short Dress";
    case EarringsDanglers:
      return "Earrings, Danglers";
    case EarringsLargehoops:
      return "Earrings, Largehoops";
    case EarringsSingleDangler:
      return "Earrings, Single Dangler";
    case EarringsSingleLargeHoop:
      return "Earrings, Single Large Hoop";
    case EarringsSingleSmallHoop:
      return "Earrings, Single Small Hoop";
    case EarringsSingleStud:
      return "Earrings, Single Stud";
    case EarringsSmallHoops:
      return "Earrings, Small Hoops";
    case EarringsStuds:
      return "Earrings, Studs";
    case GlassesCostume:
      return "Glasses, Costume";
    case GlassesGlasses:
      return "Glasses, Glasses";
    case GlassesSunglasses:
      return "Glasses, Sunglasses";
    case GlovesFingerless:
      return "Gloves, Fingerless";
    case GlovesFullFingered:
      return "Gloves, Full Fingered";
    case HatBaseballCap:
      return "Hat, Baseball Cap";
    case HatBeanie:
      return "Hat, Beanie";
    case HatBearskin:
      return "Hat, Bearskin";
    case HatBrimmed:
      return "Hat, Brimmed";
    case HatCostume:
      return "Hat, Costume";
    case HatFez:
      return "Hat, Fez";
    case HatFlatCap:
      return "Hat, Flat Cap";
    case HatHeadwrap:
      return "Hat, Headwrap";
    case HatHelmet:
      return "Hat, Helmet";
    case HatPeakCap:
      return "Hat, Peak Cap";
    case RingLast:
      return "Ring, Last";
    case RingLeft:
      return "Ring, Left";
    case RingRight:
      return "Ring, Right";
    case ShirtCoat:
      return "Shirt, Coat";
    case ShirtHoodie:
      return "Shirt, Hoodie";
    case ShirtJacket:
      return "Shirt, Jacket";
    case ShirtLongSleeveShirt:
      return "Shirt, Long Sleeve Shirt";
    case ShirtLongSleeveTee:
      return "Shirt, Long Sleeve Tee";
    case ShirtPolo:
      return "Shirt, Polo";
    case ShirtShortSleeveShirt:
      return "Shirt, Short Sleeve Shirt";
    case ShirtSportsTee:
      return "Shirt, Sports Tee";
    case ShirtSweater:
      return "Shirt, Sweater";
    case ShirtTee:
      return "Shirt, Tee";
    case ShirtVest:
      return "Shirt, Vest";
    case ShoesCostume:
      return "Shoes, Costume";
    case ShoesFormal:
      return "Shoes, Formal";
    case ShoesHeels:
      return "Shoes, Heels";
    case ShoesHighBoots:
      return "Shoes, High Boots";
    case ShoesPumps:
      return "Shoes, Pumps";
    case ShoesSandals:
      return "Shoes, Sandals";
    case ShoesShortBoots:
      return "Shoes, Short Boots";
    case ShoesTrainers:
      return "Shoes, Trainers";
    case TrousersCargo:
      return "Trousers, Cargo";
    case TrousersHotpants:
      return "Trousers, Hotpants";
    case TrousersJeans:
      return "Trousers, Jeans";
    case TrousersKilt:
      return "Trousers, Kilt";
    case TrousersLeggings:
      return "Trousers, Leggings";
    case TrousersLongShorts:
      return "Trousers, Long Shorts";
    case TrousersLongSkirt:
      return "Trousers, Long Skirt";
    case TrousersShorts:
      return "Trousers, Shorts";
    case TrousersShortSkirt:
      return "Trousers, Short Skirt";
    case TrousersTrousers:
      return "Trousers, Trousers";
    case WristwearBands:
      return "Wristwear, Bands";
    case WristwearBracelet:
      return "Wristwear, Bracelet";
    case WristwearSweatbands:
      return "Wristwear, Sweatbands";
    case WristwearWatch:
      return "Wristwear, Watch";
    default:
      return "";
  }
}

constexpr inline std::string GetViewTypeName(const ViewType view_type) {
  return GetViewTypeName(static_cast<uint32_t>(view_type));
}

constexpr inline ViewType GetViewType(const uint32_t flags) {
  return static_cast<ViewType>(flags & kViewTypeMask);
}

constexpr inline bool IsArbitrated(const uint32_t flags) {
  return flags & static_cast<uint32_t>(StatsViewFlags::kArbitrated);
}

constexpr inline bool IsHidden(const uint32_t flags) {
  return flags & static_cast<uint32_t>(StatsViewFlags::kHidden);
}

constexpr inline bool IsTeamView(const uint32_t flags) {
  return flags & static_cast<uint32_t>(StatsViewFlags::kTeamView);
}

constexpr inline bool IsOnlineOnly(const uint32_t flags) {
  return flags & static_cast<uint32_t>(StatsViewFlags::kOnlineOnly);
}

constexpr inline uint32_t GetSkillLeaderboardId(uint32_t game_type,
                                                uint32_t game_mode) {
  return (0xFFF00000 | (game_type == 1 ? 0xF0000 : 0xE0000)) |
         (game_mode & 0xFFFF);
}

constexpr inline bool IsLeaderboardIdSkill(uint32_t id) {
  return (id & 0x2000000) != 0;
}

#pragma pack(push, 1)
struct PropertyBagEntry {
  xe::be<uint32_t> contexts_count;
  xe::be<uint32_t> properties_count;
};

struct PropertyBag {
  std::set<xe::be<uint32_t>> contexts;
  std::set<xe::be<uint32_t>> properties;
};

struct TitleHeaderData {
  xe::be<uint32_t> title_id;
  xe::be<TitleType> title_type;
  xe::be<uint16_t> major;
  xe::be<uint16_t> minor;
  xe::be<uint16_t> build;
  xe::be<uint16_t> revision;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> padding_1;
  xe::be<uint32_t> padding_2;
  xe::be<uint32_t> padding_3;
};
static_assert_size(TitleHeaderData, 32);

struct StatsViewTableEntry {
  xe::be<uint32_t> id;
  xe::be<uint32_t> flags;  // StatsViewFlags
  xe::be<uint16_t> shared_index;
  xe::be<uint16_t> string_id;
  xe::be<uint32_t> unused;
};
static_assert_size(StatsViewTableEntry, 0x10);

struct ViewFieldEntry {
  xe::be<uint32_t> size;
  xe::be<uint32_t> property_id;
  xe::be<uint32_t> flags;  // ViewFieldEntryFlags
  xe::be<uint16_t> attribute_id;
  xe::be<uint16_t> string_id;
  xe::be<uint16_t> aggregation_type;  // AggregationType
  xe::be<uint8_t> ordinal;
  xe::be<uint8_t> field_type;
  xe::be<uint32_t> format_type;
  xe::be<uint32_t> unused_1;
  xe::be<uint32_t> unused_2;
};
static_assert_size(ViewFieldEntry, 0x20);

struct SharedViewMetaTableEntry {
  xe::be<uint16_t> column_count;
  xe::be<uint16_t> row_count;
  xe::be<uint32_t> unused_1;
  xe::be<uint32_t> unused_2;
};
static_assert_size(SharedViewMetaTableEntry, 0xC);

struct SharedView {
  std::vector<ViewFieldEntry> column_entries;
  std::vector<ViewFieldEntry> row_entries;
  PropertyBag property_bag;
};

struct ViewTable {
  StatsViewTableEntry view_entry;
  SharedView shared_view;
};

struct PresenceTableEntry {
  PropertyBag property_bag;
  std::vector<PropertyBag> presence_modes;
};

struct AchievementTableEntry {
  xe::be<uint16_t> id;
  xe::be<uint16_t> label_id;
  xe::be<uint16_t> description_id;
  xe::be<uint16_t> unachieved_id;
  xe::be<uint32_t> image_id;
  xe::be<uint16_t> gamerscore;
  xe::be<uint16_t> unused;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> unused1;
  xe::be<uint32_t> unused2;
  xe::be<uint32_t> unused3;
  xe::be<uint32_t> unused4;
};
static_assert_size(AchievementTableEntry, 0x24);

struct AvatarItemsTableHeaderEntry {
  xe::be<uint16_t> count;
};
static_assert_size(AvatarItemsTableHeaderEntry, 2);

struct AvatarAwardEntry {
  xe::avatars::AssetId asset_id;
  xe::be<uint16_t> display_string_id;
  xe::be<uint16_t> description_string_id;
  xe::be<uint16_t> unachieved_string_id;
  xe::be<uint16_t> reserved;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> sub_category;
};
static_assert_size(AvatarAwardEntry, 0x24);

struct AvatarAwardsTableEntry {
  std::vector<AvatarAwardEntry> avatar_awards;
};
#pragma pack(pop)

class SpaInfo : public XdbfFile {
 public:
  SpaInfo(const std::span<uint8_t> buffer);
  ~SpaInfo() = default;

  void Load();

  const uint8_t* ReadXLast(uint32_t& compressed_size,
                           uint32_t& decompressed_size);

  // Checks if provided language exist, if not returns default title language.
  XLanguage GetExistingLanguage(XLanguage language_to_check) const;

  // The game icon image, if found.
  std::span<const uint8_t> title_icon() const;

  std::span<const uint8_t> GetIcon(uint64_t id) const;

  // The game's default language.
  XLanguage default_language() const;

  bool is_system_app() const;
  bool is_demo() const;
  bool include_in_profile() const;

  uint32_t title_id() const;
  // The game's title in its default language.
  std::string title_name() const;

  std::string title_name(XLanguage language) const;

  uint32_t achievement_count() const {
    return static_cast<uint32_t>(achievements_.size());
  }

  const AchievementTableEntry* GetAchievement(uint32_t id);

  std::vector<const AchievementTableEntry*> GetAchievements() const {
    return achievements_;
  }

  std::vector<const XdbfContextTableEntry*> GetContexts() const {
    return contexts_;
  }

  std::vector<const XdbfPropertyTableEntry*> GetProperties() const {
    return properties_;
  }

  const std::vector<ViewTable>* GetStatsViews() const { return &stats_views_; }

  const PresenceTableEntry* GetPresence() const { return &presence_; }

  const AvatarAwardsTableEntry* GetAvatarAwards() const {
    return &avatar_awards_;
  }

  const PropertyBag* GetMatchCollection() const { return &matchmaking_; }

  const XdbfContextTableEntry* GetContext(uint32_t id);
  const XdbfPropertyTableEntry* GetProperty(uint32_t id);
  const std::optional<ViewTable> GetStatsView(uint32_t id);
  const std::optional<PropertyBag> GetPresenceMode(
      uint32_t context_value) const;
  const std::optional<AvatarAwardEntry> GetAvatarAward(uint32_t award_id) const;

  uint32_t total_gamerscore() const {
    return std::accumulate(achievements_.cbegin(), achievements_.cend(), 0,
                           [](uint32_t sum, const auto& entry) {
                             return sum + entry->gamerscore;
                           });
  }

  friend bool operator<(const SpaInfo& first, const SpaInfo& second);
  friend bool operator<=(const SpaInfo& first, const SpaInfo& second);
  friend bool operator==(const SpaInfo& first, const SpaInfo& second);

  std::string GetStringTableEntry(XLanguage language, uint16_t string_id) const;

 private:
  // Base info. There should be comparator between different SpaInfos and entry
  // with newer data should replace old one. Such situation can happen when game
  // adds achievements and so on with DLC.
  TitleHeaderData title_header_;

  // SPA is Read-Only so it's reasonable to make it readonly.
  std::vector<const AchievementTableEntry*> achievements_;
  std::vector<const XdbfContextTableEntry*> contexts_;
  std::vector<const XdbfPropertyTableEntry*> properties_;
  std::vector<ViewTable> stats_views_;
  PresenceTableEntry presence_;
  AvatarAwardsTableEntry avatar_awards_;
  PropertyBag matchmaking_;

  using XdbfLanguageStrings = std::map<uint16_t, std::string>;

  std::map<XLanguage, XdbfLanguageStrings> language_strings_;

  void LoadTitleInformation();
  void LoadAchievements();

  void LoadLanguageData();

  void LoadContexts();
  void LoadProperties();

  void LoadStatsViews();
  void LoadPresenceModes();
  void LoadAvatarItems();
  void LoadMatchmaking();

  template <typename T>
  static T GetSpaEntry(std::vector<T>& container, uint32_t id);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XDBF_SPA_INFO_H_
