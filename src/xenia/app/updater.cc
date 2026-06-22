/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"

// clang-format off
// We want to include platform.h first to define NOMINMAX to prevent window.h
// from defining the macros.
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
// clang-format on

#include "version.h"
#include "xenia/app/updater.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace app {
namespace {

constexpr uint32_t kRequestError = std::numeric_limits<uint32_t>::max();

inline bool SetCurlOptions(CURL*) { return true; }

template <typename Value, typename... Options>
bool SetCurlOptions(CURL* curl, CURLoption option, Value value,
                    Options... options) {
  return curl_easy_setopt(curl, option, value) == CURLE_OK &&
         SetCurlOptions(curl, options...);
}

size_t WriteResponseToFileCallback(void* contents, size_t size, size_t nmemb,
                                   void* userp) {
  if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) {
    return 0;
  }

  const size_t total_size = size * nmemb;
  if (total_size >
      static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
    return 0;
  }

  auto* file = static_cast<std::ofstream*>(userp);
  file->write(static_cast<const char*>(contents),
              static_cast<std::streamsize>(total_size));
  return *file ? total_size : 0;
}

}  // namespace

// TODO:
// - SSL backend for libcurl on Linux using wolfssl
Updater::Updater(const std::string& owner, const std::string& repo)
    : owner_(owner), repo_(repo) {}

uint32_t Updater::GetRequest(const std::string& endpoint,
                             std::vector<uint8_t>& response_buffer,
                             std::atomic<bool>& cancel_flag) const {
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                           curl_easy_cleanup);

  if (!curl) {
    return kRequestError;
  }

  ProgressCallbackData callback_data = {.cancelled = &cancel_flag};

  const bool options_set = SetCurlOptions(
      curl.get(), CURLOPT_URL, endpoint.c_str(), CURLOPT_USERAGENT,
      "xenia-canary", CURLOPT_WRITEFUNCTION, WriteResponceToMemoryCallback,
      CURLOPT_WRITEDATA, &response_buffer, CURLOPT_FOLLOWLOCATION, 1L,
      CURLOPT_NOPROGRESS, 0L, CURLOPT_XFERINFODATA, &callback_data,
      CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);

  if (!options_set) {
    return kRequestError;
  }

  const CURLcode result = curl_easy_perform(curl.get());

  if (result == CURLE_ABORTED_BY_CALLBACK) {
    XELOGI("Cancelled Request!");
  }

  long response_code = 0;
  if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code) !=
          CURLE_OK ||
      result != CURLE_OK) {
    return kRequestError;
  }

  return static_cast<uint32_t>(response_code);
}

