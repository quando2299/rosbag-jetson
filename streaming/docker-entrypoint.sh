#!/bin/bash

# Docker entrypoint script for MQTT streaming client
echo "=== MQTT Streaming Client Docker Container ==="
echo "Architecture: $(uname -m)"
echo "MQTT Broker: test.rmcs.d6-vnext.com:1883"
echo "Topic: vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/connection"

# Check if executable exists
if [ ! -f "./mqtt_client" ]; then
    echo "❌ Error: mqtt_client executable not found!"
    echo "Available files:"
    ls -la ./
    exit 1
fi

echo "✅ Executable found: ./mqtt_client"

# Check for video files
echo ""
echo "🔍 Checking for video files in container..."
echo "Contents of /workspace:"
ls -la /workspace/ || echo "Directory not found"
echo ""
echo "Contents of /workspace/bag_processor:"
ls -la /workspace/bag_processor/ || echo "Directory not found"
echo ""
echo "Looking for extracted_images directories:"
find /workspace -name "extracted_images_*" -type d 2>/dev/null || echo "No extracted_images directories found"
echo ""
echo "Looking for MP4 files:"
find /workspace -name "*.mp4" -type f 2>/dev/null || echo "No MP4 files found"
echo ""

echo ""
echo "🚀 Starting MQTT client..."
echo "This will:"
echo "  1. Connect to the MQTT broker"
echo "  2. Subscribe to the specified topic"
echo "  3. Display incoming messages with timestamps"
echo ""
echo "Press Ctrl+C to stop the client"
echo ""

# Run the MQTT client
exec "$@"