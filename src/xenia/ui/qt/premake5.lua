project_root = "../../../.."
include(project_root.."/tools/build")
local qt = premake.extensions.qt

group("src")
project("xenia-ui-qt")
  uuid("3AB69653-3ACB-4C0B-976C-3B7F044E3E3A")
  kind("StaticLib")
  language("C++")

  -- Setup Qt libraries
  qt.enable()
  qtpath "../../../../third_party/qt_binary/6.5.2/msvc2019_64"
  qtmodules{"core", "gui", "widgets"}
  qtprefix "Qt6"

  filter "configurations:Debug"
    qtsuffix "d"
  filter "configurations:Checked"
    qtsuffix "d"
  filter {}

  buildoptions { "/Zc:__cplusplus", "/permissive-" }

  links({
    "xenia-base",
    "xenia-core",
    "xenia-ui"
  })
  defines({

  })
  recursive_platform_files()
  removefiles({
    "qt_demoapp.cc",
  })
 
group("src")
project("xenia-ui-qt-demoapp")
  uuid("b46e0512-b0cb-4a02-90c6-c23db81c6de3")
  kind("WindowedApp")
  language("C++")
  entrypoint("mainCRTStartup")

  -- Setup Qt libraries
  qt.enable()
  qtpath "../../../../third_party/qt_binary/6.5.2/msvc2019_64"
  qtmodules{"core", "gui", "widgets"}
  qtprefix "Qt6"

  filter "configurations:Debug"
    qtsuffix "d"
  filter "configurations:Checked"
    qtsuffix "d"
  filter {}
  
  buildoptions { "/Zc:__cplusplus", "/permissive-" }

  links({
    "xenia-base",
    "xenia-app-library",
    "xenia-app-settings",
    "xenia-vfs",
    "xenia-ui-qt",
    "xenia-kernel",
    "xenia-cpu",
  })
  defines({})
  files({
    "qt_demoapp.cc",
    "../../app/xenia.qrc", -- hardcode for now
  })