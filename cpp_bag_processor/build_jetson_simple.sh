#!/bin/bash

# Simple build script for Jetson - manual dependency check
echo "=== Simple Jetson Build Script ==="

# Clean previous build
echo "Cleaning previous build..."
rm -rf build
mkdir -p build
cd build

echo "Installing basic dependencies..."
sudo apt update
sudo apt install -y build-essential cmake pkg-config

# Install pthread
sudo apt install -y libc6-dev

# Try to install ROS packages
echo "Installing ROS packages..."
sudo apt install -y \
    ros-melodic-rosbag \
    ros-melodic-sensor-msgs \
    ros-melodic-cv-bridge \
    ros-melodic-roscpp \
    ros-melodic-cpp-common \
    libopencv-dev \
    libboost-all-dev

# Source ROS
if [ -f "/opt/ros/melodic/setup.bash" ]; then
    echo "Sourcing ROS Melodic..."
    source /opt/ros/melodic/setup.bash
    export ROS_PACKAGE_PATH=/opt/ros/melodic/share
elif [ -f "/opt/ros/noetic/setup.bash" ]; then
    echo "Sourcing ROS Noetic..."
    source /opt/ros/noetic/setup.bash
    export ROS_PACKAGE_PATH=/opt/ros/noetic/share
else
    echo "❌ No ROS found! Please install ROS first:"
    echo "  sudo apt install ros-melodic-desktop-full"
    exit 1
fi

# Configure with explicit pthread linking
echo "Configuring with CMake..."
cmake .. \
    -DUSE_ROS=ON \
    -DCMAKE_CXX_FLAGS="-pthread" \
    -DCMAKE_EXE_LINKER_FLAGS="-pthread" \
    -DBoost_USE_STATIC_LIBS=OFF \
    -DBoost_USE_MULTITHREADED=ON

# Build with verbose output
echo "Building..."
make VERBOSE=1

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "Executable created: ./bag_processor"
    
    # Test if bag file exists
    if [ -f "../../camera_data_2025-07-08-16-29-06_0.bag" ]; then
        echo "✅ Bag file found!"
        echo ""
        echo "🚀 Ready to run:"
        echo "  ./bag_processor"
    else
        echo "❌ Bag file not found at ../../camera_data_2025-07-08-16-29-06_0.bag"
        echo "Make sure the bag file is in the correct location"
    fi
else
    echo "❌ Build failed!"
    echo ""
    echo "Troubleshooting steps:"
    echo "1. Check CMakeOutput.log and CMakeError.log"
    echo "2. Ensure all ROS packages are installed:"
    echo "   sudo apt install ros-melodic-desktop-full"
    echo "3. Try building with standalone mode:"
    echo "   cmake .. -DUSE_ROS=OFF"
    exit 1
fi