/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// math.h and curl.h conflict so we include it first
#include "xenia/kernel/xnet.h"

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/libcurl/include/curl/curl.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"

#include "build/version.h"
#include "xenia/app/updater.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {

Updater::Updater(const std::string& owner, const std::string& repo)
    : owner_(owner), repo_(repo) {}

uint32_t Updater::GetRequest(const std::string& endpoint,
                             std::vector<uint8_t>& response_buffer) const {
  CURL* curl;
  CURLcode result;

  curl = curl_easy_init();

  if (!curl) {
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia-canary");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponceToMemoryCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  if (result != CURLE_OK && response_code == 0) {
    response_code = -1;
  }

  return response_code;
}

bool Updater::CheckForUpdates(const std::string& branch,
                              std::string* commit_hash,
                              std::string* commit_date,
                              uint32_t* response_code) {
  const uint32_t result = GetLatestCommitHash(branch, commit_hash, commit_date);

  if (response_code) {
    *response_code = result;
  }

  if (result != HTTP_STATUS_CODE::HTTP_OK) {
    return false;
  }

  return *commit_hash != XE_BUILD_COMMIT;
}

uint32_t Updater::GetLatestCommitHash(const std::string& branch,
                                      std::string* commit_hash,
                                      std::string* commit_date) {
  if (!commit_hash) {
    return -1;
  }

  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/commits?sha={}&per_page=1", owner_,
      repo_, branch);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  rapidjson::Document document;
  document.Parse(reinterpret_cast<char*>(response_buffer.data()));

  if (!document.IsArray() || document.Empty()) {
    return -1;
  }

  const auto& commits = document.GetArray();

  if (commits.Empty()) {
    return -1;
  }

  const auto& commit = commits[0];

  if (!commit.HasMember("sha") || !commit["sha"].IsString()) {
    return -1;
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

  *commit_hash = commit["sha"].GetString();

  if (commit_date) {
    *commit_date = FormatDate(commit_date_).c_str();
  }

  return response_code;
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

uint32_t Updater::DownloadLatestNightlyArtifact(
    const std::string& workflow_file, const std::string& branch,
    const std::string& artifact_name, const std::string& output_path) const {
  const std::string endpoint =
      fmt::format("https://nightly.link/{}/{}/workflows/{}/{}/{}", owner_,
                  repo_, workflow_file, branch, artifact_name);

  return DownloadFile(endpoint, output_path);
}

uint32_t Updater::DownloadLatestRelease(const std::string& asset_name,
                                        const std::string& output_path) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/releases/latest", owner_, repo_);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  rapidjson::Document doc;
  doc.Parse(reinterpret_cast<char*>(response_buffer.data()));

  if (!doc.IsObject() || !doc.HasMember("assets")) {
    XELOGE("Invalid JSON or no assets.", output_path);
    return -1;
  }

  std::string asset_url;

  for (const auto& asset : doc["assets"].GetArray()) {
    if (asset.HasMember("name") && asset["name"].IsString() &&
        asset["name"].GetString() == asset_name &&
        asset.HasMember("browser_download_url")) {
      asset_url = asset["browser_download_url"].GetString();
      break;
    }
  }

  if (asset_url.empty()) {
    XELOGE("Asset '{}' not found in latest release.", asset_name);
    return -1;
  }

  return DownloadFile(asset_url, output_path);
}

uint32_t Updater::DownloadFile(const std::string& file_endpoint,
                               const std::string& output_path) const {
  std::vector<uint8_t> response_buffer = {};

  uint32_t response_code = GetRequest(file_endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  std::ofstream out_file(output_path, std::ios::binary);

  if (!out_file) {
    XELOGE("Failed to open output file: {}", output_path);
    return -1;
  }

  out_file.write(reinterpret_cast<char*>(response_buffer.data()),
                 response_buffer.size());

  out_file.close();

  return response_code;
}

uint32_t Updater::GetRecentCommitMessages(const std::string& branch,
                                          std::vector<std::string>& messages,
                                          uint32_t count) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/commits?sha={}&per_page={}", owner_,
      repo_, branch, count);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  std::string json =
      std::string(reinterpret_cast<char*>(response_buffer.data()));

  std::string json_wrapper = fmt::format(R"({{"commits": {}}})", json);

  response_buffer.resize(json_wrapper.size());

  std::memcpy(response_buffer.data(), json_wrapper.c_str(),
              json_wrapper.size());

  bool result = ParseCommitMessages(response_buffer, messages);

  if (!result) {
    return -1;
  }

  std::reverse(messages.begin(), messages.end());

  return response_code;
}

