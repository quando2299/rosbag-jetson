#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <fstream>
#include <map>
#include <mutex>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <functional>
// #include <opencv2/opencv.hpp>  // OpenCV not available, fallback to images

extern "C" {
#include "mongoose.h"
}

// Global variables
std::atomic<bool> g_running(true);
std::atomic<bool> g_streaming(false);
std::thread g_streaming_thread;
std::string g_video_source;
std::vector<std::string> g_video_files;
std::vector<std::string> g_image_files;

std::mutex g_clients_mutex;
std::map<mg_connection*, std::string> g_clients;
std::atomic<int> g_ping_counter(0);

void signal_handler(int signal) {
    std::cout << "Shutting down Jetson video streamer..." << std::endl;
    g_running.store(false);
    g_streaming.store(false);
}

// Get video files
std::vector<std::string> getVideoFiles(const std::string& directory) {
    std::vector<std::string> video_files;
    
    DIR *dir = opendir(directory.c_str());
    if (dir == nullptr) return video_files;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".mp4") {
            video_files.push_back(directory + "/" + filename);
        }
    }
    closedir(dir);
    
    std::sort(video_files.begin(), video_files.end());
    return video_files;
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

// Forward declaration
void streamImages();

// Stream video using fallback to images (OpenCV not available)
void streamVideo() {
    if (g_video_files.empty()) {
        std::cout << "No video files found, using images..." << std::endl;
        streamImages();
        return;
    }
    
    std::cout << "🎬 Video files found but OpenCV not available, falling back to images..." << std::endl;
    streamImages();
}

// Stream images (fallback)
void streamImages() {
    if (g_image_files.empty()) {
        std::cout << "❌ No images to stream!" << std::endl;
        return;
    }
    
    std::cout << "📸 Streaming images: " << g_image_files.size() << " total" << std::endl;
    
    auto frame_duration = std::chrono::milliseconds(50); // 20 FPS to prevent disconnects
    size_t image_index = 0;
    
    while (g_streaming.load()) {
        std::string image_path = g_image_files[image_index];
        
        // Read image file
        std::ifstream file(image_path, std::ios::binary);
        if (file) {
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            std::vector<char> image_data(file_size);
            file.read(image_data.data(), file_size);
            file.close();
            
            // Send to all clients with error handling
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            auto it = g_clients.begin();
            while (it != g_clients.end()) {
                mg_connection* client = it->first;
                
                // Check if connection is still alive
                if (client->is_closing) {
                    std::cout << "⚠️ Removing dead client: " << it->second << std::endl;
                    it = g_clients.erase(it);
                    continue;
                }
                
                // Send data - reduce frame rate if image is too large
                if (image_data.size() > 100000) {  // Skip large images to prevent buffer overflow
                    std::cout << "⚠️ Skipping large image (" << image_data.size() << " bytes)" << std::endl;
                } else {
                    mg_ws_send(client, image_data.data(), image_data.size(), WEBSOCKET_OP_BINARY);
                }
                ++it;
            }
            
            if (image_index % 90 == 0) {
                std::cout << "📤 Sent image " << image_index << "/" << g_image_files.size() 
                          << " to " << g_clients.size() << " clients" << std::endl;
            }
        }
        
        image_index = (image_index + 1) % g_image_files.size();
        
        // Send periodic ping to keep connections alive
        g_ping_counter++;
        if (g_ping_counter % 100 == 0) {  // Every ~5 seconds
            std::lock_guard<std::mutex> ping_lock(g_clients_mutex);
            for (auto& client : g_clients) {
                mg_ws_send(client.first, "{\"type\":\"ping\"}", 16, WEBSOCKET_OP_TEXT);
            }
        }
        
        std::this_thread::sleep_for(frame_duration);
    }
}

