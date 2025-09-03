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
    
    # Find the latest timestamped directory that was created
    TIMESTAMP_DIR=$(find "$CURRENT_DIR" -name "extracted_images_*" -type d | sort -r | head -1)
    if [ -n "$TIMESTAMP_DIR" ]; then
        echo "Check extracted images in: $TIMESTAMP_DIR"
        echo ""
        echo "Image count per camera:"
        find "$TIMESTAMP_DIR" -name "*.jpg" | cut -d'/' -f2 | sort | uniq -c 2>/dev/null || echo "No images found or directory structure different"
        
        # Extract datetime from directory name
        DATETIME=$(basename "$TIMESTAMP_DIR" | sed 's/extracted_images_//')
        echo ""
        echo "🎥 Processing MP4 files to H264 format..."
        
        # Process each MP4 file found in the extracted directory
        for MP4_FILE in "$TIMESTAMP_DIR"/*.mp4; do
            # Check if the glob matched any files
            if [ ! -e "$MP4_FILE" ]; then
                echo "No MP4 files found in $TIMESTAMP_DIR"
                break
            fi
            
            # Get video name without extension
            VIDEO_NAME=$(basename "$MP4_FILE" .mp4)
            OUTPUT_DIR="h264/${DATETIME}/${VIDEO_NAME}"
            
            echo ""
            echo "Processing: $VIDEO_NAME"
            echo "  Input: $MP4_FILE"
            echo "  Output: $OUTPUT_DIR"
            
            # Run the H264 generation script
            python3 "$CURRENT_DIR/generate_h264.py" -i "$MP4_FILE" -f 30 -o "$OUTPUT_DIR"
            
            if [ $? -eq 0 ]; then
                echo "✅ Successfully processed $VIDEO_NAME"
                H264_COUNT=$(find "$CURRENT_DIR/$OUTPUT_DIR" -name "*.h264" 2>/dev/null | wc -l)
                echo "  Generated $H264_COUNT H264 samples"
            else
                echo "❌ Failed to process $VIDEO_NAME"
            fi
        done
        
        echo ""
        echo "🎉 H264 processing complete!"
    else
        echo "Timestamped directory not found in $CURRENT_DIR"
        ls -la "$CURRENT_DIR"/extracted_images_* 2>/dev/null || echo "No extracted_images_* directories found"
    fi
else
    echo "❌ Container failed!"
    exit 1
fi