// Using this function will reduce the chances of API rate limits from GitHub.
// Only supports default branch and release builds.
CheckForUpdateInfo Updater::CheckForUpdatesViaXeniaManagerDatabase(
    bool stable, std::atomic<bool>& cancel_flag) const {
  const std::string endpoint =
      "https://xenia-manager.github.io/database/data/version.json";

  CheckForUpdateInfo update_info = {};

  // Perform HTTP GET
  std::vector<uint8_t> response_buffer;
  const uint32_t result = GetRequest(endpoint, response_buffer, cancel_flag);

  update_info.metadata.response_code = result;

  if (result != HTTP_STATUS_CODE::HTTP_OK) {
    XELOGI("endpoint request failed with HTTP {}", result);
    return update_info;
  }

  // Parse JSON
  const std::string response_data(response_buffer.cbegin(),
                                  response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError() || !document.IsObject()) {
    XELOGI("JSON parse error or invalid root");
    update_info.metadata.response_code = -1;
    return update_info;
  }

  // Navigate JSON
  auto get_object = [&](const rapidjson::Value& parent,
                        const char* name) -> const rapidjson::Value* {
    return (parent.HasMember(name) && parent[name].IsObject()) ? &parent[name]
                                                               : nullptr;
  };

  const char* build_type = stable ? "stable" : "nightly";

  const rapidjson::Value* xenia = get_object(document, "xenia");
  const rapidjson::Value* netplay =
      xenia ? get_object(*xenia, "netplay") : nullptr;
  const rapidjson::Value* build =
      netplay ? get_object(*netplay, build_type) : nullptr;

  if (!build) {
    XELOGI("missing 'xenia.netplay.build' object");
    update_info.metadata.response_code = -1;
    return update_info;
  }

  if (!build->HasMember("commit_sha") || !(*build)["commit_sha"].IsString() ||
      !build->HasMember("tag_name") || !(*build)["tag_name"].IsString()) {
    XELOGI("missing or invalid build commit/tag metadata");
    update_info.metadata.response_code = kRequestError;
    return update_info;
  }

  const std::string latest_commit = (*build)["commit_sha"].GetString();
  const std::string tag_name = (*build)["tag_name"].GetString();
  const std::string latest_date =
      (build->HasMember("date") && (*build)["date"].IsString())
          ? FormatDate((*build)["date"].GetString())
          : "";

  update_info.metadata.commit_hash = latest_commit;
  update_info.metadata.commit_date = latest_date;
  update_info.metadata.tag = tag_name;
  update_info.update_available = latest_commit != XE_BUILD_COMMIT;

  if (update_info.update_available) {
    XELOGI("Update Available!");
  } else {
    XELOGI("Build is up to date!");
  }

  if (!latest_commit.empty()) {
    XELOGI("Updater: Current={}, Latest={}, Date={}", XE_BUILD_COMMIT_SHORT,
           latest_commit.substr(0, 10), latest_date);
  }

  return update_info;
}

std::future<CheckForUpdateInfo> Updater::StartupUpdateCheckAsync(
    std::atomic<bool>& cancel_flag,
    std::function<void(CheckForUpdateInfo)> callback) const {
  auto checking_for_updates =
      std::async(std::launch::async, &Updater::StartupUpdateCheck, this,
                 std::ref(cancel_flag), callback);

  return checking_for_updates;
}

CheckForUpdateInfo Updater::StartupUpdateCheck(
    std::atomic<bool>& cancel_flag,
    std::function<void(CheckForUpdateInfo)> callback) const {
  CheckForUpdateInfo update_info =
      CheckForUpdatesViaXeniaManagerDatabase(false, cancel_flag);

  if (update_info.metadata.response_code != HTTP_STATUS_CODE::HTTP_OK) {
    update_info = CheckForUpdates(false, XE_BUILD_BRANCH, cancel_flag);
  }

  if (callback) {
    callback(update_info);
  }

  return update_info;
}

std::future<CheckForUpdateInfo> Updater::CheckForUpdatesAsync(
    bool stable, const std::string& branch,
    std::atomic<bool>& cancel_flag) const {
  auto checking_for_updates =
      std::async(std::launch::async, &Updater::CheckForUpdates, this, stable,
                 branch, std::ref(cancel_flag));

  return checking_for_updates;
}

CheckForUpdateInfo Updater::CheckForUpdates(
    bool stable, const std::string& branch,
    std::atomic<bool>& cancel_flag) const {
  CheckForUpdateInfo update_info = {};
  ChangelogInfo changelog_info = {};

  bool update_available = false;

  if (stable) {
    update_info.metadata = GetLatestReleaseCommitHash(cancel_flag);
  } else {
    update_info.metadata = GetLatestCommitHash(branch, cancel_flag);
  }

  if (update_info.metadata.response_code != HTTP_STATUS_CODE::HTTP_OK) {
    update_info.update_available = false;
    return update_info;
  }

  if (stable) {
    // Either get commit hash from tag or compare commits to get state
    changelog_info = GetChangelogBetweenCommits(
        XE_BUILD_COMMIT, update_info.metadata.tag, cancel_flag);

    if (changelog_info.response_code != HTTP_STATUS_CODE::HTTP_OK) {
      update_info.metadata.response_code = changelog_info.response_code;
      update_info.update_available = false;
      return update_info;
    }

    update_available = changelog_info.messages.status != "identical";
  } else {
    update_available = update_info.metadata.commit_hash != XE_BUILD_COMMIT;
  }

  update_info.update_available = update_available;

  return update_info;
}

