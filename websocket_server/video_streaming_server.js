#!/usr/bin/env node

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const path = require('path');

// Configuration
const CONFIG = {
    PORT: process.env.PORT || 8080,
    HOST: '0.0.0.0',
    MAX_CLIENTS: 100,
    PING_INTERVAL: 30000, // 30 seconds
    LOG_LEVEL: process.env.LOG_LEVEL || 'info'
};

class VideoStreamingServer {
    constructor() {
        this.clients = new Map();
        this.jetsonClients = new Map();
        this.flutterClients = new Map();
        this.stats = {
            totalConnections: 0,
            totalMessages: 0,
            totalBytes: 0,
            startTime: new Date()
        };
        
        this.setupServer();
        this.setupSignalHandlers();
    }
    
    setupServer() {
        // Create HTTP server for health checks and static files
        this.httpServer = http.createServer((req, res) => {
            this.handleHttpRequest(req, res);
        });
        
        // Create WebSocket server
        this.wss = new WebSocket.Server({ 
            server: this.httpServer,
            path: '/ws'
        });
        
        this.wss.on('connection', (ws, req) => {
            this.handleNewConnection(ws, req);
        });
        
        // Start server
        this.httpServer.listen(CONFIG.PORT, CONFIG.HOST, () => {
            console.log('🚀 Video Streaming WebSocket Server');
            console.log('=====================================');
            console.log(`📡 WebSocket endpoint: ws://${CONFIG.HOST}:${CONFIG.PORT}/ws`);
            console.log(`🌐 HTTP endpoint: http://${CONFIG.HOST}:${CONFIG.PORT}`);
            console.log(`📊 Max clients: ${CONFIG.MAX_CLIENTS}`);
            console.log(`⏰ Started at: ${new Date().toISOString()}`);
            console.log('=====================================');
        });
        
        // Setup ping interval
        this.pingInterval = setInterval(() => {
            this.pingAllClients();
        }, CONFIG.PING_INTERVAL);
    }
    
    handleHttpRequest(req, res) {
        if (req.url === '/health') {
            this.handleHealthCheck(res);
        } else if (req.url === '/stats') {
            this.handleStats(res);
        } else if (req.url === '/') {
            this.handleHomePage(res);
        } else {
            res.writeHead(404);
            res.end('Not Found');
        }
    }
    
