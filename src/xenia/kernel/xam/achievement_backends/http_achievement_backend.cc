/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/achievement_backends/http_achievement_backend.h"
#include <third_party/libcurl/include/curl/curl.h>
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"

DECLARE_int32(user_language);

DEFINE_string(
    default_achievements_backend_url, "https://account.xboxpreservation.org",
    "Defines which api url achievements backend should be used as an default. ",
    "Kernel");

namespace xe {
namespace kernel {
namespace xam {

HttpAchievementBackend::HttpAchievementBackend() {}
HttpAchievementBackend::~HttpAchievementBackend() {}

void HttpAchievementBackend::EarnAchievement(const uint64_t xuid,
                                             const uint32_t title_id,
                                             const uint32_t achievement_id) {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  auto achievement = GetAchievementInfo(xuid, title_id, achievement_id);
  if (!achievement) {
    return;
  }

  XELOGI("Player: {} Unlocked Achievement: {}", user->name(),
         xe::to_utf8(xe::load_and_swap<std::u16string>(
             achievement->achievement_name.c_str())));

  const uint64_t unlock_time = Clock::QueryHostSystemTime();
  // We're adding achieved online flag because on console locally achieved
  // entries don't have valid unlock time.
  achievement->flags = achievement->flags |
                       static_cast<uint32_t>(AchievementFlags::kAchieved) |
                       static_cast<uint32_t>(AchievementFlags::kAchievedOnline);
  // Prevent overwriting original unlock time
  if (!achievement->unlock_time.is_valid())
    achievement->unlock_time = unlock_time;

  kernel_state()->xam_state()->user_tracker()->UnlockAchievement(
      xuid, achievement_id);
  SaveAchievementData(xuid, title_id, &achievement.value());
}

std::string HttpAchievementBackend::SendRequest(
    const std::string host, const std::string option) const {
  XELOGI("url: {}", host);
  CURLoption type;
  if (option == "POST") {
    type = CURLOPT_HTTPPOST;
  } else {
    type = CURLOPT_HTTPGET;
  }

  if (host.empty()) return "";
  std::string url =
      fmt::format("{}/{}", cvars::default_achievements_backend_url, host);
  std::string agent = "Xenia";
  CURL* curl = curl_easy_init();

  if (!curl) {
    XELOGI("[HTTP Backend] failed to init curl!");
    return "";
  }

  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(curl, type, 5L);
  if (option == "POST") {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
  }

  curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
  curl_easy_setopt(
      curl, CURLOPT_WRITEFUNCTION,
      +[](char* ptr, size_t size, size_t nmemb,
          std::string* userdata) -> size_t {
        if (userdata) {
          userdata->append(ptr, size * nmemb);
        }
        return size * nmemb;
      });
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 5L);

  CURLcode result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (result != CURLE_OK) {
    return "";
  }

  return response_body;
}

const std::optional<Achievement> HttpAchievementBackend::GetAchievementInfo(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return std::nullopt;
  }

  auto entry = user->games_gpd_.find(title_id);
  if (entry == user->games_gpd_.cend()) {
    return std::nullopt;
  }

  const auto achievement_entry =
      entry->second.GetAchievementEntry(achievement_id);

  if (!achievement_entry) {
    return std::nullopt;
  }

  Achievement achievement(achievement_entry);
  achievement.achievement_name =
      entry->second.GetAchievementTitle(achievement_id);
  achievement.unlocked_description =
      entry->second.GetAchievementDescription(achievement_id);
  achievement.locked_description =
      entry->second.GetAchievementUnachievedDescription(achievement_id);

