#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <sys/stat.h>

extern "C" {
#include "mongoose.h"
}

// Global variables
std::atomic<bool> g_running(true);
std::atomic<bool> g_streaming(false);
std::atomic<bool> g_connected(false);
std::thread g_streaming_thread;
std::string g_video_file;

std::mutex g_connection_mutex;
mg_connection* g_websocket_client = nullptr;
std::string g_server_url = "ws://localhost:8080/ws";

void signal_handler(int signal) {
    std::cout << "Shutting down H.264 Stream client..." << std::endl;
    g_running.store(false);
    g_streaming.store(false);
}

// Stream H.264 video file in chunks
void streamH264Video() {
    std::cout << "🎬 Starting H.264 video stream: " << g_video_file << std::endl;
    
    std::ifstream video_file(g_video_file, std::ios::binary | std::ios::ate);
    if (!video_file) {
        std::cerr << "❌ Cannot open video file: " << g_video_file << std::endl;
        return;
    }
    
    // Get file size
    size_t file_size = video_file.tellg();
    video_file.seekg(0, std::ios::beg);
    
    std::cout << "📹 Video file size: " << file_size / (1024 * 1024) << " MB" << std::endl;
    
    // Stream in smaller chunks to avoid WebSocket frame errors
    const size_t chunk_size = 8192; // 8KB chunks (reduced from 32KB)
    std::vector<char> buffer(chunk_size);
    
    int chunk_count = 0;
    auto frame_duration = std::chrono::milliseconds(100); // Slower rate: 10 FPS to prevent errors
    
    while (g_streaming.load() && g_running.load()) {
        if (!g_connected.load()) {
            std::cout << "⚠️ Not connected, pausing stream..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        
        video_file.read(buffer.data(), chunk_size);
        size_t bytes_read = video_file.gcount();
        
        if (bytes_read > 0) {
            std::lock_guard<std::mutex> lock(g_connection_mutex);
            
            if (g_websocket_client && g_connected.load()) {
                // Send H.264 chunk metadata
                std::string metadata = "{"
                    "\"type\":\"h264_chunk\","
                    "\"from\":\"jetson_h264\","
                    "\"size\":" + std::to_string(bytes_read) + ","
                    "\"chunk\":" + std::to_string(chunk_count) + ","
                    "\"format\":\"h264\","
                    "\"codec\":\"avc1.64001E\","
                    "\"timestamp\":\"" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()) + "\""
                "}";
                
                // Send metadata
                mg_ws_send(g_websocket_client, metadata.c_str(), metadata.length(), WEBSOCKET_OP_TEXT);
                
                // Small delay before binary data
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                // Send H.264 video chunk
                mg_ws_send(g_websocket_client, buffer.data(), bytes_read, WEBSOCKET_OP_BINARY);
                
                chunk_count++;
                if (chunk_count % 30 == 0) {
                    std::cout << "📤 Sent H.264 chunk #" << chunk_count << " (" << bytes_read << " bytes)" << std::endl;
                }
            }
            
            std::this_thread::sleep_for(frame_duration);
        } else {
            // Loop video
            video_file.clear();
            video_file.seekg(0, std::ios::beg);
            chunk_count = 0;
            std::cout << "🔄 Looping H.264 video..." << std::endl;
        }
    }
    
    video_file.close();
    std::cout << "✅ H.264 streaming stopped" << std::endl;
}