    handleHealthCheck(res) {
        const health = {
            status: 'healthy',
            uptime: Date.now() - this.stats.startTime.getTime(),
            clients: {
                total: this.clients.size,
                jetson: this.jetsonClients.size,
                flutter: this.flutterClients.size
            },
            memory: process.memoryUsage(),
            timestamp: new Date().toISOString()
        };
        
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(health, null, 2));
    }
    
    handleStats(res) {
        const stats = {
            ...this.stats,
            uptime: Date.now() - this.stats.startTime.getTime(),
            currentClients: {
                total: this.clients.size,
                jetson: this.jetsonClients.size,
                flutter: this.flutterClients.size
            }
        };
        
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(stats, null, 2));
    }
    
    handleHomePage(res) {
        const html = `
<!DOCTYPE html>
<html>
<head>
    <title>Video Streaming WebSocket Server</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
        h1 { color: #333; text-align: center; }
        .status { padding: 15px; margin: 20px 0; border-radius: 5px; }
        .online { background: #d4edda; color: #155724; }
        .info { background: #d1ecf1; color: #0c5460; }
        .stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin: 20px 0; }
        .stat-card { background: #f8f9fa; padding: 20px; border-radius: 5px; text-align: center; }
        .stat-number { font-size: 24px; font-weight: bold; color: #007bff; }
        .code { background: #f8f9fa; padding: 15px; border-radius: 5px; font-family: monospace; margin: 10px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎥 Video Streaming WebSocket Server</h1>
        
        <div class="status online">
            ✅ Server is running and ready for connections
        </div>
        
        <div class="stats" id="stats">
            <div class="stat-card">
                <div class="stat-number" id="totalClients">${this.clients.size}</div>
                <div>Total Clients</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="jetsonClients">${this.jetsonClients.size}</div>
                <div>Jetson Clients</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="flutterClients">${this.flutterClients.size}</div>
                <div>Flutter Clients</div>
            </div>
            <div class="stat-card">
                <div class="stat-number">${Math.floor((Date.now() - this.stats.startTime.getTime()) / 1000)}</div>
                <div>Uptime (seconds)</div>
            </div>
        </div>
        
        <div class="info">
            <h3>📡 Connection Information</h3>
            <div class="code">WebSocket URL: ws://localhost:${CONFIG.PORT}/ws</div>
            <div class="code">Health Check: http://localhost:${CONFIG.PORT}/health</div>
            <div class="code">Statistics: http://localhost:${CONFIG.PORT}/stats</div>
        </div>
        
        <div class="info">
            <h3>🔧 Usage</h3>
            <p><strong>For Jetson (C++):</strong> Connect and send image data</p>
            <p><strong>For Flutter:</strong> Connect and receive image stream</p>
            <p><strong>Message Format:</strong> JSON metadata + Binary image data</p>
        </div>
        
        <script>
            // Auto-refresh stats every 5 seconds
            setInterval(async () => {
                try {
                    const response = await fetch('/stats');
                    const stats = await response.json();
                    document.getElementById('totalClients').textContent = stats.currentClients.total;
                    document.getElementById('jetsonClients').textContent = stats.currentClients.jetson;
                    document.getElementById('flutterClients').textContent = stats.currentClients.flutter;
                } catch (e) {
                    console.error('Failed to update stats:', e);
                }
            }, 5000);
        </script>
    </div>
</body>
</html>`;
        
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(html);
    }
    
    handleNewConnection(ws, req) {
        const clientId = this.generateClientId();
        const clientInfo = {
            id: clientId,
            ws: ws,
            type: 'unknown',
            connectedAt: new Date(),
            lastPing: new Date(),
            messagesReceived: 0,
            bytesReceived: 0
        };
        
        this.clients.set(ws, clientInfo);
        this.stats.totalConnections++;
        
        console.log(`🔌 New client connected: ${clientId} (Total: ${this.clients.size})`);
        
        // Send welcome message
        this.sendMessage(ws, {
            type: 'welcome',
            clientId: clientId,
            serverTime: new Date().toISOString(),
            message: 'Connected to Video Streaming Server'
        });
        
        ws.on('message', (data) => {
            this.handleMessage(ws, data);
        });
        
        ws.on('close', () => {
            this.handleDisconnection(ws);
        });
        
        ws.on('error', (error) => {
            console.error(`❌ WebSocket error for ${clientInfo.id}:`, error.message);
            this.handleDisconnection(ws);
        });
        
        ws.on('pong', () => {
            if (this.clients.has(ws)) {
                this.clients.get(ws).lastPing = new Date();
            }
        });
    }
    
    handleMessage(ws, data) {
        const clientInfo = this.clients.get(ws);
        if (!clientInfo) return;
        
        clientInfo.messagesReceived++;
        clientInfo.bytesReceived += data.length;
        this.stats.totalMessages++;
        this.stats.totalBytes += data.length;
        
        try {
            // Try to parse as JSON first for registration messages
            if (typeof data === 'string' || (data instanceof Buffer && data.length < 1000)) {
                try {
                    const text = typeof data === 'string' ? data : data.toString();
                    const message = JSON.parse(text);
                    this.handleTextMessage(ws, message);
                    return;
                } catch (e) {
                    // Not JSON, continue to binary handling
                }
            }
            
            // Binary data - check if it's H.264, complete video, or regular image
            if (data instanceof Buffer || data instanceof Uint8Array) {
                if (clientInfo.pendingCompleteVideoMetadata) {
                    // This is complete video file data
                    const metadata = clientInfo.pendingCompleteVideoMetadata;
                    clientInfo.pendingCompleteVideoMetadata = null;
                    
                    // Broadcast complete video to Flutter clients
                    let broadcastCount = 0;
                    this.flutterClients.forEach((flutterClient) => {
                        if (flutterClient.ws.readyState === WebSocket.OPEN) {
                            // Send complete video metadata first
                            this.sendMessage(flutterClient.ws, metadata);
                            // Then send binary video data
                            flutterClient.ws.send(data);
                            broadcastCount++;
                        }
                    });
                    
                    if (broadcastCount > 0) {
                        console.log(`🎬 Broadcasted complete video (${(data.length / (1024 * 1024)).toFixed(1)} MB) to ${broadcastCount} Flutter clients`);
                    }
                } else if (clientInfo.pendingH264Metadata) {
                    // This is H.264 video chunk data
                    const metadata = clientInfo.pendingH264Metadata;
                    clientInfo.pendingH264Metadata = null;
                    
                    // Broadcast H.264 to Flutter clients
                    let broadcastCount = 0;
                    this.flutterClients.forEach((flutterClient) => {
                        if (flutterClient.ws.readyState === WebSocket.OPEN) {
                            // Send H.264 metadata first
                            this.sendMessage(flutterClient.ws, metadata);
                            // Then send binary H.264 data
                            flutterClient.ws.send(data);
                            broadcastCount++;
                        }
                    });
                    
                    if (broadcastCount > 0) {
                        console.log(`🎬 Broadcasted H.264 chunk #${metadata.chunk} (${data.length} bytes) to ${broadcastCount} Flutter clients`);
                    }
                } else {
                    // Regular image data
                    this.handleImageData(ws, data);
                }
            }
        } catch (error) {
            console.error(`❌ Error processing message from ${clientInfo.id}:`, error.message);
        }
    }
    
    handleTextMessage(ws, message) {
        const clientInfo = this.clients.get(ws);
        
        console.log(`📨 Text message from ${clientInfo.id}:`, JSON.stringify(message));
        
        switch (message.type) {
            case 'client_type':
                console.log(`🔧 Processing client_type registration for ${clientInfo.id}:`, message.clientType);
                this.handleClientTypeRegistration(ws, message);
                break;
                
            case 'ping':
                this.sendMessage(ws, { type: 'pong', timestamp: new Date().toISOString() });
                break;
                
            case 'request_stream':
                this.handleStreamRequest(ws, message);
                break;
                
            case 'stop_stream':
                this.handleStopStream(ws, message);
                break;
                
            case 'h264_chunk':
            case 'h264_frame':
                // Handle H.264 video chunks/frames
                this.handleH264Data(ws, message);
                break;
                
            case 'complete_video':
                // Handle complete video file metadata
                this.handleCompleteVideoMetadata(ws, message);
                break;
                
            default:
                console.log(`📨 Message from ${clientInfo.id}:`, message);
        }
    }
    
    handleClientTypeRegistration(ws, message) {
        const clientInfo = this.clients.get(ws);
        
        // Don't override if already registered
        if (clientInfo.type !== 'unknown') {
            console.log(`⚠️ Client ${clientInfo.id} already registered as ${clientInfo.type}, ignoring re-registration as ${message.clientType}`);
            return;
        }
        
        clientInfo.type = message.clientType;
        
        if (message.clientType === 'jetson') {
            this.jetsonClients.set(ws, clientInfo);
            console.log(`🤖 Jetson client registered: ${clientInfo.id}`);
        } else if (message.clientType === 'flutter') {
            this.flutterClients.set(ws, clientInfo);
            console.log(`📱 Flutter client registered: ${clientInfo.id}`);
        }
        
        this.sendMessage(ws, {
            type: 'registration_success',
            clientType: message.clientType,
            message: `Registered as ${message.clientType} client`
        });
    }
    
    handleImageData(ws, imageData) {
        const clientInfo = this.clients.get(ws);
        
        // Allow any client to send images for now (FIX THE FUCKING REGISTRATION LATER)
        console.log(`📤 Broadcasting image from ${clientInfo.id} (${imageData.length} bytes)`);
        
        // Auto-register as jetson if sending images
        if (clientInfo.type === 'unknown') {
            clientInfo.type = 'jetson';
            this.jetsonClients.set(ws, clientInfo);
        }
        
        // Broadcast image to all Flutter clients
        const imageMessage = {
            type: 'image_frame',
            from: clientInfo.id,
            timestamp: new Date().toISOString(),
            size: imageData.length
        };
        
        let broadcastCount = 0;
        this.flutterClients.forEach((flutterClient) => {
            if (flutterClient.ws.readyState === WebSocket.OPEN) {
                // Send metadata first
                this.sendMessage(flutterClient.ws, imageMessage);
                // Then send binary data
                flutterClient.ws.send(imageData);
                broadcastCount++;
            }
        });
        
        if (broadcastCount > 0) {
            console.log(`📤 Broadcasted image (${imageData.length} bytes) from ${clientInfo.id} to ${broadcastCount} Flutter clients`);
        }
    }
    
    handleH264Data(ws, metadata) {
        const clientInfo = this.clients.get(ws);
        
        console.log(`🎬 H.264 data from ${clientInfo.id}:`, metadata.format, 'chunk:', metadata.chunk);
        
        // Auto-register as jetson if sending H.264
        if (clientInfo.type === 'unknown') {
            clientInfo.type = 'jetson';
            this.jetsonClients.set(ws, clientInfo);
        }
        
        // Store metadata for next binary data
        clientInfo.pendingH264Metadata = metadata;
    }
    
    handleCompleteVideoMetadata(ws, metadata) {
        const clientInfo = this.clients.get(ws);
        
        console.log(`🎬 Complete video from ${clientInfo.id}: ${metadata.filename} (${(metadata.size / (1024 * 1024)).toFixed(1)} MB)`);
        
        // Auto-register as jetson if sending complete video
        if (clientInfo.type === 'unknown') {
            clientInfo.type = 'jetson';
            this.jetsonClients.set(ws, clientInfo);
        }
        
        // Store metadata for next binary data
        clientInfo.pendingCompleteVideoMetadata = metadata;
    }
    
    handleStreamRequest(ws, message) {
        // Flutter client requesting to start receiving stream
        const clientInfo = this.clients.get(ws);
        console.log(`📺 Stream requested by ${clientInfo.id}`);
        
        // Notify all Jetson clients to start streaming
        this.jetsonClients.forEach((jetsonClient) => {
            if (jetsonClient.ws.readyState === WebSocket.OPEN) {
                this.sendMessage(jetsonClient.ws, {
                    type: 'start_streaming',
                    requestedBy: clientInfo.id,
                    timestamp: new Date().toISOString()
                });
            }
        });
    }
    
    handleStopStream(ws, message) {
        // Flutter client requesting to stop receiving stream
        const clientInfo = this.clients.get(ws);
        console.log(`⏹️ Stream stop requested by ${clientInfo.id}`);
        
        // Notify all Jetson clients to stop streaming
        this.jetsonClients.forEach((jetsonClient) => {
            if (jetsonClient.ws.readyState === WebSocket.OPEN) {
                this.sendMessage(jetsonClient.ws, {
                    type: 'stop_streaming',
                    requestedBy: clientInfo.id,
                    timestamp: new Date().toISOString()
                });
            }
        });
    }
    
    handleDisconnection(ws) {
        const clientInfo = this.clients.get(ws);
        if (!clientInfo) return;
        
        console.log(`❌ Client disconnected: ${clientInfo.id} (Type: ${clientInfo.type})`);
        
        // Remove from all maps
        this.clients.delete(ws);
        this.jetsonClients.delete(ws);
        this.flutterClients.delete(ws);
    }
    
    pingAllClients() {
        let activeClients = 0;
        let deadConnections = [];
        
        this.clients.forEach((clientInfo, ws) => {
            if (ws.readyState === WebSocket.OPEN) {
                ws.ping();
                activeClients++;
            } else {
                deadConnections.push(ws);
            }
        });
        
        // Clean up dead connections
        deadConnections.forEach(ws => this.handleDisconnection(ws));
        
        if (CONFIG.LOG_LEVEL === 'debug') {
            console.log(`💓 Pinged ${activeClients} clients, cleaned up ${deadConnections.length} dead connections`);
        }
    }
    
    sendMessage(ws, message) {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify(message));
        }
    }
    
    generateClientId() {
        return `client_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
    }
    
    setupSignalHandlers() {
        process.on('SIGINT', () => {
            console.log('\\n🛑 Shutting down WebSocket server...');
            this.shutdown();
        });
        
        process.on('SIGTERM', () => {
            console.log('\\n🛑 Received SIGTERM, shutting down...');
            this.shutdown();
        });
    }
    
    shutdown() {
        clearInterval(this.pingInterval);
        
        // Close all WebSocket connections
        this.clients.forEach((clientInfo, ws) => {
            this.sendMessage(ws, {
                type: 'server_shutdown',
                message: 'Server is shutting down',
                timestamp: new Date().toISOString()
            });
            ws.close();
        });
        
        // Close HTTP server
        this.httpServer.close(() => {
            console.log('👋 Video Streaming WebSocket Server stopped');
            process.exit(0);
        });
    }
}

// Start the server
new VideoStreamingServer();