  return achievement;
}

bool HttpAchievementBackend::IsAchievementUnlocked(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto achievement = GetAchievementInfo(xuid, title_id, achievement_id);

  if (!achievement) {
    return false;
  }
  // We get the user via offline xuid to get the online xuid.
  // Why? because you can't lookup a user via online xuid...
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  std::string host = fmt::format("{}/{:016X}/{:08X}", "api/achievements",
                                 user->GetLogonXUID(), title_id);

  std::string response_body = SendRequest(host, "GET");

  rapidjson::Document doc;
  if (doc.Parse(response_body.c_str()).HasParseError()) {
    XELOGI("failed to parse JSON! {}", response_body.c_str());
    return false;
  }

  if (!doc.HasMember("status") || doc["status"].GetInt() != 200 ||
      !doc.HasMember("message") || !doc["message"].IsObject() ||
      !doc["message"].HasMember("achievements")) {
    XELOGI("[HTTP Backend] No account found or malformed backend!");
    return true;
  }

  const rapidjson::Value& message = doc["message"];
  const rapidjson::Value& achievements = message["achievements"];
  std::string key = std::to_string(achievement_id);

  bool unlocked_offline =
      (achievement->flags &
       static_cast<uint32_t>(AchievementFlags::kAchieved)) != 0;
  bool unlocked_online = achievements.HasMember(key.c_str());

  if (unlocked_offline && !unlocked_online ||
      unlocked_online && !unlocked_offline) {
    return false;
  }

  return unlocked_online;
}

const std::vector<Achievement> HttpAchievementBackend::GetTitleAchievements(
    const uint64_t xuid, const uint32_t title_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  return kernel_state()->xam_state()->user_tracker()->GetUserTitleAchievements(
      xuid, title_id);
}

const std::span<const uint8_t> HttpAchievementBackend::GetAchievementIcon(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  return kernel_state()->xam_state()->user_tracker()->GetAchievementIcon(
      xuid, title_id, achievement_id);
}

bool HttpAchievementBackend::LoadAchievementsData(const uint64_t xuid) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);

  if (!user) {
    return false;
  }

  // Get title ids
  std::string hostV2 =
      fmt::format("{}/{:016X}", "api/achievements", user->GetLogonXUID());

  std::string response_body = SendRequest(hostV2, "GET");
  rapidjson::Document doc;

  if (doc.Parse(response_body.c_str()).HasParseError()) {
    XELOGI("failed to parse JSON! {}", response_body.c_str());
    return true;
  }

  if (!doc.HasMember("status") || doc["status"].GetInt() != 200 ||
      !doc.HasMember("message") || !doc["message"].IsObject() ||
      !doc["message"].HasMember("achievements")) {
    XELOGI("[HTTP Backend] No account found or malformed backend!");
    return true;
  }
  const rapidjson::Value& title_ids = doc["message"];
  // end

  for (const auto& title_id_str : title_ids.GetArray()) {
    std::string title_id = title_id_str.GetString();  // if it's a string
    std::string host =
        fmt::format("{}/{:016X}/{:08X}", "api/achievements",
                    user->GetLogonXUID(), std::stoul(title_id, nullptr, 16));

    std::string response_body = SendRequest(host, "GET");
    rapidjson::Document doc;

    if (doc.Parse(response_body.c_str()).HasParseError()) {
      XELOGI("failed to parse JSON! {}", response_body.c_str());
      return true;
    }

    if (!doc.HasMember("status") || doc["status"].GetInt() != 200 ||
        !doc.HasMember("message") || !doc["message"].IsObject() ||
        !doc["message"].HasMember("achievements")) {
      XELOGI("[HTTP Backend] No account found or malformed backend!");
      return true;
    }
    const rapidjson::Value& message = doc["message"];
    const rapidjson::Value& userAchievements = message["achievements"];

    for (auto it = userAchievements.MemberBegin();
         it != userAchievements.MemberEnd(); ++it) {
      const std::string achievement_id = it->name.GetString();

      auto achievementData = GetAchievementInfo(
          xuid, static_cast<uint32_t>(std::stoul(title_id, nullptr, 16)),
          std::stoi(achievement_id));
      if (userAchievements.HasMember(achievement_id.c_str())) {
        if (userAchievements[achievement_id.c_str()]["revoked"].GetInt() != 1) {
          achievementData->flags =
              achievementData->flags |
              static_cast<uint32_t>(AchievementFlags::kAchieved) |
              static_cast<uint32_t>(AchievementFlags::kAchievedOnline);
          achievementData->unlock_time = X_FILETIME(static_cast<time_t>(
              userAchievements[achievement_id.c_str()]["unlocked_at"]
                  .GetInt()));
        }
      }
    }
  }
  return true;
}

bool HttpAchievementBackend::SaveAchievementData(
    const uint64_t xuid, const uint32_t title_id,
    const Achievement* achievement) {
  const uint32_t achievement_id = achievement->achievement_id;
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  std::string host =
      fmt::format("{}/{:016X}/{:08X}/{}", "api/achievements",
                  user->GetLogonXUID(), title_id, achievement_id);

  std::string response_body = SendRequest(host, "POST");

  rapidjson::Document doc;
  if (doc.Parse(response_body.c_str()).HasParseError()) {
    XELOGI("failed to parse JSON! {}", response_body.c_str());
    return false;
  }

  if (doc.HasMember("status") && doc["status"].GetInt() == 200) {
    return true;
  }
  if (doc.HasMember("status") && doc["status"].GetInt() == 404) {
    XELOGI("No account found on backend so achievements won't be saved!");
  }
  return false;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
