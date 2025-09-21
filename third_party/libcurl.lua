group("third_party")
project("libcurl")
  uuid("1ba7e608-5752-457c-8df0-c006c6e8b7fe")
  kind("StaticLib")
  language("C")
  links({
    "wolfssl"
  })
  defines({
    "BUILDING_LIBCURL",
    "HTTP_ONLY",
    "USE_WOLFSSL",
  })

  filter {}
  postbuildcommands {
    "{DELETE} %[wolfssl/options.h]"
  }
  includedirs({
    "libcurl/lib",
    "libcurl/include",
    "wolfssl",
 
    -- "wolfssl/src",
    -- "wolfssl/wolfssl",
    -- "wolfssl/wolfssl/openssl",
    -- "wolfssl/wolfssl/wolfcrypt",
  })
  files({
    "libcurl/lib/**.h",
    "libcurl/lib/**.c",
  })
