project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-kernel")
  uuid("ae185c4a-1c4f-4503-9892-328e549e871a")
  kind("StaticLib")
  language("C++")
  links({
    "aes_128",
    "fmt",
    "libcurl",
    "miniupnp",
    "xenia-apu",
    "xenia-base",
    "xenia-cpu",
    "xenia-hid",
    "xenia-vfs",
  })
  defines({
    "CURL_STATICLIB",
    "MINIUPNP_STATICLIB"
  })
  recursive_platform_files()
  files({
    "debug_visualizers.natvis",
  })
