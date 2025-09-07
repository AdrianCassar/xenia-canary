group("third_party")
project("libcurl")
  uuid("1ba7e608-5752-457c-8df0-c006c6e8b7fe")
  kind("StaticLib")
  language("C")
  defines({
    "BUILDING_LIBCURL",
    "HTTP_ONLY",

    -- "USE_WOLFSSL",
    -- "WITHOUT_SSL",
    -- "OPENSSL_EXTRA",
  })

  filter { "system:linux" }
     defines {
      "HAVE_RECV",
      "HAVE_SEND",
      "SIZEOF_LONG=8",
      "SIZEOF_SIZE_T=8",
      "SIZEOF_CURL_OFF_T=8",
      "USE_THREADS_POSIX",
      "HAVE_NETINET_IN_H",
      "HAVE_NETDB_H",
      "HAVE_STRUCT_TIMEVAL",
      "HAVE_FCNTL_O_NONBLOCK",
      "HAVE_FCNTL",
      "HAVE_FCNTL_H",
      "HAVE_SELECT",
      "HAVE_LONGLONG",
      "HAVE_SOCKET",
      'CURL_OS="x86_64-pc-linux-gnu"',
    }
    links({

    })

  filter { "system:windows" }
    defines({
      "USE_SCHANNEL",
      "USE_WINDOWS_SSPI",
    })
    
    links({
      "crypt32",
      "secur32",
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
