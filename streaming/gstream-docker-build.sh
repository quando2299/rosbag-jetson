#!/bin/bash

# GStreamer WebRTC Docker Build Script
# This builds a Docker image with GStreamer and WebRTC support

echo "🚀 Building GStreamer WebRTC Docker image..."
echo "=========================================="

# Build the Docker image with GStreamer support
docker build \
    -f Dockerfile.gstreamer \
    -t jetson-gstreamer-webrtc:latest \
    --build-arg BUILDKIT_INLINE_CACHE=1 \
    .

if [ $? -eq 0 ]; then
    echo "✅ GStreamer WebRTC Docker image built successfully!"
    echo "Image name: jetson-gstreamer-webrtc:latest"
else
    echo "❌ Failed to build GStreamer WebRTC Docker image"
    exit 1
fi