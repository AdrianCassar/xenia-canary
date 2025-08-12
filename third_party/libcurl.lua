group("third_party")
project("libcurl")
  uuid("1ba7e608-5752-457c-8df0-c006c6e8b7fe")
  kind("StaticLib")
  language("C")
  links({
    "crypt32",
    "secur32",
  })
  defines({
    "BUILDING_LIBCURL",
    "HTTP_ONLY",
    "USE_SCHANNEL",
    "USE_WINDOWS_SSPI",

    -- "USE_WOLFSSL",
    -- "WITHOUT_SSL",
    -- "OPENSSL_EXTRA",
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
