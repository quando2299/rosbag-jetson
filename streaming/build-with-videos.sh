#!/bin/bash

echo "🎬 Building Docker image with embedded videos"
echo "=============================================="

# Step 1: Prepare videos
echo ""
echo "Step 1: Preparing video files..."
./prepare-videos.sh

if [ $? -ne 0 ]; then
    echo "❌ Failed to prepare videos"
    exit 1
fi

# Step 2: Build Docker image
echo ""
echo "Step 2: Building Docker image..."
./docker-build.sh

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Docker image built successfully with embedded videos!"
    echo ""
    echo "To run the container:"
    echo "  ./docker-run.sh"
else
    echo "❌ Docker build failed"
    exit 1
fi