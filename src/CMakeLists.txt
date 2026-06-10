idf_component_register(SRCS "main.cpp" "wifi_manager.cpp" "algod_client.cpp"
                                              "algorand.cpp" "algorand_codec.cpp"
                                              "ed25519/add_scalar.c"
                                              "ed25519/fe.c"
                                              "ed25519/ge.c"
                                              "ed25519/keypair.c"
                                              "ed25519/sc.c"
                                              "ed25519/sha512.c"
                                              "ed25519/sign.c"
                       INCLUDE_DIRS "../include" "ed25519"
                       REQUIRES esp_http_client json mbedtls)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ED25519_NO_SEED)
