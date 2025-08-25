cmake_minimum_required(VERSION 3.10)
project(simple_video_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Find Threads
find_package(Threads REQUIRED)

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

# Enable WebRTC support
add_definitions(-DWEBRTC_ENABLED)

# Enable JSON support
add_definitions(-DJSON_ENABLED)

# Add executable for MQTT client
add_executable(mqtt_client mqtt_client.cpp webrtc_manager.cpp)

# Link libraries
target_link_libraries(mqtt_client 
    ${OpenCV_LIBS} 
    Threads::Threads
    mosquitto
    datachannel
)

# Add JSON support if available
if(JSONCPP_FOUND)
    target_link_libraries(mqtt_client ${JSONCPP_LIBRARIES})
    target_include_directories(mqtt_client PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_directories(mqtt_client PRIVATE ${JSONCPP_LIBRARY_DIRS})
    message(STATUS "JSON support enabled with jsoncpp")
else()
    message(WARNING "JSON support disabled - jsoncpp not found")
endif()