#ifdef XE_PLATFORM_WIN32
std::wstring Updater::RunPowershellCommand(const std::string& command) const {
  std::wstring result;
  std::string ps_command =
      fmt::format("powershell -NoProfile -Command \"{}\"", command);

  std::wstring ps_commandw = std::wstring(ps_command.begin(), ps_command.end());

  FILE* pipe = _wpopen(ps_commandw.c_str(), L"rt, ccs=UNICODE");
  if (!pipe) {
    return result;
  }

  wchar_t buffer[512];  // Could be set to smaller value
  while (fgetws(buffer, sizeof(buffer) / sizeof(wchar_t), pipe)) {
    result += buffer;
  }
  _pclose(pipe);
  return result;
}

bool Updater::IsAnotherInstanceRunning() const {
  const auto executable_path = xe::filesystem::GetExecutablePath();
  const auto executable_filename =
      executable_path.filename().replace_extension("").string();

  // Check if the same executable is running in another instance
  std::string ps_command = fmt::format(
      "Get-Process {} | Where-Object Path -EQ {} | Select-Object "
      "-ExpandProperty Id",
      executable_filename, executable_path);

  std::wstring output = RunPowershellCommand(ps_command);

  uint32_t current_pid = GetCurrentProcessId();

  std::wistringstream ss(output);
  std::wstring line;
  uint32_t instance_count = 0;

  while (ss >> line) {
    try {
      uint32_t pid = std::stoul(line);

      if (pid != current_pid) {
        instance_count++;
      }
    } catch (...) {
      // Ignoring non id lines (if they appear)
    }
  }

  return instance_count > 0;
}
#endif

UpdateMetadata Updater::GetLatestCommitHash(
    const std::string& branch, std::atomic<bool>& cancel_flag) const {
  UpdateMetadata update_metadata = {};

  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/commits?sha={}&per_page=1", owner_,
      repo_, branch);

  uint32_t response_code = GetRequest(endpoint, response_buffer, cancel_flag);

  update_metadata.response_code = response_code;

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return update_metadata;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError()) {
    update_metadata.response_code = kRequestError;
    return update_metadata;
  }

  if (!document.IsArray() || document.Empty()) {
    update_metadata.response_code = kRequestError;
    return update_metadata;
  }

  const auto& commit = document[0];

  if (!commit.IsObject() || !commit.HasMember("sha") ||
      !commit["sha"].IsString()) {
    update_metadata.response_code = kRequestError;
    return update_metadata;
  }

  std::string commit_date_ = "Unknown";

  if (commit.HasMember("commit") && commit["commit"].IsObject()) {
    const auto& commit_details = commit["commit"];

    if (commit_details.HasMember("committer") &&
        commit_details["committer"].IsObject() &&
        commit_details["committer"].HasMember("date") &&
        commit_details["committer"]["date"].IsString()) {
      commit_date_ = commit_details["committer"]["date"].GetString();
    }
  }

  update_metadata.commit_hash = commit["sha"].GetString();
  update_metadata.commit_date = FormatDate(commit_date_).c_str();
  update_metadata.response_code = response_code;

  return update_metadata;
}

