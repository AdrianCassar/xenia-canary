group("third_party")
project("zarchive")
  uuid("d32f03aa-f0c9-11ed-a05b-0242ac120003")
  kind("StaticLib")
  language("C++")
  links({
    "zstd",
  })
  defines({
    "_LIB",
  })
  includedirs({
    "zarchive/include",
    "zstd/lib",
  })
  files({
    "zarchive/include/zarchive/zarchivecommon.h",
    "zarchive/include/zarchive/zarchivereader.h",
    "zarchive/include/zarchive/zarchivewriter.h",
    "zarchive/src/zarchivereader.cpp",
    "zarchive/src/zarchivewriter.cpp",
    "zarchive/src/sha_256.c",
    "zarchive/src/sha_256.h",
  })
