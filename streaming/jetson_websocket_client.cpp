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
#include <dirent.h>
#include <sys/stat.h>
#include <functional>

extern "C" {
#include "mongoose.h"
}

// Global variables
std::atomic<bool> g_running(true);
std::atomic<bool> g_streaming(false);
std::atomic<bool> g_connected(false);
std::thread g_streaming_thread;
std::string g_video_source;
std::vector<std::string> g_image_files;

std::mutex g_connection_mutex;
mg_connection* g_websocket_client = nullptr;
std::string g_server_url = "ws://localhost:8080/ws";

void signal_handler(int signal) {
    std::cout << "Shutting down Jetson WebSocket client..." << std::endl;
    g_running.store(false);
    g_streaming.store(false);
}

// Get all JPEG images recursively
std::vector<std::string> getAllImageFiles(const std::string& directory) {
    std::vector<std::string> image_files;
    
    std::function<void(const std::string&)> scanDirectory = [&](const std::string& dir) {
        DIR *d = opendir(dir.c_str());
        if (d == nullptr) return;
        
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            
            std::string full_path = dir + "/" + name;
            struct stat statbuf;
            
            if (stat(full_path.c_str(), &statbuf) == 0) {
                if (S_ISDIR(statbuf.st_mode)) {
                    scanDirectory(full_path);
                } else if (name.length() > 4 && name.substr(name.length() - 4) == ".jpg") {
                    image_files.push_back(full_path);
                }
            }
        }
        closedir(d);
    };
    
    scanDirectory(directory);
    std::sort(image_files.begin(), image_files.end());
    return image_files;
}

// Send image to WebSocket server
void sendImageToServer(const std::string& image_path) {
    std::lock_guard<std::mutex> lock(g_connection_mutex);
    
    if (!g_websocket_client || !g_connected.load()) {
        std::cout << "⚠️ Not connected to server, skipping image: " << image_path << std::endl;
        return;
    }
    
    // Read image file
    std::ifstream file(image_path, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Cannot read image: " << image_path << std::endl;
        return;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read image data
    std::vector<char> image_data(file_size);
    file.read(image_data.data(), file_size);
    file.close();
    
    // Send image metadata first
    std::string metadata = "{"
        "\"type\":\"image_data\","
        "\"from\":\"jetson_client\","
        "\"size\":" + std::to_string(file_size) + ","
        "\"format\":\"jpeg\","
        "\"timestamp\":\"" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) + "\","
        "\"source\":\"" + image_path + "\""
    "}";
    
    // Send metadata
    mg_ws_send(g_websocket_client, metadata.c_str(), metadata.length(), WEBSOCKET_OP_TEXT);
    
    // Send binary image data
    mg_ws_send(g_websocket_client, image_data.data(), image_data.size(), WEBSOCKET_OP_BINARY);
    
    std::cout << "📤 Sent image: " << image_path << " (" << file_size << " bytes)" << std::endl;
}

// Stream images to WebSocket server
void streamImagesToServer() {
    if (g_image_files.empty()) {
        std::cout << "❌ No images to stream!" << std::endl;
        return;
    }
    
    std::cout << "📸 Starting image stream to server: " << g_image_files.size() << " images" << std::endl;
    
    auto frame_duration = std::chrono::milliseconds(33); // 30 FPS (33ms per frame)
    size_t image_index = 0;
    
    while (g_streaming.load() && g_running.load()) {
        if (!g_connected.load()) {
            std::cout << "⚠️ Not connected, pausing stream..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        
        std::string image_path = g_image_files[image_index];
        sendImageToServer(image_path);
        
        image_index = (image_index + 1) % g_image_files.size();
        
        // Log progress every 10 images
        if (image_index % 10 == 0) {
            std::cout << "📊 Streamed " << image_index << "/" << g_image_files.size() << " images" << std::endl;
        }
        
        std::this_thread::sleep_for(frame_duration);
    }
    
    std::cout << "✅ Image streaming stopped" << std::endl;
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
        
        // Register as Jetson client
        std::string register_msg = "{"
            "\"type\":\"client_type\","
            "\"clientType\":\"jetson\""
        "}";
        
        mg_ws_send(c, register_msg.c_str(), register_msg.length(), WEBSOCKET_OP_TEXT);
        std::cout << "📤 Registered as Jetson client" << std::endl;
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        
        std::cout << "📨 Received from server: " << message << std::endl;
        
        // Handle server commands
        if (message.find("\"registration_success\"") != std::string::npos) {
            std::cout << "✅ Successfully registered with server" << std::endl;
            // Small delay to ensure registration is processed
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } else if (message.find("\"start_streaming\"") != std::string::npos) {
            std::cout << "🚀 Server requested to start streaming" << std::endl;
            if (!g_streaming.load()) {
                g_streaming.store(true);
                g_streaming_thread = std::thread(streamImagesToServer);
            }
            
        } else if (message.find("\"stop_streaming\"") != std::string::npos) {
            std::cout << "🛑 Server requested to stop streaming" << std::endl;
            g_streaming.store(false);
            if (g_streaming_thread.joinable()) {
                g_streaming_thread.join();
            }
            
        } else if (message.find("\"ping\"") != std::string::npos) {
            // Respond to ping
            mg_ws_send(c, "{\"type\":\"pong\"}", 16, WEBSOCKET_OP_TEXT);
        }
        
    } else if (ev == MG_EV_CLOSE) {
        std::lock_guard<std::mutex> lock(g_connection_mutex);
        g_websocket_client = nullptr;
        g_connected.store(false);
        
        std::cout << "❌ Disconnected from WebSocket server" << std::endl;
        
        // Stop streaming if running
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
    
    std::cout << "🚀 Jetson WebSocket Client" << std::endl;
    std::cout << "=========================" << std::endl;
    
    // Parse arguments
    g_video_source = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613";
    
    if (argc > 1) g_server_url = argv[1];
    if (argc > 2) g_video_source = argv[2];
    
    std::cout << "🌐 WebSocket Server: " << g_server_url << std::endl;
    std::cout << "📁 Image source: " << g_video_source << std::endl;
    std::cout << "=========================" << std::endl;
    
    // Load images
    g_image_files = getAllImageFiles(g_video_source);
    std::cout << "📸 Found " << g_image_files.size() << " JPEG images" << std::endl;
    
    if (g_image_files.empty()) {
        std::cerr << "❌ No images found in directory!" << std::endl;
        return 1;
    }
    
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
    
    // Auto-start streaming after 5 seconds if connected (allow time for registration)
    std::thread auto_start_thread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_connected.load() && !g_streaming.load()) {
            std::cout << "🚀 Auto-starting image stream..." << std::endl;
            g_streaming.store(true);
            g_streaming_thread = std::thread(streamImagesToServer);
        }
    });
    
    std::cout << std::endl;
    std::cout << "💡 Instructions:" << std::endl;
    std::cout << "   • Connecting to external WebSocket server..." << std::endl;
    std::cout << "   • Will auto-start streaming in 3 seconds if connected" << std::endl;
    std::cout << "   • Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;
    std::cout << "🔍 Monitoring connection..." << std::endl;
    std::cout << "=================================" << std::endl;
    
    // Main event loop
    int connection_retry_count = 0;
    auto last_retry = std::chrono::steady_clock::now();
    
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50); // Poll every 50ms
        
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
    std::cout << "👋 Jetson WebSocket client stopped" << std::endl;
    
    return 0;
}