UpdateMetadata Updater::GetLatestReleaseCommitHash(
    std::atomic<bool>& cancel_flag) const {
  UpdateMetadata update_metadata = {};

  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/releases/latest", owner_, repo_);

  uint32_t response_code = GetRequest(endpoint, response_buffer, cancel_flag);

  update_metadata.response_code = response_code;

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return update_metadata;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError() || !document.IsObject()) {
    update_metadata.response_code = kRequestError;
    return update_metadata;
  }

  if (!document.HasMember("tag_name") || !document["tag_name"].IsString()) {
    update_metadata.response_code = kRequestError;
    return update_metadata;
  }

  update_metadata.tag = document["tag_name"].GetString();

  if (document.HasMember("published_at") &&
      document["published_at"].IsString()) {
    update_metadata.published_date =
        FormatDate(document["published_at"].GetString()).c_str();
  }

  if (document.HasMember("target_commitish") &&
      document["target_commitish"].IsString()) {
    update_metadata.commit_hash = document["target_commitish"].GetString();
  }

  return update_metadata;
}

std::string Updater::FormatDate(const std::string& iso_date) const {
  std::istringstream ss(iso_date);
  std::tm time = {};

  ss >> std::get_time(&time, "%Y-%m-%dT%H:%M:%SZ");

  if (ss.fail()) {
    return "";
  }

  return fmt::format("{:%b %d, %Y}", time);
}

std::future<uint32_t> Updater::DownloadLatestNightlyArtifactAsync(
    const std::string& workflow_file, const std::string& branch,
    const std::string& artifact_name, const std::string& output_path,
    std::atomic<bool>& cancel_flag,
    std::function<void(double, double)> progress_callback) {
  auto download_nightly =
      std::async(std::launch::async, &Updater::DownloadLatestNightlyArtifact,
                 this, workflow_file, branch, artifact_name, output_path,
                 std::ref(cancel_flag), progress_callback);

  return download_nightly;
}

uint32_t Updater::DownloadLatestNightlyArtifact(
    const std::string& workflow_file, const std::string& branch,
    const std::string& artifact_name, const std::string& output_path,
    std::atomic<bool>& cancel_flag,
    std::function<void(double, double)> progress_callback) const {
  const std::string endpoint =
      fmt::format("https://nightly.link/{}/{}/workflows/{}/{}/{}", owner_,
                  repo_, workflow_file, branch, artifact_name);

  return DownloadFile(endpoint, output_path, cancel_flag, progress_callback);
}

std::future<uint32_t> Updater::DownloadLatestReleaseAsync(
    const std::string& asset_name, const std::string& output_path,
    std::atomic<bool>& cancel_flag,
    std::function<void(double, double)> progress_callback) {
  auto download_release = std::async(
      std::launch::async, &Updater::DownloadLatestRelease, this, asset_name,
      output_path, std::ref(cancel_flag), progress_callback);

  return download_release;
}

uint32_t Updater::DownloadLatestRelease(
    const std::string& asset_name, const std::string& output_path,
    std::atomic<bool>& cancel_flag,
    std::function<void(double, double)> progress_callback) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/releases/latest", owner_, repo_);

  uint32_t response_code = GetRequest(endpoint, response_buffer, cancel_flag);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError() || !document.IsObject()) {
    return kRequestError;
  }

  if (!document.HasMember("assets") || !document["assets"].IsArray()) {
    XELOGE("Invalid JSON or no assets.", output_path);
    return kRequestError;
  }

  std::string asset_url;

  for (const auto& asset : document["assets"].GetArray()) {
    if (!asset.IsObject()) {
      continue;
    }

    if (asset.HasMember("name") && asset["name"].IsString() &&
        asset["name"].GetString() == asset_name &&
        asset.HasMember("browser_download_url") &&
        asset["browser_download_url"].IsString()) {
      asset_url = asset["browser_download_url"].GetString();
      break;
    }
  }

  if (asset_url.empty()) {
    XELOGE("Asset '{}' not found in latest release.", asset_name);
    return kRequestError;
  }

  return DownloadFile(asset_url, output_path, cancel_flag, progress_callback);
}

