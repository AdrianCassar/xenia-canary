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
#include <functional>
#include <iostream>
#include <string>

// math.h and curl.h conflict so we include it first
#include "xenia/kernel/xnet.h"

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/libcurl/include/curl/curl.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"

#include "version.h"
#include "xenia/app/updater.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {

// TODO:
// - SSL backend for libcurl on Linux using wolfssl
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

bool Updater::StartupUpdateCheck(std::string* commit_hash,
                                 std::string* commit_date,
                                 uint32_t* response_code) {
  const std::string endpoint =
      "https://xenia-manager.github.io/database/data/version.json";

  const std::string _func_ = __func__;
  auto fallback = [&, _func_](const char* reason) {
    XELOGW("{}: {}, falling back to GitHub API", _func_, reason);
    std::string tag;
    return CheckForUpdates(false, XE_BUILD_BRANCH, commit_hash, commit_date,
                           &tag, response_code);
  };

  // Perform HTTP GET
  std::vector<uint8_t> response_buffer;
  uint32_t result = GetRequest(endpoint, response_buffer);

  if (response_code) {
    *response_code = result;
  }
  if (result != HTTP_STATUS_CODE::HTTP_OK) {
    return fallback(
        fmt::format("endpoint request failed with HTTP {}", result).c_str());
  }

  // Parse JSON
  const std::string response_data(response_buffer.begin(),
                                  response_buffer.end());
  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError() || !document.IsObject()) {
    return fallback("JSON parse error or invalid root");
  }

  // Navigate JSON
  auto get_object = [&](const rapidjson::Value& parent,
                        const char* name) -> const rapidjson::Value* {
    return (parent.HasMember(name) && parent[name].IsObject()) ? &parent[name]
                                                               : nullptr;
  };

  const rapidjson::Value* xenia = get_object(document, "xenia");
  const rapidjson::Value* netplay =
      xenia ? get_object(*xenia, "netplay") : nullptr;
  const rapidjson::Value* nightly =
      netplay ? get_object(*netplay, "nightly") : nullptr;

  if (!nightly) {
    return fallback("missing 'xenia.netplay.nightly' object");
  }

  if (!nightly->HasMember("commit_sha") ||
      !(*nightly)["commit_sha"].IsString()) {
    return fallback("missing 'commit_sha'");
  }

  // Extract values
  const std::string latest_commit = (*nightly)["commit_sha"].GetString();
  const std::string latest_date =
      (nightly->HasMember("date") && (*nightly)["date"].IsString())
          ? FormatDate((*nightly)["date"].GetString())
          : "";

  if (commit_hash) {
    *commit_hash = latest_commit;
  }
  if (commit_date) {
    *commit_date = latest_date;
  }

  bool update_available = latest_commit != XE_BUILD_COMMIT;
  XELOGI("{}: current={}, latest={}, date={}", __func__, XE_BUILD_COMMIT,
         latest_commit, latest_date);

  return update_available;
}

bool Updater::CheckForUpdates(bool stable, const std::string& branch,
                              std::string* commit_hash, std::string* date,
                              std::string* tag, uint32_t* response_code) {
  uint32_t result = 0;

  bool update_available = false;

  if (stable) {
    result = GetLatestReleaseCommitHash(nullptr, tag, date);
  } else {
    result = GetLatestCommitHash(branch, commit_hash, date);
  }

  if (response_code) {
    *response_code = result;
  }

  if (result != HTTP_STATUS_CODE::HTTP_OK) {
    return false;
  }

  if (stable) {
    std::string commit_compare_status;
    std::vector<std::string> commit_messages;

    // Either get commit hash from tag or compare commits to get state
    result = GetChangelogBetweenCommits(XE_BUILD_COMMIT, *tag,
                                        commit_compare_status, commit_messages);

    if (response_code) {
      *response_code = result;
    }

    if (result != HTTP_STATUS_CODE::HTTP_OK) {
      return false;
    }

    update_available = commit_compare_status != "identical";
  } else {
    update_available = *commit_hash != XE_BUILD_COMMIT;
  }

  return update_available;
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

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError()) {
    return -1;
  }

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

uint32_t Updater::GetLatestReleaseCommitHash(std::string* commit_hash,
                                             std::string* tag,
                                             std::string* published_date) {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/releases/latest", owner_, repo_);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError()) {
    return -1;
  }

  if (document.HasMember("tag_name") && document["tag_name"].IsString()) {
    if (tag) {
      *tag = document["tag_name"].GetString();
    }
  }

  if (document.HasMember("published_at") &&
      document["published_at"].IsString()) {
    if (published_date) {
      *published_date =
          FormatDate(document["published_at"].GetString()).c_str();
    }
  }

  if (document.HasMember("target_commitish") &&
      document["target_commitish"].IsString()) {
    if (commit_hash) {
      *commit_hash = document["target_commitish"].GetString();
    }
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
    const std::string& artifact_name, const std::string& output_path,
    std::function<void(double, double)> progress_callback) const {
  const std::string endpoint =
      fmt::format("https://nightly.link/{}/{}/workflows/{}/{}/{}", owner_,
                  repo_, workflow_file, branch, artifact_name);

  return DownloadFile(endpoint, output_path, progress_callback);
}

