cmake_minimum_required(VERSION 3.10)
project(rosbag_analyzed)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add pthread support
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# Find ROS packages
find_package(catkin REQUIRED COMPONENTS
    rosbag
    sensor_msgs
    cv_bridge
    roscpp
)

find_package(OpenCV REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem thread)

# Include directories
include_directories(
    ${catkin_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# Add executable with ROS support
add_executable(rosbag_analyzed rosbag_analyzed.cpp)

# Link ROS libraries
target_link_libraries(rosbag_analyzed
    ${catkin_LIBRARIES}
    ${OpenCV_LIBS}
    ${Boost_LIBRARIES}
    Threads::Threads
)

# Define ROS compilation flag
target_compile_definitions(rosbag_analyzed PRIVATE HAVE_ROS=1)

# No install needed for Docker build