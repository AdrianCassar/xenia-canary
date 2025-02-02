project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-app-discord")
  uuid("d14c0885-22d2-40de-ab28-7b234ef2b949")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-app-discord-game-sdk",
  })
  defines({
  })
  includedirs({
  })
  files({
    "discord_presence.cc",
    "discord_presence.h"
  })
