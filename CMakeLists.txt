idf_component_register(
        SRCS
            "AppFramework.cpp"
            "SimpleWebServer.cpp"
            "SimpleWebServer.h"
            "captdns.cpp"
            "captdns.h"

        INCLUDE_DIRS
            "."
            "./include"
        REQUIRES
            cpp_utils
            console-cpp
            esp_https_server
            esp-api-server
            spiffs
    )