uint32_t Updater::DownloadFile(
    const std::string& file_endpoint, const std::string& output_path,
    std::atomic<bool>& cancel_flag,
    std::function<void(double, double)> progress_callback) const {
  std::filesystem::path temporary_path(output_path);
  temporary_path += ".part";
  std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);

  if (!file) {
    return kRequestError;
  }

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                           curl_easy_cleanup);

  if (!curl) {
    file.close();
    std::error_code ec;
    std::filesystem::remove(temporary_path, ec);
    return kRequestError;
  }

  ProgressCallbackData callback_data = {.progress_callback = progress_callback,
                                        .cancelled = &cancel_flag};

  const bool options_set = SetCurlOptions(
      curl.get(), CURLOPT_URL, file_endpoint.c_str(), CURLOPT_USERAGENT,
      "xenia-canary", CURLOPT_FOLLOWLOCATION, 1L, CURLOPT_WRITEFUNCTION,
      WriteResponseToFileCallback, CURLOPT_WRITEDATA, &file, CURLOPT_NOPROGRESS,
      0L, CURLOPT_XFERINFODATA, &callback_data, CURLOPT_XFERINFOFUNCTION,
      CurlProgressCallback, CURLOPT_LOW_SPEED_LIMIT, 1024L,
      CURLOPT_LOW_SPEED_TIME, 30L);

  if (!options_set) {
    file.close();
    std::error_code ec;
    std::filesystem::remove(temporary_path, ec);
    return kRequestError;
  }

  const CURLcode result = curl_easy_perform(curl.get());

  long response_code = 0;
  const CURLcode info_result =
      curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);

  if (result == CURLE_ABORTED_BY_CALLBACK) {
    XELOGI("Download cancelled!");
  }

  file.flush();
  const bool file_ok = file.good();
  file.close();

  if (result != CURLE_OK || info_result != CURLE_OK || !file_ok ||
      response_code != HTTP_STATUS_CODE::HTTP_OK) {
    std::error_code ec;
    std::filesystem::remove(temporary_path, ec);

    if (result != CURLE_OK || info_result != CURLE_OK || !file_ok) {
      return kRequestError;
    }

    return static_cast<uint32_t>(response_code);
  }

  std::error_code ec;
  std::filesystem::remove(output_path, ec);

  if (ec) {
    std::filesystem::remove(temporary_path, ec);
    return kRequestError;
  }

  std::filesystem::rename(temporary_path, output_path, ec);

  if (ec) {
    std::filesystem::remove(temporary_path, ec);
    return kRequestError;
  }

  return static_cast<uint32_t>(response_code);
}

ChangelogInfo Updater::GetRecentCommitMessages(const std::string& branch,
                                               std::atomic<bool>& cancel_flag,
                                               uint32_t count) const {
  ChangelogInfo changelog_info = {};
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/commits?sha={}&per_page={}", owner_,
      repo_, branch, count);

  uint32_t response_code = GetRequest(endpoint, response_buffer, cancel_flag);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    changelog_info.response_code = response_code;
    return changelog_info;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  std::string json_wrapper =
      fmt::format(R"({{"commits": {}}})", response_data.c_str());

  response_buffer.resize(json_wrapper.size());

  std::memcpy(response_buffer.data(), json_wrapper.c_str(),
              json_wrapper.size());

  CommitMessages commit_messages = ParseCommitMessages(response_buffer);

  if (!commit_messages.success) {
    changelog_info.response_code = kRequestError;
    return changelog_info;
  }

  std::reverse(commit_messages.messages.begin(),
               commit_messages.messages.end());

  changelog_info.messages = commit_messages;
  changelog_info.response_code = response_code;

  return changelog_info;
}

std::future<ChangelogInfo> Updater::GetChangelogBetweenCommitsAsync(
    const std::string& base_commit, const std::string& head_commit,
    std::atomic<bool>& cancel_flag) const {
  auto changelog =
      std::async(std::launch::async, &Updater::GetChangelogBetweenCommits, this,
                 base_commit, head_commit, std::ref(cancel_flag));

  return changelog;
}

