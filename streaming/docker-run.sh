#!/bin/bash

# Docker run script for MQTT streaming client
echo "=== Running MQTT Streaming Client Docker Container ==="

# Check if Docker image exists
if ! docker image inspect mqtt-streaming:latest >/dev/null 2>&1; then
    echo "❌ Docker image 'mqtt-streaming:latest' not found!"
    echo "Build it first with: ./docker-build.sh"
    exit 1
fi

# Detect current directory structure
CURRENT_DIR=$(pwd)
echo "Current directory: $CURRENT_DIR"

# Run the Docker container
echo ""
echo "🚀 Starting MQTT Streaming Docker container..."
echo "This will connect to: test.rmcs.d6-vnext.com:1883"
echo "Listening to topic: vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/connection"
echo ""
echo "Press Ctrl+C to stop the container"
echo ""

docker run \
    --rm \
    -it \
    --network host \
    --platform linux/$(uname -m | sed 's/x86_64/amd64/') \
    --name mqtt-streaming-client \
    mqtt-streaming:latest

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Container finished successfully!"
else
    echo "❌ Container failed!"
    exit 1
fi