uint32_t Updater::GetChangelogBetweenCommits(
    const std::string& base_commit, const std::string& head_commit,
    std::vector<std::string>& messages) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint =
      fmt::format("https://api.github.com/repos/{}/{}/compare/{}...{}", owner_,
                  repo_, base_commit, head_commit);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  // Max 250 commits returned by compare API
  bool result = ParseCommitMessages(response_buffer, messages);

  if (!result) {
    return -1;
  }

  std::reverse(messages.begin(), messages.end());

  return response_code;
}

bool Updater::ParseCommitMessages(std::vector<uint8_t>& response_buffer,
                                  std::vector<std::string>& messages) const {
  std::string response = std::string(
      reinterpret_cast<char*>(response_buffer.data()), response_buffer.size());

  rapidjson::Document doc;
  doc.Parse(response.c_str());

  if (!doc.IsObject() || !doc.HasMember("commits")) {
    return false;
  }

  const auto& commits = doc["commits"];

  if (!commits.IsArray()) {
    return false;
  }

  for (const auto& commit : commits.GetArray()) {
    if (commit.HasMember("commit") && commit["commit"].HasMember("message")) {
      std::string msg = commit["commit"]["message"].GetString();
      messages.push_back(msg);
    }
  }

  return true;
}

bool Updater::UpdateAndRestart(const std::filesystem::path& zip_path) {
  std::error_code ec;

  if (zip_path.empty()) {
    return false;
  }

  if (!std::filesystem::exists(zip_path, ec) || ec) {
    return false;
  }

  const auto exe_path = xe::filesystem::GetExecutablePath();
  const auto exe_filename = exe_path.filename().string();
  const auto exe_parent = xe::filesystem::GetExecutableFolder();

  const auto update_script_path = exe_parent / "updater.bat";
  const auto zip_filename = zip_path.filename().string();

  const auto update_log_filename = "xenia_canary_update.log";

  if (std::filesystem::exists(update_script_path, ec) && !ec) {
    std::filesystem::remove(update_script_path, ec);
  }

  if (ec) {
    return false;
  }

  // Batch script for completing the automatic update process.
  std::string script_content = fmt::format(
      "@echo off\n"
      "set LOG_FILE=\"{1}\\{5}\"\n"
      "echo [INF] Starting Xenia update script > %LOG_FILE%\n"
      "echo [INF] Changed to extract directory >> %LOG_FILE%\n"
      "cd \"{1}\"\n"
      "echo [INF] Waiting for Xenia process to exit >> %LOG_FILE%\n"
      ":loop\n"
      "tasklist | findstr /I \"{0}\" > nul\n"
      "if not errorlevel 1 (\n"
      "  timeout /t 1 > nul\n"
      "  goto loop\n"
      ")\n"
      "echo [INF] Xenia process has exited >> %LOG_FILE%\n"
      "echo [INF] Cleaning and creating backup folder >> %LOG_FILE%\n"
      "if exist \"{1}\\old\" rd /s /q \"{1}\\old\"\n"
      "mkdir     \"{1}\\old\"\n"
      "echo [INF] Backing up old executable: {2} >> %LOG_FILE%\n"
      "copy /y \"{2}\" \"{1}\\old\\{0}\" >> %LOG_FILE% 2>&1\n"
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
      exe_filename,        // {0}
      exe_parent,          // {1}
      exe_path,            // {2}
      zip_filename,        // {3}
      zip_path,            // {4}
      update_log_filename  // {5}
  );

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

  SHELLEXECUTEINFO ShExecInfo = {};

  const std::wstring update_script_path_wstr_ = update_script_path.wstring();
  const wchar_t* update_script_path_wstr_ptr = update_script_path_wstr_.c_str();

  ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
  ShExecInfo.lpFile = update_script_path_wstr_ptr;
  ShExecInfo.nShow = SW_HIDE;

  return ShellExecuteEx(&ShExecInfo);
}

}  // namespace app
}  // namespace xe
