#!/bin/bash

# Build script for Jetson with ROS support
echo "=== Building Bag Processor for Jetson ==="

# Check if ROS is sourced
if [ -z "$ROS_DISTRO" ]; then
    echo "ROS environment not detected. Sourcing ROS..."
    
    # Try different ROS distributions
    if [ -f "/opt/ros/melodic/setup.bash" ]; then
        source /opt/ros/melodic/setup.bash
        echo "Sourced ROS Melodic"
    elif [ -f "/opt/ros/noetic/setup.bash" ]; then
        source /opt/ros/noetic/setup.bash
        echo "Sourced ROS Noetic"
    elif [ -f "/opt/ros/kinetic/setup.bash" ]; then
        source /opt/ros/kinetic/setup.bash
        echo "Sourced ROS Kinetic"
    else
        echo "Warning: No ROS installation found!"
        echo "Install ROS first:"
        echo "  sudo apt update"
        echo "  sudo apt install ros-melodic-desktop-full"
        echo "  sudo apt install ros-melodic-rosbag ros-melodic-sensor-msgs ros-melodic-cv-bridge"
        exit 1
    fi
else
    echo "ROS $ROS_DISTRO environment detected"
fi

# Install dependencies if not present
echo "Checking dependencies..."

# Check for rosbag
if ! dpkg -l | grep -q "ros-$ROS_DISTRO-rosbag"; then
    echo "Installing ROS packages..."
    sudo apt update
    sudo apt install -y \
        ros-$ROS_DISTRO-rosbag \
        ros-$ROS_DISTRO-sensor-msgs \
        ros-$ROS_DISTRO-cv-bridge \
        ros-$ROS_DISTRO-roscpp
fi

# Check for OpenCV
if ! pkg-config --exists opencv; then
    echo "Installing OpenCV..."
    sudo apt install -y libopencv-dev
fi

# Check for Boost
if ! dpkg -l | grep -q "libboost-dev"; then
    echo "Installing Boost..."
    sudo apt install -y libboost-all-dev
fi

# Create build directory
echo "Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DUSE_ROS=ON

# Build
echo "Building..."
make -j$(nproc) VERBOSE=1

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "Run with: ./bag_processor"
    
    # Check if bag file exists
    if [ -f "../../camera_data_2025-07-08-16-29-06_0.bag" ]; then
        echo "✅ Bag file found: ../../camera_data_2025-07-08-16-29-06_0.bag"
    else
        echo "❌ Bag file not found. Make sure camera_data_2025-07-08-16-29-06_0.bag is in the m2m folder"
    fi
else
    echo "❌ Build failed!"
    exit 1
fi