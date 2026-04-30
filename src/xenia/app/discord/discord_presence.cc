/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2020 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <ctime>
#include <regex>

extern "C" {
#include "third_party/FFmpeg/libavutil/base64.h"
}

#include "third_party/discord-rpc/include/discord_rpc.h"

#include "xenia/app/discord/discord_presence.h"
#include "xenia/base/string.h"

DEFINE_bool(discord, true, "Enable Discord rich presence", "General");

// TODO: This library has been deprecated in favor of Discord's GameSDK.
namespace xe {
namespace discord {

static void HandleDiscordReady(const DiscordUser* request) {}
static void HandleDiscordError(int errorCode, const char* message) {}
static void HandleDiscordJoinGame(const char* joinSecret) {
  if (joinSecret) {
    DiscordPresence::ProcessJoinSecret(joinSecret);
  }
}

void DiscordPresence::Initialize() {
  DiscordEventHandlers handlers = {};
  handlers.ready = HandleDiscordReady;
  handlers.errored = HandleDiscordError;
  handlers.joinGame = HandleDiscordJoinGame;
  Discord_Initialize("1193272084797849762", &handlers, 1, nullptr);
  initialized_ = true;
}

void DiscordPresence::Update() {
  if (!initialized_) {
    return;
  }
  Discord_RunCallbacks();
}

void DiscordPresence::NotPlaying() {
  DiscordRichPresence discordPresence = {};
  discordPresence.state = "Idle";
  discordPresence.details = "Standby";
  discordPresence.largeImageKey = "app";
  discordPresence.largeImageText = "Xenia Canary - Netplay";
  discordPresence.startTimestamp = time(0);
  Discord_UpdatePresence(&discordPresence);
}

void DiscordPresence::PlayingTitle(const std::string_view game_title,
                                   const std::string_view state) {
  if (!initialized_) {
    return;
  }

  if (!start_time) {
    start_time = time(0);
  }

  current_state_ =
      std::regex_replace(std::string(state), std::regex("\\n"), ", ");
  current_details_ = std::string(game_title);
  UpdatePresence();
}

void DiscordPresence::UpdateSession(uint32_t title_id,
                                    const kernel::XSESSION_INFO* session_info,
                                    uint32_t party_size, uint32_t party_max,
                                    uint64_t host_xuid) {
  bool reset = false;

  if (session_info) {
    kernel::X_INVITE_INFO invite = {};
    invite.xuid_invitee = 0;  // Filled on join
    invite.xuid_inviter = host_xuid;
    invite.title_id = title_id;
    invite.host_info = *session_info;
    invite.from_game_invite = 0;

    const uint32_t size = sizeof(kernel::X_INVITE_INFO);
    const uint32_t out_size = AV_BASE64_SIZE(size);

    std::vector<char> serialized_data(out_size);

    const char* out =
        av_base64_encode(serialized_data.data(), out_size,
                         reinterpret_cast<const uint8_t*>(&invite), size);

    if (out) {
      const uint64_t session_id = kernel::XNKIDtoUint64(
          const_cast<kernel::XNKID*>(&session_info->sessionID));

      join_secret_.assign(serialized_data.data());
      party_id_ = fmt::format("{:016X}", session_id);
      party_size_ = party_size;
      party_max_ = party_max;
    } else {
      reset = true;
    }
  } else {
    reset = true;
  }

  if (reset) {
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

  const std::optional<kernel::X_INVITE_INFO> invite_info =
      DecodeJoinSecret(join_secret);

  if (invite_info.has_value()) {
    join_request_handler_(invite_info.value());
  }
}

std::optional<kernel::X_INVITE_INFO> DiscordPresence::DecodeJoinSecret(
    const std::string join_secret) {
  const std::uint32_t size = static_cast<uint32_t>(join_secret.size());
  const std::uint32_t decode_size = AV_BASE64_DECODE_SIZE(size);

  std::vector<uint8_t> property_data(decode_size);

  const int out =
      av_base64_decode(property_data.data(), join_secret.c_str(), decode_size);

  if (out < 0) {
    return std::nullopt;
  }

  if (decode_size < sizeof(kernel::X_INVITE_INFO)) {
    return std::nullopt;
  }

  kernel::X_INVITE_INFO invite_info =
      *reinterpret_cast<kernel::X_INVITE_INFO*>(property_data.data());

  return invite_info;
}

void DiscordPresence::Shutdown() {
  if (!initialized_) {
    return;
  }
  initialized_ = false;
  Discord_Shutdown();
}

void DiscordPresence::SetDiscordState(bool state) {
  OVERRIDE_bool(discord, state);
}

}  // namespace discord
}  // namespace xe
