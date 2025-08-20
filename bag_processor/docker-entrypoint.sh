#!/bin/bash

# Docker entrypoint script for ROS bag processor
echo "=== ROS Bag Processor Docker Container ==="
echo "Architecture: $(uname -m)"
echo "ROS Distribution: $ROS_DISTRO"

# Source ROS environment
source /opt/ros/melodic/setup.bash

# Check if any bag file exists in jetson directory
BAG_FILE=$(find /workspace/jetson -name "*.bag" -type f | head -1)
if [ -z "$BAG_FILE" ]; then
    echo "❌ Error: No .bag file found in /workspace/jetson/"
    echo ""
    echo "Mount the bag file with:"
    echo "  -v /path/to/your/jetson:/workspace/jetson"
    echo ""
    echo "Available files in jetson directory:"
    ls -la /workspace/jetson/ || echo "jetson directory not mounted"
    exit 1
fi

echo "✅ Bag file found: $BAG_FILE"
echo "File size: $(du -h "$BAG_FILE" | cut -f1)"

# Check if executable exists
if [ ! -f "./rosbag_analyzed" ]; then
    echo "❌ Error: rosbag_analyzed executable not found!"
    echo "Available files:"
    ls -la ./
    exit 1
fi

echo "✅ Executable found: ./rosbag_analyzed"
echo ""
echo "🚀 Starting bag processing..."
echo "This will:"
echo "  1. Analyze the bag file structure"
echo "  2. Extract all images to timestamped folder extracted_images_YYYYMMDD_HHMMSS/"
echo "  3. Organize by camera topic"
echo ""

# Run the bag processor
exec "$@"