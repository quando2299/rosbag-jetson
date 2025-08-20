cmake_minimum_required(VERSION 3.10)
project(mqtt_streaming)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

add_executable(mqtt_client mqtt_client.cpp)

target_link_libraries(mqtt_client ${MOSQUITTO_LIBRARIES})
target_include_directories(mqtt_client PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
target_compile_options(mqtt_client PRIVATE ${MOSQUITTO_CFLAGS_OTHER})
target_link_directories(mqtt_client PRIVATE ${MOSQUITTO_LIBRARY_DIRS})