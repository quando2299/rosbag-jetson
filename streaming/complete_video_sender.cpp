#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <mutex>

extern "C" {
#include "mongoose.h"
}

std::atomic<bool> g_running(true);
std::atomic<bool> g_connected(false);
std::mutex g_connection_mutex;
mg_connection* g_websocket_client = nullptr;
std::string g_server_url = "ws://localhost:8080/ws";

void signal_handler(int signal) {
    std::cout << "Shutting down..." << std::endl;
    g_running.store(false);
}

void sendCompleteVideoFile() {
    std::string video_file = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613/flir_id8_image_resized_30fps.mp4";
    
    std::ifstream file(video_file, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "❌ Cannot open video file: " << video_file << std::endl;
        return;
    }
    
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> video_data(file_size);
    file.read(video_data.data(), file_size);
    file.close();
    
    std::cout << "🎬 Sending COMPLETE H.264 video file: " << file_size << " bytes" << std::endl;
    
    std::lock_guard<std::mutex> lock(g_connection_mutex);
    
    if (g_websocket_client && g_connected.load()) {
        // Send video file metadata
        std::string metadata = "{"
            "\"type\":\"complete_video\","
            "\"format\":\"mp4\","
            "\"codec\":\"h264\","
            "\"filename\":\"" + video_file + "\","
            "\"size\":" + std::to_string(file_size) + ","
            "\"timestamp\":\"" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) + "\""
        "}";
        
        // Send metadata
        mg_ws_send(g_websocket_client, metadata.c_str(), metadata.length(), WEBSOCKET_OP_TEXT);
        std::cout << "📤 Sent video metadata" << std::endl;
        
        // Send complete video file as binary
        mg_ws_send(g_websocket_client, video_data.data(), video_data.size(), WEBSOCKET_OP_BINARY);
        std::cout << "✅ Sent COMPLETE H.264 video file!" << std::endl;
    }
}

static void websocket_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_WS_OPEN) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = c;
        g_connected.store(true);
        
        std::cout << "✅ Connected to WebSocket server" << std::endl;
        
        // Register as video sender
        std::string register_msg = "{"
            "\"type\":\"client_type\","
            "\"clientType\":\"jetson\","
            "\"streamType\":\"complete_video\""
        "}";
        
        mg_ws_send(c, register_msg.c_str(), register_msg.length(), WEBSOCKET_OP_TEXT);
        std::cout << "📤 Registered as complete video sender" << std::endl;
        
        // Don't auto-send - wait for stream request
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        std::cout << "📨 Server: " << message << std::endl;
        
        // Send video when requested
        if (message.find("\"start_streaming\"") != std::string::npos) {
            std::cout << "🚀 Server requested video - sending now!" << std::endl;
            sendCompleteVideoFile();
        }
        
    } else if (ev == MG_EV_CLOSE) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = nullptr;
        g_connected.store(false);
        std::cout << "❌ Disconnected" << std::endl;
    }
}

int main() {
    signal(SIGINT, signal_handler);
    
    std::cout << "🚀 Complete H.264 Video Sender" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << "📹 Will send COMPLETE MP4/H.264 video file" << std::endl;
    std::cout << "==============================" << std::endl;
    
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    mg_ws_connect(&mgr, g_server_url.c_str(), websocket_event_handler, nullptr, nullptr);
    
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50);
    }
    
    mg_mgr_free(&mgr);
    return 0;
}