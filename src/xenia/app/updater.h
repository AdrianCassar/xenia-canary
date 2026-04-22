/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_UPDATER_H_
#define XENIA_APP_UPDATER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "third_party/libcurl/include/curl/system.h"

namespace xe {
namespace app {

struct UpdateMetadata {
  std::string commit_hash;
  std::string tag;
  std::string published_date;
  std::string commit_date;
  uint32_t response_code = 0;
};

struct CheckForUpdateInfo {
  UpdateMetadata metadata;
  bool update_available = false;
};

struct CommitMessages {
  std::vector<std::string> messages;
  std::string status;
  bool success = false;
};

struct ChangelogInfo {
  CommitMessages messages;
  uint32_t response_code = 0;
};

class Updater {
 public:
  Updater(const std::string& owner, const std::string& repo);

  ~Updater() {};
  bool UpdateAndRestart(const std::filesystem::path& zip_path);

  uint32_t GetRequest(const std::string& endpoint,
                      std::vector<uint8_t>& response_buffer,
                      std::atomic<bool>& cancel_flag) const;

  CheckForUpdateInfo CheckForUpdatesViaXeniaManagerDatabase(
      bool stable, std::atomic<bool>& cancel_flag) const;

  std::future<CheckForUpdateInfo> StartupUpdateCheckAsync(
      std::atomic<bool>& cancel_flag,
      std::function<void(CheckForUpdateInfo)> callback) const;

  CheckForUpdateInfo StartupUpdateCheck(
      std::atomic<bool>& cancel_flag,
      std::function<void(CheckForUpdateInfo)> callback) const;

  std::future<CheckForUpdateInfo> CheckForUpdatesAsync(
      bool stable, const std::string& branch,
      std::atomic<bool>& cancel_flag) const;

  CheckForUpdateInfo CheckForUpdates(bool stable, const std::string& branch,
                                     std::atomic<bool>& cancel_flag) const;

  std::wstring RunPowershellCommand(const std::string& command) const;

  bool IsAnotherInstanceRunning() const;

  UpdateMetadata GetLatestCommitHash(const std::string& branch,
                                     std::atomic<bool>& cancel_flag) const;

  UpdateMetadata GetLatestReleaseCommitHash(
      std::atomic<bool>& cancel_flag) const;

  std::string FormatDate(const std::string& iso_date) const;

  std::future<uint32_t> DownloadLatestNightlyArtifactAsync(
      const std::string& workflow_file, const std::string& branch,
      const std::string& artifact_name, const std::string& output_path,
      std::atomic<bool>& cancel_flag,
      std::function<void(double, double)> progress_callback);

  uint32_t DownloadLatestNightlyArtifact(
      const std::string& workflow_file, const std::string& branch,
      const std::string& artifact_name, const std::string& output_path,
      std::atomic<bool>& cancel_flag,
      std::function<void(double, double)> progress_callback) const;

  std::future<uint32_t> DownloadLatestReleaseAsync(
      const std::string& asset_name, const std::string& output_path,
      std::atomic<bool>& cancel_flag,
      std::function<void(double, double)> progress_callback);

  uint32_t DownloadLatestRelease(
      const std::string& asset_name, const std::string& output_path,
      std::atomic<bool>& cancel_flag,
      std::function<void(double, double)> progress_callback) const;

  uint32_t DownloadFile(
      const std::string& file_endpoint, const std::string& output_path,
      std::atomic<bool>& cancel_flag,
      std::function<void(double, double)> progress_callback) const;

  ChangelogInfo GetRecentCommitMessages(const std::string& branch,
                                        std::atomic<bool>& cancel_flag,
                                        uint32_t count = 5) const;

  std::future<ChangelogInfo> GetChangelogBetweenCommitsAsync(
      const std::string& base_commit, const std::string& head_commit,
      std::atomic<bool>& cancel_flag) const;

  ChangelogInfo GetChangelogBetweenCommits(
      const std::string& base_commit, const std::string& head_commit,
      std::atomic<bool>& cancel_flag) const;

  CommitMessages ParseCommitMessages(
      const std::vector<uint8_t>& response_buffer) const;

  const std::string GetOwner() const { return owner_; }

  const std::string GeRepo() const { return repo_; }

 private:
  std::string owner_;
  std::string repo_;

  static size_t WriteResponceToMemoryCallback(void* contents, size_t size,
                                              size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::vector<uint8_t>* buffer = static_cast<std::vector<uint8_t>*>(userp);
    uint8_t* dataPtr = static_cast<uint8_t*>(contents);

    buffer->insert(buffer->end(), dataPtr, dataPtr + total_size);
    return total_size;
  }

  struct ProgressCallbackData {
    std::function<void(double, double)> progress_callback;
    std::atomic<bool>* cancelled;
  };

  static int CurlProgressCallback(void* clientp, curl_off_t dltotal,
                                  curl_off_t dlnow, curl_off_t ultotal,
                                  curl_off_t ulnow) {
    const ProgressCallbackData* callback_data =
        static_cast<ProgressCallbackData*>(clientp);

    // Check atomic cancellation flag first
    if (callback_data->cancelled && callback_data->cancelled->load()) {
      // Abort Transfer
      return 1;
    }

    if (callback_data->progress_callback) {
      callback_data->progress_callback((double)dlnow, (double)dltotal);
    }

    // Continue downloading
    return 0;
  }
};
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_UPDATER_H_
