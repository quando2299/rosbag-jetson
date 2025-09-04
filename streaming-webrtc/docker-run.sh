#!/bin/bash

echo "=== Running MQTT WebRTC Streaming Docker Container ==="

# Check if Docker image exists
if ! docker image inspect mqtt-webrtc-streaming:latest >/dev/null 2>&1; then
    echo "❌ Docker image 'mqtt-webrtc-streaming:latest' not found!"
    echo "Build it first with: ./docker-build.sh"
    exit 1
fi

# Get Jetson directory
CURRENT_DIR=$(pwd)
JETSON_DIR=$(realpath "$CURRENT_DIR/..")
echo "Current directory: $CURRENT_DIR"
echo "Jetson directory: $JETSON_DIR"

# Check H264 files
H264_DIR="$JETSON_DIR/bag_processor/h264/20250903_092730/leopard_id1_image_resized_30fps"
if [ -d "$H264_DIR" ]; then
    H264_COUNT=$(find "$H264_DIR" -name "*.h264" -type f 2>/dev/null | wc -l)
    echo "✅ Found $H264_COUNT H264 files in target directory"
else
    echo "⚠️  H264 directory not found: $H264_DIR"
    echo "   Please run bag_processor first to generate H264 files"
fi

echo ""
echo "Starting container..."
echo "  MQTT Broker: test.rmcs.d6-vnext.com:1883"
echo "  Topics:"
echo "    - Offer: vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/robot-control/+/offer"
echo "    - ICE: vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/robot-control/+/candidate/robot"
echo ""
echo "Press Ctrl+C to stop the container"
echo ""

# Run Docker container
docker run \
    --rm \
    --name mqtt-webrtc-streaming \
    --network host \
    --platform linux/$(uname -m | sed 's/x86_64/amd64/') \
    -v "$JETSON_DIR:/jetson:ro" \
    mqtt-webrtc-streaming:latest

if [ $? -eq 0 ]; then
    echo "✅ Container stopped successfully"
else
    echo "❌ Container failed"
    exit 1
fi