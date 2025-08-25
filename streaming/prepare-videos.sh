#!/bin/bash

echo "🎬 Preparing video files for Docker build..."

# Find the latest extracted_images directory
LATEST_DIR=$(ls -td ../bag_processor/extracted_images_* 2>/dev/null | head -1)

if [ -z "$LATEST_DIR" ]; then
    echo "❌ No extracted_images_* directory found in ../bag_processor/"
    echo "Please ensure bag_processor directory exists with extracted videos"
    exit 1
fi

echo "📁 Found latest directory: $LATEST_DIR"

# Create videos directory in streaming folder
mkdir -p ./videos

# Copy all MP4 files to videos directory
echo "📹 Copying video files..."
cp -v "$LATEST_DIR"/*.mp4 ./videos/ 2>/dev/null || {
    echo "⚠️ No MP4 files found in $LATEST_DIR"
}

# List copied files
echo ""
echo "✅ Video files ready for Docker build:"
ls -lh ./videos/*.mp4 2>/dev/null || echo "No video files found"

echo ""
echo "Ready to build Docker image with embedded videos!"