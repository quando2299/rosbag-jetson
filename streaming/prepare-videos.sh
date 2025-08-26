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

# Copy image files directly to videos directory (not subdirectories)
echo "🖼️ Copying image files..."
for img_dir in "$LATEST_DIR"/*/; do
    if [ -d "$img_dir" ]; then
        dir_name=$(basename "$img_dir")
        echo "Copying images from: $dir_name"
        # Copy all JPG files from each subdirectory directly to videos/
        cp "$img_dir"/*.jpg "./videos/" 2>/dev/null || echo "No .jpg files in $dir_name"
        cp "$img_dir"/*.jpeg "./videos/" 2>/dev/null || true
        cp "$img_dir"/*.JPG "./videos/" 2>/dev/null || true
        cp "$img_dir"/*.JPEG "./videos/" 2>/dev/null || true
    fi
done

# List copied files
echo ""
echo "✅ Video files ready for Docker build:"
ls -lh ./videos/*.mp4 2>/dev/null || echo "No video files found"

echo ""
echo "✅ Image files ready for Docker build:"
ls -1 ./videos/*.jpg 2>/dev/null | head -5 || echo "No .jpg image files found"
echo "Total image files: $(ls -1 ./videos/*.jpg 2>/dev/null | wc -l) JPG files"

echo ""
echo "Ready to build Docker image with embedded videos and images!"