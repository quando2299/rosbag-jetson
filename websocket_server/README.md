# Jetson Video Streaming WebSocket Server

A dedicated WebSocket server for real-time video streaming from Jetson devices to Flutter applications.

## Features

- 🎥 **Real-time video streaming** from Jetson to Flutter
- 🔌 **WebSocket-based** for low latency
- 📊 **Built-in monitoring** and health checks
- 🤖 **Multi-client support** (multiple Jetson + Flutter clients)
- 💓 **Connection health monitoring** with ping/pong
- 📈 **Statistics dashboard** and API endpoints

## Architecture

```
Jetson Device --[images]--> WebSocket Server --[broadcast]--> Flutter Apps
     |                            |                               |
     +---- Register as Jetson ----+                               |
                                  +---- Register as Flutter ------+
```

## Quick Start

### 1. Install Dependencies
```bash
npm install
```

### 2. Start Server
```bash
npm start
# or
node video_streaming_server.js
```

### 3. Server Endpoints
- **WebSocket**: `ws://localhost:8080/ws`
- **Health Check**: `http://localhost:8080/health`
- **Statistics**: `http://localhost:8080/stats`
- **Dashboard**: `http://localhost:8080/`

## Client Integration

### For Jetson (C++)
1. Connect to `ws://server:8080/ws`
2. Register client type:
   ```json
   {"type": "client_type", "clientType": "jetson"}
   ```
3. Send binary image data directly

### For Flutter
1. Connect to `ws://server:8080/ws`
2. Register client type:
   ```json
   {"type": "client_type", "clientType": "flutter"}
   ```
3. Request stream:
   ```json
   {"type": "request_stream"}
   ```
4. Receive image frames as binary data

## Message Protocol

### Text Messages (JSON)
- `welcome`: Server welcome message
- `client_type`: Client registration
- `ping`/`pong`: Keep-alive
- `request_stream`: Start streaming
- `stop_stream`: Stop streaming
- `start_streaming`: Server command to Jetson
- `image_frame`: Metadata for binary image

### Binary Messages
- Raw image data (JPEG format)

## Configuration

Environment variables:
- `PORT`: Server port (default: 8080)
- `LOG_LEVEL`: Logging level (info/debug)

## Deployment

### Local Development
```bash
npm start
```

### Production (PM2)
```bash
npm install -g pm2
pm2 start video_streaming_server.js --name jetson-video-server
pm2 save
pm2 startup
```

### Docker
```dockerfile
FROM node:18-alpine
WORKDIR /app
COPY package*.json ./
RUN npm ci --only=production
COPY . .
EXPOSE 8080
CMD ["node", "video_streaming_server.js"]
```

## Monitoring

### Health Check
```bash
curl http://localhost:8080/health
```

### Statistics
```bash
curl http://localhost:8080/stats
```

### Dashboard
Open `http://localhost:8080/` in browser for real-time dashboard.

## Performance

- Supports 100+ concurrent clients
- Real-time image broadcasting
- Memory-efficient binary data handling
- Automatic connection cleanup
- Health monitoring with ping/pong

## Troubleshooting

### Connection Issues
- Check WebSocket URL includes `/ws` path
- Verify client registration message
- Check server logs for error messages

### Performance Issues
- Monitor `/stats` endpoint
- Check network bandwidth
- Adjust image compression on Jetson side

## License

MIT License - see LICENSE file for details.