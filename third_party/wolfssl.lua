group("third_party")
project("wolfssl")
  uuid("a34e3e3f-f08b-4718-8f96-cfc347f07931")
  kind("StaticLib")
  language("C")
  links({

  })
  defines({
    "WOLFSSL_LIB",
    "WOLFSSL_USER_SETTINGS"
  })
  filter({"configurations:Release", "platforms:Windows"})
    buildoptions({
      "/Os",
      "/O1",
    })
  filter {}

  includedirs({
    "wolfssl",
    "wolfssl/IDE/WIN"
  })
  files({
    "wolfcrypt/src/*.c",

    "wolfssl/src/crl.c",
    "wolfssl/src/dtls.c",
    "wolfssl/src/dtls13.c",
    "wolfssl/src/internal.c",
    "wolfssl/src/keys.c",
    "wolfssl/src/ocsp.c",
    "wolfssl/src/ssl.c",
    "wolfssl/src/tls.c",
    "wolfssl/src/tls13.c",
    "wolfssl/src/wolfio.c",
  })