ChangelogInfo Updater::GetChangelogBetweenCommits(
    const std::string& base_commit, const std::string& head_commit,
    std::atomic<bool>& cancel_flag) const {
  ChangelogInfo changelog_info = {};
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint =
      fmt::format("https://api.github.com/repos/{}/{}/compare/{}...{}", owner_,
                  repo_, base_commit, head_commit);

  uint32_t response_code = GetRequest(endpoint, response_buffer, cancel_flag);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    changelog_info.response_code = response_code;
    return changelog_info;
  }

  // Max 250 commits returned by compare API
  CommitMessages commit_messages = ParseCommitMessages(response_buffer);

  if (!commit_messages.success) {
    changelog_info.response_code = kRequestError;
    return changelog_info;
  }

  std::reverse(commit_messages.messages.begin(),
               commit_messages.messages.end());

  changelog_info.messages = commit_messages;
  changelog_info.response_code = response_code;

  return changelog_info;
}

CommitMessages Updater::ParseCommitMessages(
    const std::vector<uint8_t>& response_buffer) const {
  CommitMessages data = {};

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError() || !document.IsObject()) {
    data.success = false;
    return data;
  }

  if (!document.IsObject() || !document.HasMember("commits")) {
    XELOGE("Invalid JSON or no commits array to parse.");
    data.success = false;
    return data;
  }

  const auto& commits = document["commits"];

  if (document.HasMember("status") && document["status"].IsString()) {
    data.status = document["status"].GetString();
  }

  if (!commits.IsArray()) {
    data.success = false;
    return data;
  }

  for (const auto& commit : commits.GetArray()) {
    if (commit.IsObject() && commit.HasMember("commit") &&
        commit["commit"].IsObject() && commit["commit"].HasMember("message") &&
        commit["commit"]["message"].IsString()) {
      std::string msg = commit["commit"]["message"].GetString();
      data.messages.push_back(msg);
    }
  }

  data.success = true;

  return data;
}

