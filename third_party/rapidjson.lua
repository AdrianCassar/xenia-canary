group("third_party")
project("rapidjson")
  uuid("7bbf871e-f65b-4b32-92ee-f6c316ed3e56")
  kind("StaticLib")
  language("C++")
  links({
  })
  defines({
    "RAPIDJSON_SSE42",
  })

  filter {}

  includedirs({
    "rapidjson/include",
  })
  files({
    "rapidjson/include/**.h",
  })