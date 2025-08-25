#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <fstream>
#include <mutex>

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
    std::cout << "Shutting down Video Stream client..." << std::endl;
    g_running.store(false);
    g_streaming.store(false);
}

// Stream entire video file
void streamVideoFile() {
    std::cout << "🎬 Streaming complete video file: " << g_video_file << std::endl;
    
    std::ifstream video_file(g_video_file, std::ios::binary | std::ios::ate);
    if (!video_file) {
        std::cerr << "❌ Cannot open video file: " << g_video_file << std::endl;
        return;
    }
    
    // Get file size
    size_t file_size = video_file.tellg();
    video_file.seekg(0, std::ios::beg);
    
    std::cout << "📹 Video file size: " << file_size / 1024 << " KB" << std::endl;
    
    // Read entire file
    std::vector<char> video_data(file_size);
    video_file.read(video_data.data(), file_size);
    video_file.close();
    
    while (g_streaming.load() && g_running.load()) {
        if (!g_connected.load()) {
            std::cout << "⚠️ Not connected, waiting..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        
        if (g_websocket_client && g_connected.load()) {
            // Send video metadata
            std::string metadata = "{"
                "\"type\":\"video_stream\","
                "\"format\":\"mp4\","
                "\"codec\":\"h264\","
                "\"size\":" + std::to_string(file_size) + ","
                "\"filename\":\"" + g_video_file + "\","
                "\"timestamp\":\"" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()) + "\""
            "}";
            
            std::cout << "📤 Sending video metadata..." << std::endl;
            mg_ws_send(g_websocket_client, metadata.c_str(), metadata.length(), WEBSOCKET_OP_TEXT);
            
            // Small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::cout << "📤 Sending complete video file (" << file_size << " bytes)..." << std::endl;
            mg_ws_send(g_websocket_client, video_data.data(), video_data.size(), WEBSOCKET_OP_BINARY);
            
            std::cout << "✅ Video file sent successfully!" << std::endl;
        }
        
        // Wait 10 seconds before looping
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    std::cout << "✅ Video streaming stopped" << std::endl;
}

// WebSocket event handler
static void websocket_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_WS_OPEN) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = c;
        g_connected.store(true);
        
        std::cout << "✅ Connected to WebSocket server" << std::endl;
        
        // Register as video streaming client
        std::string register_msg = "{"
            "\"type\":\"client_type\","
            "\"clientType\":\"jetson\","
            "\"streamType\":\"video\""
        "}";
        
        mg_ws_send(c, register_msg.c_str(), register_msg.length(), WEBSOCKET_OP_TEXT);
        std::cout << "📤 Registered as video streaming client" << std::endl;
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        
        if (message.find("\"start_streaming\"") != std::string::npos) {
            std::cout << "🚀 Starting video stream..." << std::endl;
            if (!g_streaming.load()) {
                g_streaming.store(true);
                g_streaming_thread = std::thread(streamVideoFile);
            }
        }
        
    } else if (ev == MG_EV_CLOSE) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = nullptr;
        g_connected.store(false);
        std::cout << "❌ Disconnected from server" << std::endl;
    }
}

int main() {
    signal(SIGINT, signal_handler);
    
    g_video_file = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613/leopard_id4_image_resized_30fps.mp4";
    
    std::cout << "🚀 Video Stream Client" << std::endl;
    std::cout << "🎬 Video: " << g_video_file << std::endl;
    std::cout << "=======================" << std::endl;
    
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    mg_ws_connect(&mgr, g_server_url.c_str(), websocket_event_handler, nullptr, nullptr);
    
    // Auto-start after connection
    std::thread auto_start([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (g_connected.load()) {
            g_streaming.store(true);
            g_streaming_thread = std::thread(streamVideoFile);
        }
    });
    
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50);
    }
    
    // Cleanup
    g_streaming.store(false);
    if (g_streaming_thread.joinable()) g_streaming_thread.join();
    if (auto_start.joinable()) auto_start.join();
    
    mg_mgr_free(&mgr);
    return 0;
}