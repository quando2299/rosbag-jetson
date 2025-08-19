#!/bin/bash

# Docker run script for ROS bag processor
echo "=== Running ROS Bag Processor Docker Container ==="

# Check if Docker image exists
if ! docker image inspect bag-processor:latest >/dev/null 2>&1; then
    echo "❌ Docker image 'bag-processor:latest' not found!"
    echo "Build it first with: ./docker-build.sh"
    exit 1
fi

# Detect current directory structure
CURRENT_DIR=$(pwd)
echo "Current directory: $CURRENT_DIR"

# Find the m2m directory (should be 2 levels up)
M2M_DIR=$(realpath "$CURRENT_DIR/../..")
echo "M2M directory: $M2M_DIR"

# Check if bag file exists
BAG_FILE="$M2M_DIR/camera_data_2025-07-08-16-29-06_0.bag"
if [ ! -f "$BAG_FILE" ]; then
    echo "❌ Bag file not found at: $BAG_FILE"
    echo ""
    echo "Please ensure the bag file is in the correct location:"
    echo "  m2m/"
    echo "  ├── camera_data_2025-07-08-16-29-06_0.bag  ← Should be here"
    echo "  └── jetson/"
    echo "      └── cpp_bag_processor/"
    echo "          └── docker-run.sh  ← You are here"
    exit 1
fi

echo "✅ Found bag file: $BAG_FILE"
echo "File size: $(du -h "$BAG_FILE" | cut -f1)"

# Create output directory on host
OUTPUT_DIR="$CURRENT_DIR/cpp_extracted_images"
mkdir -p "$OUTPUT_DIR"
echo "Output directory: $OUTPUT_DIR"

# Run the Docker container
echo ""
echo "🚀 Starting Docker container..."
echo "This will mount the m2m directory and extract images..."

docker run \
    --rm \
    --platform linux/$(uname -m | sed 's/x86_64/amd64/') \
    -v "$M2M_DIR:/workspace/m2m" \
    -v "$OUTPUT_DIR:/workspace/build/cpp_extracted_images" \
    -w /workspace/build \
    bag-processor:latest

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Container finished successfully!"
    echo "Check extracted images in: $OUTPUT_DIR"
    echo ""
    echo "Image count per camera:"
    find "$OUTPUT_DIR" -name "*.jpg" | cut -d'/' -f2 | sort | uniq -c
else
    echo "❌ Container failed!"
    exit 1
fi