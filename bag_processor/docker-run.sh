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

# Find the jetson directory (should be 1 level up)
JETSON_DIR=$(realpath "$CURRENT_DIR/..")
echo "Jetson directory: $JETSON_DIR"

# Check if any bag file exists in jetson directory
BAG_FILE=$(find "$JETSON_DIR" -name "*.bag" -type f | head -1)
if [ -z "$BAG_FILE" ]; then
    echo "❌ No .bag file found in: $JETSON_DIR"
    echo ""
    echo "Available files in jetson directory:"
    ls -la "$JETSON_DIR" 2>/dev/null || echo "Directory not accessible"
    echo ""
    echo "Please place any .bag file in the jetson directory:"
    echo "  jetson/"
    echo "  ├── your_bag_file.bag  ← Place any .bag file here"
    echo "  └── bag_processor/"
    echo "      └── docker-run.sh  ← You are here"
    exit 1
fi

echo "✅ Found bag file: $BAG_FILE"
echo "File size: $(du -h "$BAG_FILE" | cut -f1)"

# The output directory will be created inside the container with timestamp
echo "Output will be created in: $CURRENT_DIR/extracted_images_YYYYMMDD_HHMMSS"

# Run the Docker container
echo ""
echo "🚀 Starting Docker container..."
echo "This will mount the jetson directory and extract images with timestamp..."

docker run \
    --rm \
    --platform linux/$(uname -m | sed 's/x86_64/amd64/') \
    -v "$JETSON_DIR:/workspace/jetson" \
    -v "$CURRENT_DIR:/workspace/build/output" \
    -w /workspace/build \
    bag-processor:latest

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Container finished successfully!"
    
    # Find the timestamped directory that was created
    TIMESTAMP_DIR=$(find "$CURRENT_DIR" -name "extracted_images_*" -type d | head -1)
    if [ -n "$TIMESTAMP_DIR" ]; then
        echo "Check extracted images in: $TIMESTAMP_DIR"
        echo ""
        echo "Image count per camera:"
        find "$TIMESTAMP_DIR" -name "*.jpg" | cut -d'/' -f2 | sort | uniq -c 2>/dev/null || echo "No images found or directory structure different"
    else
        echo "Timestamped directory not found in $CURRENT_DIR"
        ls -la "$CURRENT_DIR"/extracted_images_* 2>/dev/null || echo "No extracted_images_* directories found"
    fi
else
    echo "❌ Container failed!"
    exit 1
fi