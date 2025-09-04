cmake_minimum_required(VERSION 3.10)
project(mqtt_webrtc_streaming)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)

# Find JSON library
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# Define executable
add_executable(mqtt_client
    mqtt_client.cpp
    dispatchqueue.cpp
    helpers.cpp
    fileparser.cpp
    h264fileparser.cpp
)

# Link libraries
target_link_libraries(mqtt_client
    datachannel
    mosquitto
    ${JSONCPP_LIBRARIES}
    ${OpenCV_LIBS}
    Threads::Threads
    ssl
    crypto
    nice
    srtp2
)

# Include directories
target_include_directories(mqtt_client PRIVATE 
    ${JSONCPP_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    /usr/local/include
)

# Compile definitions
target_compile_definitions(mqtt_client PRIVATE JSON_ENABLED)

# Compile options
target_compile_options(mqtt_client PRIVATE ${JSONCPP_CFLAGS})

message(STATUS "MQTT WebRTC Streaming configured")
message(STATUS "OpenCV version: ${OpenCV_VERSION}")