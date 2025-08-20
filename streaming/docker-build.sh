#!/bin/bash

# Docker build script for MQTT streaming client
echo "=== Building MQTT Streaming Client Docker Image ==="

# Detect architecture
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

# Set platform for multi-arch support
if [ "$ARCH" = "x86_64" ]; then
    PLATFORM="linux/amd64"
    TAG_SUFFIX="amd64"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    PLATFORM="linux/arm64"
    TAG_SUFFIX="arm64"
else
    echo "❌ Unsupported architecture: $ARCH"
    exit 1
fi

echo "Building for platform: $PLATFORM"

# Build the Docker image
echo "Building Docker image..."
docker build \
    --platform $PLATFORM \
    -t mqtt-streaming:$TAG_SUFFIX \
    -t mqtt-streaming:latest \
    .

if [ $? -eq 0 ]; then
    echo "✅ Docker image built successfully!"
    echo "Image tags:"
    echo "  - mqtt-streaming:$TAG_SUFFIX"
    echo "  - mqtt-streaming:latest"
    echo ""
    echo "To run the container:"
    echo "  ./docker-run.sh"
else
    echo "❌ Docker build failed!"
    exit 1
fi