// WebSocket event handler
static void websocket_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_OPEN) {
        std::cout << "🔌 Attempting WebSocket connection..." << std::endl;
        
    } else if (ev == MG_EV_WS_OPEN) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = c;
        g_connected.store(true);
        
        std::cout << "✅ Connected to WebSocket server: " << g_server_url << std::endl;
        
        // Register as Jetson H.264 client
        std::string register_msg = "{"
            "\"type\":\"client_type\","
            "\"clientType\":\"jetson\","
            "\"streamFormat\":\"h264\""
        "}";
        
        mg_ws_send(c, register_msg.c_str(), register_msg.length(), WEBSOCKET_OP_TEXT);
        std::cout << "📤 Registered as Jetson H.264 streaming client" << std::endl;
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        
        std::cout << "📨 Received from server: " << message << std::endl;
        
        if (message.find("\"registration_success\"") != std::string::npos) {
            std::cout << "✅ Successfully registered with server" << std::endl;
            
        } else if (message.find("\"start_streaming\"") != std::string::npos) {
            std::cout << "🚀 Server requested to start H.264 streaming" << std::endl;
            if (!g_streaming.load()) {
                g_streaming.store(true);
                g_streaming_thread = std::thread(streamH264Video);
            }
            
        } else if (message.find("\"stop_streaming\"") != std::string::npos) {
            std::cout << "🛑 Server requested to stop streaming" << std::endl;
            g_streaming.store(false);
            if (g_streaming_thread.joinable()) {
                g_streaming_thread.join();
            }
            
        } else if (message.find("\"ping\"") != std::string::npos) {
            mg_ws_send(c, "{\"type\":\"pong\"}", 16, WEBSOCKET_OP_TEXT);
        }
        
    } else if (ev == MG_EV_CLOSE) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = nullptr;
        g_connected.store(false);
        
        std::cout << "❌ Disconnected from WebSocket server" << std::endl;
        
        g_streaming.store(false);
        if (g_streaming_thread.joinable()) {
            g_streaming_thread.join();
        }
        
    } else if (ev == MG_EV_ERROR) {
        std::cout << "❌ WebSocket connection error" << std::endl;
        g_connected.store(false);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "🚀 Jetson H.264 Stream Client" << std::endl;
    std::cout << "================================" << std::endl;
    std::cout << "📹 Streaming H.264 video format" << std::endl;
    std::cout << "================================" << std::endl;
    
    // Default to first MP4 file
    g_video_file = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613/flir_id8_image_resized_30fps.mp4";
    
    if (argc > 1) g_server_url = argv[1];
    if (argc > 2) g_video_file = argv[2];
    
    std::cout << "🌐 WebSocket Server: " << g_server_url << std::endl;
    std::cout << "🎬 H.264 Video File: " << g_video_file << std::endl;
    std::cout << "================================" << std::endl;
    
    // Check if video file exists
    struct stat st;
    if (stat(g_video_file.c_str(), &st) != 0) {
        std::cerr << "❌ Video file not found: " << g_video_file << std::endl;
        return 1;
    }
    
    std::cout << "📊 Video file info:" << std::endl;
    std::cout << "   • Size: " << st.st_size / (1024 * 1024) << " MB" << std::endl;
    std::cout << "   • Format: H.264/MP4" << std::endl;
    std::cout << "   • Streaming: 30 FPS" << std::endl;
    
    // Initialize Mongoose manager
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    // Connect to WebSocket server
    std::cout << "🔌 Connecting to: " << g_server_url << std::endl;
    struct mg_connection *c = mg_ws_connect(&mgr, g_server_url.c_str(), websocket_event_handler, nullptr, nullptr);
    
    if (c == nullptr) {
        std::cerr << "❌ Failed to initiate connection to: " << g_server_url << std::endl;
        return 1;
    }
    
    std::cout << "⏳ Waiting for connection..." << std::endl;
    
    // Auto-start streaming after connection
    std::thread auto_start_thread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (g_connected.load() && !g_streaming.load()) {
            std::cout << "🚀 Auto-starting H.264 video stream..." << std::endl;
            g_streaming.store(true);
            g_streaming_thread = std::thread(streamH264Video);
        }
    });
    
    std::cout << std::endl;
    std::cout << "💡 Instructions:" << std::endl;
    std::cout << "   • Streaming H.264 video format" << std::endl;
    std::cout << "   • Auto-starts streaming in 3 seconds" << std::endl;
    std::cout << "   • Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;
    
    // Main event loop
    int connection_retry_count = 0;
    auto last_retry = std::chrono::steady_clock::now();
    
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50);
        
        // Reconnection logic
        if (!g_connected.load() && g_running.load()) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_retry).count() >= 10) {
                connection_retry_count++;
                std::cout << "🔄 Reconnection attempt #" << connection_retry_count << "..." << std::endl;
                
                mg_ws_connect(&mgr, g_server_url.c_str(), websocket_event_handler, nullptr, nullptr);
                last_retry = now;
            }
        }
    }
    
    // Cleanup
    std::cout << "🧹 Cleaning up..." << std::endl;
    g_streaming.store(false);
    g_connected.store(false);
    
    if (g_streaming_thread.joinable()) {
        g_streaming_thread.join();
    }
    
    if (auto_start_thread.joinable()) {
        auto_start_thread.join();
    }
    
    mg_mgr_free(&mgr);
    std::cout << "👋 H.264 Stream client stopped" << std::endl;
    
    return 0;
}