// Mongoose event handler
static void event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        
        if (mg_match(hm->uri, mg_str("/ws"), nullptr)) {
            // Upgrade to WebSocket
            mg_ws_upgrade(c, hm, nullptr);
            
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            std::string client_id = "jetson_client_" + std::to_string(reinterpret_cast<uintptr_t>(c));
            g_clients[c] = client_id;
            
            std::cout << "✅ WebSocket client connected: " << client_id << std::endl;
            
            // Send welcome message
            std::string welcome = "{\"type\":\"welcome\",\"clientId\":\"" + client_id + "\",\"source\":\"jetson\"}";
            mg_ws_send(c, welcome.c_str(), welcome.length(), WEBSOCKET_OP_TEXT);
            
        } else {
            // Serve static files (including viewer.html)
            struct mg_http_serve_opts opts = {};
            opts.root_dir = ".";
            mg_http_serve_dir(c, hm, &opts);
        }
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        
        if (message.find("\"start_stream\"") != std::string::npos) {
            std::cout << "🚀 Starting Jetson video stream..." << std::endl;
            
            if (!g_streaming.load()) {
                g_streaming.store(true);
                g_streaming_thread = std::thread(streamVideo);
            }
            
        } else if (message.find("\"stop_stream\"") != std::string::npos) {
            std::cout << "🛑 Stopping Jetson video stream..." << std::endl;
            g_streaming.store(false);
            
            if (g_streaming_thread.joinable()) {
                g_streaming_thread.join();
            }
        }
        
    } else if (ev == MG_EV_CLOSE) {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        auto it = g_clients.find(c);
        if (it != g_clients.end()) {
            std::cout << "❌ Client disconnected: " << it->second << std::endl;
            g_clients.erase(it);
        }
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "🚀 Jetson Video WebSocket Streamer" << std::endl;
    std::cout << "===================================" << std::endl;
    
    int port = 8080;
    g_video_source = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613";
    
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) g_video_source = argv[2];
    
    std::cout << "📁 Video source: " << g_video_source << std::endl;
    std::cout << "🔌 WebSocket port: " << port << std::endl;
    std::cout << "📺 Streaming at: 20 FPS (stable)" << std::endl;
    std::cout << "===================================" << std::endl;
    
    // Load video files and images
    g_video_files = getVideoFiles(g_video_source);
    g_image_files = getAllImageFiles(g_video_source);
    
    std::cout << "📹 Found " << g_video_files.size() << " video files" << std::endl;
    std::cout << "📸 Found " << g_image_files.size() << " image files" << std::endl;
    
    if (g_video_files.empty() && g_image_files.empty()) {
        std::cerr << "❌ No media files found!" << std::endl;
        return 1;
    }
    
    // Start Mongoose server
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    std::string listen_addr = "http://0.0.0.0:" + std::to_string(port);
    struct mg_connection *c = mg_http_listen(&mgr, listen_addr.c_str(), event_handler, nullptr);
    
    if (c == nullptr) {
        std::cerr << "❌ Failed to start server on port " << port << std::endl;
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "✅ Jetson WebSocket Server running: " << listen_addr << std::endl;
    std::cout << "🌐 Open in browser: http://localhost:" << port << "/viewer.html" << std::endl;
    std::cout << std::endl;
    std::cout << "💡 Usage:" << std::endl;
    std::cout << "   1. Open viewer.html in browser" << std::endl;
    std::cout << "   2. Click 'Connect' button" << std::endl;
    std::cout << "   3. Click 'Start Stream' button" << std::endl;
    std::cout << "   4. Enjoy stable 20 FPS video streaming!" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    // Main loop
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50); // Poll every 50ms
    }
    
    // Cleanup
    std::cout << "🧹 Cleaning up..." << std::endl;
    g_streaming.store(false);
    
    if (g_streaming_thread.joinable()) {
        g_streaming_thread.join();
    }
    
    mg_mgr_free(&mgr);
    std::cout << "👋 Jetson video streamer stopped" << std::endl;
    
    return 0;
}