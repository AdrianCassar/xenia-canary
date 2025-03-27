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

  auto achievement = GetAchievementInfoInternal(xuid, title_id, achievement_id);
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
  achievement->unlock_time = unlock_time;

  SaveAchievementData(xuid, title_id, achievement_id);
}

AchievementGpdStructure* HttpAchievementBackend::GetAchievementInfoInternal(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return nullptr;
  }

  return user->GetAchievement(title_id, achievement_id);
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

const AchievementGpdStructure* HttpAchievementBackend::GetAchievementInfo(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  return GetAchievementInfoInternal(xuid, title_id, achievement_id);
}

bool HttpAchievementBackend::IsAchievementUnlocked(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto achievement =
      GetAchievementInfoInternal(xuid, title_id, achievement_id);

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

  return achievements.HasMember(key.c_str());
}

const std::vector<AchievementGpdStructure>*
HttpAchievementBackend::GetTitleAchievements(const uint64_t xuid,
                                             const uint32_t title_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  return user->GetTitleAchievements(title_id);
}

bool HttpAchievementBackend::LoadAchievementsData(
    const uint64_t xuid, const util::XdbfGameData title_data) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return false;
  }

  // Question. Should loading for GPD for profile be directly done by profile or
  // here?
  if (!title_data.is_valid()) {
    return false;
  }

  const auto achievements = title_data.GetAchievements();
  if (achievements.empty()) {
    return true;
  }

  const auto title_id = title_data.GetTitleInformation().title_id;

  const XLanguage title_language = title_data.GetExistingLanguage(
      static_cast<XLanguage>(cvars::user_language));
  // user->achievements_[title_id][1].flags
  //  TODO: I should load achievements from the backend here.
  std::string host =
      fmt::format("{}/{:016X}/{:08X}", "api/achievements", user->GetLogonXUID(),
                  static_cast<uint32_t>(title_id));
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

  for (const auto& achievement : achievements) {
    AchievementGpdStructure achievementData(title_language, title_data,
                                            achievement);
    std::string key = std::to_string(achievementData.achievement_id);
    if (userAchievements.HasMember(key.c_str())) {
      if (userAchievements[key.c_str()]["revoked"].GetInt() != 1) {
        achievementData.flags =
            achievement.flags |
            static_cast<uint32_t>(AchievementFlags::kAchieved) |
            static_cast<uint32_t>(AchievementFlags::kAchievedOnline);
        achievementData.unlock_time =
            X_ACHIEVEMENT_UNLOCK_TIME(static_cast<time_t>(
                userAchievements[key.c_str()]["unlocked_at"].GetInt()));
      }
    }

    user->achievements_[title_id].push_back(achievementData);
  }

  return true;
}

bool HttpAchievementBackend::SaveAchievementData(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) {
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
  // When GPD is availabe achievements will be saved to profile as fail safe.
  return false;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
