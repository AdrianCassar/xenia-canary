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

#include <string>
#include <vector>

namespace xe {
namespace app {
class Updater {
 public:
  Updater(const std::string& owner, const std::string& repo);

  ~Updater() {};

  uint32_t GetRequest(const std::string& endpoint,
                      std::vector<uint8_t>& response_buffer) const;

  bool CheckForUpdates(const std::string& branch, std::string* commit_hash,
                       std::string* commit_date, uint32_t* response_code);

  uint32_t GetLatestCommitHash(const std::string& branch,
                               std::string* commit_hash,
                               std::string* commit_date);

  std::string FormatDate(const std::string& iso_date) const;

  uint32_t DownloadLatestNightlyArtifact(const std::string& workflowFile,
                                         const std::string& branch,
                                         const std::string& artifact_name,
                                         const std::string& outputPath) const;

  uint32_t DownloadLatestRelease(const std::string& asset_name,
                                 const std::string& output_path) const;

  uint32_t DownloadFile(const std::string& file_endpoint,
                        const std::string& output_path) const;

  uint32_t GetRecentCommitMessages(const std::string& branch,
                                   std::vector<std::string>& messages,
                                   uint32_t count = 5) const;

  uint32_t GetChangelogBetweenCommits(const std::string& base_commit,
                                      const std::string& head_commit,
                                      std::vector<std::string>& messages) const;

  bool ParseCommitMessages(std::vector<uint8_t>& response_buffer,
                           std::vector<std::string>& messages) const;

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
};
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_UPDATER_H_