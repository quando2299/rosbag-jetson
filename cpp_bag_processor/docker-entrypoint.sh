#!/bin/bash

# Docker entrypoint script for ROS bag processor
echo "=== ROS Bag Processor Docker Container ==="
echo "Architecture: $(uname -m)"
echo "ROS Distribution: $ROS_DISTRO"

# Source ROS environment
source /opt/ros/melodic/setup.bash

# Check if bag file exists
BAG_FILE="/workspace/m2m/camera_data_2025-07-08-16-29-06_0.bag"
if [ ! -f "$BAG_FILE" ]; then
    echo "❌ Error: Bag file not found!"
    echo "Expected location: $BAG_FILE"
    echo ""
    echo "Mount the bag file with:"
    echo "  -v /path/to/your/m2m:/workspace/m2m"
    echo ""
    echo "Available files in m2m directory:"
    ls -la /workspace/m2m/ || echo "m2m directory not mounted"
    echo ""
    echo "Available files in workspace:"
    ls -la /workspace/
    exit 1
fi

echo "✅ Bag file found: $BAG_FILE"
echo "File size: $(du -h "$BAG_FILE" | cut -f1)"

# Check if executable exists
if [ ! -f "./bag_processor" ]; then
    echo "❌ Error: bag_processor executable not found!"
    echo "Available files:"
    ls -la ./
    exit 1
fi

echo "✅ Executable found: ./bag_processor"
echo ""
echo "🚀 Starting bag processing..."
echo "This will:"
echo "  1. Analyze the bag file structure"
echo "  2. Extract all images to cpp_extracted_images/"
echo "  3. Organize by camera topic"
echo ""

# Run the bag processor
exec "$@"