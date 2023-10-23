group("third_party")
project("miniupnp")
  uuid("b7e0088f-ace6-4bba-b2cc-faae156c27fc")
  kind("StaticLib")
  language("C")
  links({
    "iphlpapi",
    "ws2_32"
  })
  defines({
    "MINIUPNP_STATICLIB"
  })
  filter({"configurations:Release", "platforms:Windows"})
    buildoptions({
      "/Os",
      "/O1",
    })

  filter { "platforms:Windows" }
    prebuildcommands {
      "cd $(SolutionDir)..\\third_party\\miniupnp\\miniupnpc\\msvc",
      "genminiupnpcstrings.vbs"
    }

  filter { "platforms:Linux" }
    prebuildcommands {
      "cd $(SolutionDir)..\\third_party\\miniupnp\\miniupnpc",
      "updateminiupnpcstrings.sh"
    }

  filter {}

  includedirs({
    "miniupnp/miniupnpc",
    "miniupnp/miniupnpc/include",
  })
  files({
    "miniupnp/miniupnpc/src/addr_is_reserved.c",
    "miniupnp/miniupnpc/src/connecthostport.c",
    "miniupnp/miniupnpc/src/igd_desc_parse.c",
    "miniupnp/miniupnpc/src/minisoap.c",
    "miniupnp/miniupnpc/src/minissdpc.c",
    "miniupnp/miniupnpc/src/miniupnpc.c",
    "miniupnp/miniupnpc/src/miniwget.c",
    "miniupnp/miniupnpc/src/minixml.c",
    "miniupnp/miniupnpc/src/portlistingparse.c",
    "miniupnp/miniupnpc/src/receivedata.c",
    "miniupnp/miniupnpc/src/upnpcommands.c",
    "miniupnp/miniupnpc/src/upnpdev.c",
    "miniupnp/miniupnpc/src/upnperrors.c",
    "miniupnp/miniupnpc/src/upnpreplyparse.c",
  })