/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2020 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_DISCORD_DISCORD_PRESENCE_H_
#define XENIA_DISCORD_DISCORD_PRESENCE_H_

#include <string>

#include "xenia/kernel/xnet.h"

namespace xe {
namespace discord {

class DiscordPresence {
 public:
  static void Initialize();
  static void Update();
  static void NotPlaying();
  static void PlayingTitle(const std::string_view game_title,
                           const std::string_view state);
  static void UpdateSession(uint32_t title_id,
                            const kernel::XSESSION_INFO* session_info,
                            uint32_t party_size, uint32_t party_max,
                            uint64_t host_xuid);
  static std::optional<kernel::X_INVITE_INFO> DecodeJoinSecret(
      const std::string join_secret);
  static void SetJoinRequestHandler(
      std::function<void(kernel::X_INVITE_INFO)> handler);
  static void Shutdown();

  static void SetDiscordState(const bool state);

  // Called by the Discord SDK when user requests to join; decodes and
  // invokes the registered handler. Public so the C callback can call it.
  static void ProcessJoinSecret(const char* join_secret);

  inline static time_t start_time;

 private:
  static void UpdatePresence();

  inline static bool initialized_ = false;
  inline static std::string current_details_;
  inline static std::string current_state_;
  inline static std::string join_secret_;
  inline static std::string party_id_;
  inline static uint32_t party_size_ = 0;
  inline static uint32_t party_max_ = 0;

  inline static std::function<void(kernel::X_INVITE_INFO)>
      join_request_handler_;
};

}  // namespace discord
}  // namespace xe

#endif  // XENIA_DISCORD_DISCORD_PRESENCE_H_
