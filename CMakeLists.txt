idf_component_register(
        SRCS
            "AppFramework.cpp"
            "SimpleWebServer.cpp"
            "SimpleWebServer.h"

        INCLUDE_DIRS
            "."
        REQUIRES
            cpp_utils
            console-cpp
            esp_https_server
            esp-api-server
    )
