/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2020 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <ctime>
#include <cstring>
#include <regex>
#include <vector>

extern "C" {
#include "third_party/FFmpeg/libavutil/base64.h"
}

#include "third_party/discord-rpc/include/discord_rpc.h"
#include "third_party/fmt/include/fmt/format.h"

#include "xenia/app/discord/discord_presence.h"
#include "xenia/base/string.h"

// TODO: This library has been deprecated in favor of Discord's GameSDK.
namespace xe {
namespace discord {

void HandleDiscordReady(const DiscordUser* request) {}
void HandleDiscordError(int errorCode, const char* message) {}
void HandleDiscordJoinGame(const char* joinSecret) {
  if (joinSecret) {
    DiscordPresence::ProcessJoinSecret(joinSecret);
  }
}
void HandleDiscordJoinRequest(const DiscordUser* request) {}
void HandleDiscordSpectateGame(const char* spectateSecret) {}

void DiscordPresence::Initialize() {
  DiscordEventHandlers handlers = {};
  handlers.ready = &HandleDiscordReady;
  handlers.errored = &HandleDiscordError;
  handlers.joinGame = &HandleDiscordJoinGame;
  handlers.joinRequest = &HandleDiscordJoinRequest;
  handlers.spectateGame = &HandleDiscordSpectateGame;
  Discord_Initialize("1193272084797849762", &handlers, 0, "");
}

void DiscordPresence::NotPlaying() {
  DiscordRichPresence discordPresence = {};
  discordPresence.state = "Idle";
  discordPresence.details = "Standby";
  discordPresence.largeImageKey = "app";
  discordPresence.largeImageText = "Xenia Canary - Netplay";
  discordPresence.startTimestamp = time(0);
  discordPresence.instance = 1;
  Discord_UpdatePresence(&discordPresence);
}

void DiscordPresence::PlayingTitle(const std::string_view game_title,
                                   const std::string_view state) {
  if (!start_time) {
    start_time = time(0);
  }

  current_state_ =
      std::regex_replace(std::string(state), std::regex("\\n"), ", ");
  current_details_ = std::string(game_title);
  join_secret_.clear();
  party_id_.clear();
  party_size_ = 0;
  party_max_ = 0;
  UpdatePresence();
}

void DiscordPresence::UpdateSession(uint32_t title_id,
                                   const kernel::XSESSION_INFO* session_info,
                                   int party_size, int party_max,
                                   uint64_t host_xuid) {
  if (session_info != nullptr) {
    // Join secret: full X_INVITE_INFO (xuid_invitee = 0, filled on join)
    kernel::X_INVITE_INFO invite = {};
    invite.xuid_invitee = 0;
    invite.xuid_inviter = host_xuid;
    invite.title_id = title_id;
    invite.host_info = *session_info;
    invite.from_game_invite = title_id;

    const int b64_size =
        AV_BASE64_SIZE(static_cast<int>(sizeof(kernel::X_INVITE_INFO)));
    std::vector<char> b64(static_cast<size_t>(b64_size));
    if (av_base64_encode(
            b64.data(), b64_size,
            reinterpret_cast<const uint8_t*>(&invite),
            static_cast<int>(sizeof(kernel::X_INVITE_INFO)))) {
      join_secret_.assign(b64.data());
    }
    party_id_ = fmt::format(
        "{:016X}",
        kernel::XNKIDtoUint64(const_cast<kernel::XNKID*>(&session_info->sessionID)));
    party_size_ = party_size;
    party_max_ = party_max;
  } else {
    join_secret_.clear();
    party_id_.clear();
    party_size_ = 0;
    party_max_ = 0;
  }
  UpdatePresence();
}

void DiscordPresence::UpdatePresence() {
  if (current_details_.empty()) {
    return;
  }
  DiscordRichPresence discordPresence = {};
  discordPresence.state = current_state_.c_str();
  discordPresence.details = current_details_.c_str();
  discordPresence.largeImageKey = "app";
  discordPresence.largeImageText = "Xenia Canary - Netplay";
  discordPresence.startTimestamp = start_time;
  discordPresence.instance = 1;
  if (!join_secret_.empty()) {
    discordPresence.joinSecret = join_secret_.c_str();
    if (party_max_ > 0 && !party_id_.empty()) {
      discordPresence.partyId = party_id_.c_str();
      discordPresence.partySize = party_size_;
      discordPresence.partyMax = party_max_;
      discordPresence.partyPrivacy = DISCORD_PARTY_PUBLIC;
    }
  }
  Discord_UpdatePresence(&discordPresence);
}

void DiscordPresence::SetJoinRequestHandler(
    std::function<void(kernel::X_INVITE_INFO)> handler) {
  join_request_handler_ = std::move(handler);
}

void DiscordPresence::ProcessJoinSecret(const char* join_secret) {
  if (!join_request_handler_) {
    return;
  }
  auto invite = DecodeJoinSecret(join_secret);
  if (invite) {
    join_request_handler_(std::move(*invite));
  }
}

std::optional<kernel::X_INVITE_INFO> DiscordPresence::DecodeJoinSecret(
    const std::string& join_secret) {
  if (join_secret.empty()) {
    return std::nullopt;
  }
  const int decode_size = AV_BASE64_DECODE_SIZE(static_cast<int>(join_secret.size()));
  if (decode_size < static_cast<int>(sizeof(kernel::X_INVITE_INFO))) {
    return std::nullopt;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(decode_size));
  const int out_len =
      av_base64_decode(buf.data(), join_secret.c_str(), decode_size);
  if (out_len != static_cast<int>(sizeof(kernel::X_INVITE_INFO))) {
    return std::nullopt;
  }
  kernel::X_INVITE_INFO invite;
  std::memcpy(&invite, buf.data(), sizeof(kernel::X_INVITE_INFO));
  return invite;
}

void DiscordPresence::Shutdown() { Discord_Shutdown(); }

}  // namespace discord
}  // namespace xe
