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

# Add executable
add_executable(simple_video_server simple_video_server.cpp mongoose.c)

# Link libraries
target_link_libraries(simple_video_server 
    ${OpenCV_LIBS} 
    Threads::Threads
)

# Add JSON support if available
if(JSONCPP_FOUND)
    target_link_libraries(simple_video_server ${JSONCPP_LIBRARIES})
    target_include_directories(simple_video_server PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_directories(simple_video_server PRIVATE ${JSONCPP_LIBRARY_DIRS})
    message(STATUS "JSON support enabled with jsoncpp")
else()
    message(WARNING "JSON support disabled - jsoncpp not found")
endif()

# Compiler flags
target_compile_definitions(simple_video_server PRIVATE MG_ENABLE_LINES)