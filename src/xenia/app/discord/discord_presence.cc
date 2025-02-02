/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2025 Xenia Emulator. All rights reserved.                        *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <ctime>

#include "xenia/app/discord/discord_presence.h"

namespace xe {
namespace discord_presence {

discord::Core* core = {};

void DiscordPresence::Initialize() {
  auto result = discord::Core::Create(1193272084797849762,
                                      DiscordCreateFlags_Default, &core);
}

void DiscordPresence::NotPlaying() {
  discord::Activity activity = {};

  activity.SetState("Idle");
  activity.SetDetails("Standby");
  activity.GetAssets().SetLargeImage("app");
  activity.GetAssets().SetLargeText("Xenia Canary");
  activity.GetTimestamps().SetStart(time(nullptr));

  UpdateActivity(activity);

  // core->ActivityManager().UpdateActivity(activity,
  //                                        [](discord::Result result) {});
}

void DiscordPresence::PlayingTitle(const std::string_view game_title) {
  discord::Activity activity = {};

  activity.SetState("In Game");
  activity.SetDetails(game_title.data());

  UpdateActivity(activity);

  // core->ActivityManager().UpdateActivity(activity,
  //                                        [](discord::Result result) {});
}

void DiscordPresence::Shutdown() { delete core; }

discord::Result DiscordPresence::UpdateActivity(
    discord::Activity const& activity) {
  discord::Result update_result = discord::Result::Ok;

  core->ActivityManager().UpdateActivity(
      activity,
      [&update_result](discord::Result result) { update_result = result; });

  return update_result;
}

}  // namespace discord_presence
}  // namespace xe
