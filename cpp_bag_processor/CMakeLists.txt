cmake_minimum_required(VERSION 3.10)
project(bag_processor)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add pthread support
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# Option to build with or without ROS
option(USE_ROS "Build with ROS support" ON)

if(USE_ROS)
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
    add_executable(bag_processor bag_processor.cpp)
    
    # Link ROS libraries
    target_link_libraries(bag_processor
        ${catkin_LIBRARIES}
        ${OpenCV_LIBS}
        ${Boost_LIBRARIES}
        Threads::Threads
    )
    
    # Define ROS compilation flag
    target_compile_definitions(bag_processor PRIVATE HAVE_ROS=1)
    
    # Install
    install(TARGETS bag_processor
        RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
    )
    
else()
    # Standalone build without ROS
    find_package(OpenCV)
    
    # Add standalone executable
    add_executable(bag_processor standalone_bag_processor.cpp)
    
    if(OpenCV_FOUND)
        target_compile_definitions(bag_processor PRIVATE HAVE_OPENCV=1)
        target_link_libraries(bag_processor ${OpenCV_LIBS})
        include_directories(${OpenCV_INCLUDE_DIRS})
    endif()
    
    target_link_libraries(bag_processor Threads::Threads)
    
endif()

# Also build standalone version always
add_executable(standalone_bag_processor standalone_bag_processor.cpp)
target_link_libraries(standalone_bag_processor Threads::Threads)