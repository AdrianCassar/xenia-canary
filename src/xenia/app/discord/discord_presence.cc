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

#include "third_party/discord-rpc/include/discord_rpc.h"

#include "xenia/app/discord/discord_presence.h"
#include "xenia/base/string.h"

// TODO: This library has been deprecated in favor of Discord's GameSDK.
namespace xe {
namespace discord {

void HandleDiscordReady(const DiscordUser* request) {}
void HandleDiscordError(int errorCode, const char* message) {}
void HandleDiscordJoinGame(const char* joinSecret) {}
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

  const std::string state_processed =
      std::regex_replace(std::string(state), std::regex("\\n"), ", ");

  auto details = std::string(game_title);
  DiscordRichPresence discordPresence = {};
  discordPresence.state = state_processed.data();
  discordPresence.details = details.c_str();
  // TODO(gibbed): we don't have state icons yet.
  // discordPresence.smallImageKey = "app";
  // discordPresence.largeImageKey = "state_ingame";
  discordPresence.largeImageKey = "app";
  discordPresence.largeImageText = "Xenia Canary - Netplay";
  discordPresence.startTimestamp = start_time;
  discordPresence.instance = 1;
  Discord_UpdatePresence(&discordPresence);
}

void DiscordPresence::Shutdown() { Discord_Shutdown(); }

}  // namespace discord
}  // namespace xe