bool Updater::UpdateAndRestart(const std::filesystem::path& zip_path) {
  std::error_code ec;

  if (zip_path.empty()) {
    return false;
  }

  if (!std::filesystem::exists(zip_path, ec) || ec) {
    return false;
  }

  const auto executable_path = xe::filesystem::GetExecutablePath();
  const auto executable_filename = executable_path.filename().string();
  const auto executable_parent = xe::filesystem::GetExecutableFolder();

  std::string update_script_filename = "";

#ifdef XE_PLATFORM_WIN32
  update_script_filename = "updater.bat";
#elif XE_PLATFORM_LINUX
  update_script_filename = "updater.sh";
#endif

  const auto update_script_path = executable_parent / update_script_filename;
  const auto zip_filename = zip_path.filename().string();

  const auto update_log_filename = "xenia_canary_update.log";
  const auto backup_folder_name = "canary_netplay_old";

  if (std::filesystem::exists(update_script_path, ec) && !ec) {
    std::filesystem::remove(update_script_path, ec);
  }

  if (ec) {
    return false;
  }

  std::string script_content = "";

// Scripts for completing the automatic update process.
#ifdef XE_PLATFORM_WIN32
  const DWORD current_process_id = GetCurrentProcessId();

  script_content = fmt::format(
      "@echo off\n"
      "set LOG_FILE=\"{1}\\{5}\"\n"
      "echo [INF] Starting Xenia update script > %LOG_FILE%\n"
      "echo [INF] Changed to extract directory >> %LOG_FILE%\n"
      "cd \"{1}\"\n"
      "echo [INF] Waiting for Xenia process to exit >> %LOG_FILE%\n"
      "set ps_stop_instances=powershell -ExecutionPolicy RemoteSigned -Command "
      "\"Get-Process (Get-Item '{2}').Basename | Where-Object Path -eq '{2}' | "
      "ForEach-Object {{ if ($_.Id -eq {7}) {{ Wait-Process -id {7} }} else {{ "
      "Stop-Process -id $_.Id; Wait-Process -id $_.Id }}}}\"\n"
      "call %ps_stop_instances%\n"
      "echo [INF] Xenia process has exited >> %LOG_FILE%\n"
      "echo [INF] Cleaning and creating backup folder >> %LOG_FILE%\n"
      "if exist \"{1}\\{6}\" rd /s /q \"{1}\\{6}\"\n"
      "mkdir     \"{1}\\{6}\"\n"
      "echo [INF] Backing up old executable: {2} >> %LOG_FILE%\n"
      "copy /y \"{2}\" \"{1}\\{6}\\{0}\" >> %LOG_FILE% 2>&1\n"
      "echo [INF] Extracting Xenia update... >> %LOG_FILE%\n"
      "echo [INF] Attempting tar extraction of {3} >> %LOG_FILE%\n"
      "tar -xf {3} >> %LOG_FILE% 2>&1\n"
      "if errorlevel 1 (\n"
      "  echo [WRN] tar extraction failed, trying PowerShell >> "
      "%LOG_FILE%\n"
      "  powershell -ExecutionPolicy RemoteSigned -Command "
      "\"$ErrorActionPreference "
      "= 'Stop'; try {{ if (-not (Get-Command Expand-Archive -ErrorAction "
      "SilentlyContinue)) {{ throw 'Expand-Archive not available' }}; "
      "Expand-Archive -Path '{4}' -DestinationPath '{1}' -Force -ErrorAction "
      "Stop; if ($Error.Count -gt 0) {{ exit 1 }}; exit 0 }} catch {{ "
      "Write-Host 'PowerShell extraction failed:' $_.Exception.Message; exit 1 "
      "}}\" >> %LOG_FILE% 2>&1\n"
      "  if errorlevel 1 (\n"
      "    echo [ERR] Both tar and PowerShell extraction "
      "failed >> %LOG_FILE%\n"
      "    echo [INF] Relaunching Xenia with update failed flag >> "
      "%LOG_FILE%\n"
      "    start \"\" \"{2}\" --updated=false\n"
      "    del \"%~f0\"\n"
      "    exit /b 1\n"
      "  ) else (\n"
      "    echo [INF] PowerShell extraction succeeded >> %LOG_FILE%\n"
      "  )\n"
      ") else (\n"
      "  echo [INF] tar extraction succeeded >> %LOG_FILE%\n"
      ")\n"
      "echo [INF] Starting updated Xenia executable >> %LOG_FILE%\n"
      "start \"\" \"{2}\" --updated=true\n"
      "echo [INF] Deleting zip file: {4} >> %LOG_FILE%\n"
      "del \"{4}\" >> %LOG_FILE% 2>&1\n"
      "echo [INF] Update script completed successfully >> "
      "%LOG_FILE%\n"
      "del \"%~f0\"\n",
      executable_filename,  // {0}
      executable_parent,    // {1}
      executable_path,      // {2}
      zip_filename,         // {3}
      zip_path,             // {4}
      update_log_filename,  // {5}
      backup_folder_name,   // {6}
      current_process_id    // {7}
  );
#elif XE_PLATFORM_LINUX
  script_content = fmt::format(
      "#!/bin/bash\n"
      "\n"
      "EXECUTABLE_NAME=\"{0}\"                   \t# final executable name\n"
      "EXECUTABLE_PATH=\"$(dirname \"$(realpath \"$0\")\")/$EXECUTABLE_NAME\"\n"
      "ARCHIVE_FILE=\"{1}\"          \t\t\t# archive file\n"
      "LOG_FILE=\"$(dirname \"$(realpath \"$0\")\")/{2}\"\n"
      "BACKUP_DIR=\"$(dirname \"$(realpath \"$0\")\")/{3}\"\n"
      "\n"
      "echo \"[INF] Starting Xenia update script\" > \"$LOG_FILE\"\n"
      "\n"
      "cd \"$(dirname \"$(realpath \"$0\")\")\" || exit 1\n"
      "\n"
      "# Check if unzip is installed before doing anything else\n"
      "if ! command -v unzip &> /dev/null; then\n"
      "    echo \"[ERR] unzip is not installed\"\n"
      "    echo \"[INF] Relaunching Xenia with update failed flag\" >> "
      "\"$LOG_FILE\"\n"
      "    \"$EXECUTABLE_PATH\" --updated=false &\n"
      "    rm -- \"$0\"\n"
      "    exit 1\n"
      "fi\n"
      "\n"
      "# Extract only the new executable directly from unzip\n"
      "echo \"[INF] Extracting executable from archive: $ARCHIVE_FILE\" >> "
      "\"$LOG_FILE\"\n"
      "if ! unzip \"$ARCHIVE_FILE\" >> \"$LOG_FILE\" 2>&1; then\n"
      "  echo \"[INF] Relaunching Xenia with update failed flag\" >> "
      "\"$LOG_FILE\"\n"
      "  \"$EXECUTABLE_PATH\" --updated=false &\n"
      "  rm -- \"$0\"\n"
      "  exit 1\n"
      "fi\n"
      "\n"
      "echo \"[INF] Cleaning and creating backup folder\" >> \"$LOG_FILE\"\n"
      "rm -rf \"$BACKUP_DIR\"\n"
      "mkdir -p \"$BACKUP_DIR\"\n"
      "\n"
      "echo \"[INF] Backing up old executable\" >> \"$LOG_FILE\"\n"
      "if [ -f \"$EXECUTABLE_PATH\" ]; then\n"
      "  cp \"$EXECUTABLE_PATH\" \"$BACKUP_DIR/$EXECUTABLE_NAME\" >> "
      "\"$LOG_FILE\" 2>&1\n"
      "fi\n"
      "\n"
      "# Start updated executable\n"
      "echo \"[INF] Starting updated Xenia executable\" >> \"$LOG_FILE\"\n"
      "\"$EXECUTABLE_PATH\" --updated=true &\n"
      "\n"
      "# Remove archive and self-delete\n"
      "echo \"[INF] Deleting archive file: $ARCHIVE_FILE\" >> \"$LOG_FILE\"\n"
      "rm -f \"$ARCHIVE_FILE\" >> \"$LOG_FILE\" 2>&1\n"
      "\n"
      "echo \"[INF] Update script completed successfully\" >> \"$LOG_FILE\"\n"
      "rm -- \"$0\"",
      executable_filename, zip_filename, update_log_filename,
      backup_folder_name);
#endif

  std::ofstream update_script_file(update_script_path);

  if (!update_script_file.is_open()) {
    return false;
  }

  update_script_file << script_content;

  if (update_script_file.fail()) {
    update_script_file.close();
    return false;
  }

  update_script_file.close();

#ifdef XE_PLATFORM_WIN32
  SHELLEXECUTEINFO ShExecInfo = {};

  const std::wstring update_script_path_wstr_ = update_script_path.wstring();
  const wchar_t* update_script_path_wstr_ptr = update_script_path_wstr_.c_str();

  ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
  ShExecInfo.lpFile = update_script_path_wstr_ptr;
  ShExecInfo.nShow = SW_HIDE;

  return ShellExecuteEx(&ShExecInfo);
#elif XE_PLATFORM_LINUX
  std::filesystem::permissions(update_script_filename,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add);

  std::string exec = fmt::format("./{}", update_script_filename);
  // Doesn't return
  execlp("/bin/bash", "/bin/bash", "-c", exec.c_str(), nullptr);
  return false;
#endif
}

}  // namespace app
}  // namespace xe
