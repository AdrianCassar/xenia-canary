group("third_party")
project("libcurl")
  uuid("1ba7e608-5752-457c-8df0-c006c6e8b7fe")
  kind("StaticLib")
  language("C")
  links({
    "Wldap32",
    "crypt32"
  })
  defines({
    "BUILDING_LIBCURL",

    -- "USE_WOLFSSL",
    -- "WITHOUT_SSL",

    "USE_SCHANNEL",
    "USE_WINDOWS_SSPI",
  })
  filter({"configurations:Release", "platforms:Windows"})
    buildoptions({
      "/Os",
      "/O1"
    })
  filter {}

  includedirs({
    "libcurl/lib",
    "libcurl/include",

    -- "wolfssl",
    -- "wolfssl/src",
    -- "wolfssl/wolfssl",
    -- "wolfssl/wolfssl/openssl",
    -- "wolfssl/wolfssl/wolfcrypt",
  })
  files({
    "libcurl/lib/**.h",
    "libcurl/lib/**.c",
  })
