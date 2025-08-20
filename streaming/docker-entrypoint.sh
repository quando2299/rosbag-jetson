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