/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2025 Xenia Emulator. All rights reserved.                        *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_DISCORD_DISCORD_PRESENCE_H_
#define XENIA_DISCORD_DISCORD_PRESENCE_H_

#include <string>

#include "xenia/app/discord_game_sdk/src/discord.h"

namespace xe {
namespace discord_presence {

class DiscordPresence {
 public:
  static void Initialize();
  static void NotPlaying();
  static void PlayingTitle(const std::string_view game_title);
  static void Shutdown();

 private:
  static discord::Result UpdateActivity(discord::Activity const& activity);
};

}  // namespace discord_presence
}  // namespace xe

#endif  // XENIA_DISCORD_DISCORD_PRESENCE_H_
