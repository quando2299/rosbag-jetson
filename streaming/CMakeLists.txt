cmake_minimum_required(VERSION 3.10)
project(mqtt_streaming)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

# Find JSON library
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(JSONCPP QUIET jsoncpp)
endif()

# Fallback for JSON library
if(NOT JSONCPP_FOUND)
    find_package(PkgConfig QUIET)
    pkg_check_modules(JSONCPP QUIET libjsoncpp)
endif()

# Find libdatachannel (WebRTC library)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(DATACHANNEL QUIET libdatachannel)
endif()

# If libdatachannel not found via pkg-config, try find_package
if(NOT DATACHANNEL_FOUND)
    find_package(LibDataChannel QUIET)
    if(LibDataChannel_FOUND)
        set(DATACHANNEL_LIBRARIES LibDataChannel::LibDataChannel)
        set(DATACHANNEL_FOUND TRUE)
    endif()
endif()

add_executable(mqtt_client mqtt_client.cpp webrtc_manager.cpp)

target_link_libraries(mqtt_client ${MOSQUITTO_LIBRARIES})
target_include_directories(mqtt_client PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
target_compile_options(mqtt_client PRIVATE ${MOSQUITTO_CFLAGS_OTHER})
target_link_directories(mqtt_client PRIVATE ${MOSQUITTO_LIBRARY_DIRS})

# Add JSON support if available
if(JSONCPP_FOUND)
    target_link_libraries(mqtt_client ${JSONCPP_LIBRARIES})
    target_include_directories(mqtt_client PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_directories(mqtt_client PRIVATE ${JSONCPP_LIBRARY_DIRS})
    target_compile_definitions(mqtt_client PRIVATE JSON_ENABLED)
    message(STATUS "JSON support enabled with jsoncpp")
else()
    target_compile_definitions(mqtt_client PRIVATE JSON_DISABLED)
    message(WARNING "JSON support disabled - jsoncpp not found")
endif()

# Add WebRTC support if available
if(DATACHANNEL_FOUND)
    target_link_libraries(mqtt_client ${DATACHANNEL_LIBRARIES})
    target_include_directories(mqtt_client PRIVATE ${DATACHANNEL_INCLUDE_DIRS})
    target_compile_definitions(mqtt_client PRIVATE WEBRTC_ENABLED)
    message(STATUS "WebRTC support enabled with libdatachannel")
else()
    target_compile_definitions(mqtt_client PRIVATE WEBRTC_DISABLED)
    message(WARNING "WebRTC support disabled - libdatachannel not found")
endif()