#!/bin/bash

# GStreamer WebRTC Docker Run Script
# This runs the GStreamer WebRTC container with proper video access

echo "🎬 Starting GStreamer WebRTC container..."
echo "=========================================="

# Check if container is already running
if [ "$(docker ps -q -f name=jetson-gstreamer-webrtc)" ]; then
    echo "⚠️  Container 'jetson-gstreamer-webrtc' is already running. Stopping it..."
    docker stop jetson-gstreamer-webrtc
    docker rm jetson-gstreamer-webrtc
fi

# Run the container with:
# - Video device access (for camera if needed)
# - Network host mode for WebRTC
# - Volume mount for video files
# - Environment variables for MQTT broker
# - Interactive mode with TTY for live logs
docker run -it \
    --rm \
    --name jetson-gstreamer-webrtc \
    --network host \
    --privileged \
    -v /dev:/dev \
    -v $(pwd)/videos:/app/videos:ro \
    -v $(pwd):/app/source:ro \
    -e MQTT_BROKER=${MQTT_BROKER:-test.rmcs.d6-vnext.com} \
    -e MQTT_PORT=${MQTT_PORT:-1883} \
    -e STUN_SERVER=${STUN_SERVER:-stun:stun.l.google.com:19302} \
    -e VIDEO_FILE=${VIDEO_FILE:-/app/videos/flir_id8_image_resized_30fps.mp4} \
    jetson-gstreamer-webrtc:latest

# The container will run interactively, showing logs directly
# Press Ctrl+C to stop the container