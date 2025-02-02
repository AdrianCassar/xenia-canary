project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-app-discord-game-sdk")
  uuid("91cdcd19-faed-4449-b0eb-3b4230544d8e")
  kind("StaticLib")
  language("C++")
  links({
  })
  defines({
  })
  recursive_platform_files()