uint32_t Updater::DownloadLatestRelease(
    const std::string& asset_name, const std::string& output_path,
    std::function<void(double, double)> progress_callback) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/releases/latest", owner_, repo_);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError()) {
    return -1;
  }

  if (!document.IsObject() || !document.HasMember("assets")) {
    XELOGE("Invalid JSON or no assets.", output_path);
    return -1;
  }

  std::string asset_url;

  for (const auto& asset : document["assets"].GetArray()) {
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

  return DownloadFile(asset_url, output_path, progress_callback);
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

static int CurlProgressCallback(void* clientp, curl_off_t dltotal,
                                curl_off_t dlnow, curl_off_t ultotal,
                                curl_off_t ulnow) {
  auto* progress_callback =
      reinterpret_cast<std::function<void(double, double)>*>(clientp);
  if (progress_callback && *progress_callback) {
    (*progress_callback)((double)dlnow, (double)dltotal);
  }
  return 0;  // 0 = continue, else abort transfer
}

uint32_t Updater::DownloadFile(
    const std::string& file_endpoint, const std::string& output_path,
    std::function<void(double, double)> progress_callback) const {
  std::vector<uint8_t> response_buffer = {};

  CURL* curl = curl_easy_init();
  if (!curl) return -1;

  FILE* fp = fopen(output_path.c_str(), "wb");
  if (!fp) {
    curl_easy_cleanup(curl);
    return -1;
  }

  curl_easy_setopt(curl, CURLOPT_URL, file_endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia-canary");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);

  // Download progress tracking
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS,
                   0L);  // Must be set to 0 for XFERINFOFUNCTION
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_callback);

  CURLcode result = curl_easy_perform(curl);

  fclose(fp);

  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  curl_easy_cleanup(curl);

  if (result != CURLE_OK && response_code == 0) {
    response_code = -1;
  }

  return response_code;
}

uint32_t Updater::GetRecentCommitMessages(const std::string& branch,
                                          std::vector<std::string>& messages,
                                          std::string& status,
                                          uint32_t count) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint = fmt::format(
      "https://api.github.com/repos/{}/{}/commits?sha={}&per_page={}", owner_,
      repo_, branch, count);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  std::string json_wrapper =
      fmt::format(R"({{"commits": {}}})", response_data.c_str());

  response_buffer.resize(json_wrapper.size());

  std::memcpy(response_buffer.data(), json_wrapper.c_str(),
              json_wrapper.size());

  bool result = ParseCommitMessages(response_buffer, messages, status);

  if (!result) {
    return -1;
  }

  std::reverse(messages.begin(), messages.end());

  return response_code;
}

uint32_t Updater::GetChangelogBetweenCommits(
    const std::string& base_commit, const std::string& head_commit,
    std::string& status, std::vector<std::string>& messages) const {
  std::vector<uint8_t> response_buffer = {};

  const std::string endpoint =
      fmt::format("https://api.github.com/repos/{}/{}/compare/{}...{}", owner_,
                  repo_, base_commit, head_commit);

  uint32_t response_code = GetRequest(endpoint, response_buffer);

  if (response_code != HTTP_STATUS_CODE::HTTP_OK) {
    return response_code;
  }

  // Max 250 commits returned by compare API
  bool result = ParseCommitMessages(response_buffer, messages, status);

  if (!result) {
    return -1;
  }

  std::reverse(messages.begin(), messages.end());

  return response_code;
}

bool Updater::ParseCommitMessages(std::vector<uint8_t>& response_buffer,
                                  std::vector<std::string>& messages,
                                  std::string& status) const {
  const std::string response_data =
      std::string(response_buffer.cbegin(), response_buffer.cend());

  rapidjson::Document document;
  document.Parse(response_data.c_str());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject() || !document.HasMember("commits")) {
    XELOGE("Invalid JSON or no commits array to parse.");
    return false;
  }

  const auto& commits = document["commits"];

  if (document.HasMember("status")) {
    status = document["status"].GetString();
  }

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
      "EXECUTABLE_NAME=\"{0}\"                    # final executable name\n"
      "EXECUTABLE_PATH=\"$(dirname \"$(realpath \"$0\")\")/$EXECUTABLE_NAME\"\n"
      "ARCHIVE_FILE=\"{1}\"          # archive file\n"
      "INNER_PATH=\"build/bin/Linux/Release/xenia_canary_netplay\" # path "
      "inside archive\n"
      "LOG_FILE=\"$(dirname \"$(realpath \"$0\")\")/{2}\"\n"
      "BACKUP_DIR=\"$(dirname \"$(realpath \"$0\")\")/{3}\"\n"
      "\n"
      "echo \"[INF] Starting Xenia update script\" > \"$LOG_FILE\"\n"
      "\n"
      "cd \"$(dirname \"$(realpath \"$0\")\")\" || exit 1\n"
      "\n"
      "# Check if tar is installed before doing anything else\n"
      "if ! command -v tar &> /dev/null; then\n"
      "    echo \"[ERR] tar is not installed\"\n"
      "    echo \"[INF] Relaunching Xenia with update failed flag\" >> "
      "\"$LOG_FILE\"\n"
      "    \"$EXECUTABLE_PATH\" --updated=false &\n"
      "    rm -- \"$0\"\n"
      "    exit 1\n"
      "fi\n"
      "\n"
      "# Extract only the new executable directly from tar.xz\n"
      "echo \"[INF] Extracting executable from archive: $ARCHIVE_FILE\" >> "
      "\"$LOG_FILE\"\n"
      "if ! tar -xJf \"$ARCHIVE_FILE\" \"$INNER_PATH\" >> \"$LOG_FILE\" 2>&1; "
      "then\n"
      "  echo \"[ERR] Failed to extract $INNER_PATH from $ARCHIVE_FILE\" >> "
      "\"$LOG_FILE\"\n"
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
      "echo \"[INF] Installing new executable\" >> \"$LOG_FILE\"\n"
      "install \"$INNER_PATH\" \"$EXECUTABLE_PATH\"\n"
      "\n"
      "# Cleanup extracted folders\n"
      "rm -rf build\n"
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
