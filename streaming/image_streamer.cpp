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

extern "C" {
#include "mongoose.h"
}

std::atomic<bool> g_running(true);
std::atomic<bool> g_streaming(false);
std::thread g_streaming_thread;
std::string g_images_dir;
std::vector<std::string> g_image_files;

std::mutex g_clients_mutex;
std::map<mg_connection*, std::string> g_clients;

void signal_handler(int signal) {
    std::cout << "Shutting down..." << std::endl;
    g_running.store(false);
    g_streaming.store(false);
}

std::vector<std::string> getImageFiles(const std::string& directory) {
    std::vector<std::string> image_files;
    
    DIR *dir = opendir(directory.c_str());
    if (dir == nullptr) {
        std::cerr << "Cannot open directory: " << directory << std::endl;
        return image_files;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".jpg") {
            image_files.push_back(directory + "/" + filename);
        }
    }
    closedir(dir);
    
    std::sort(image_files.begin(), image_files.end());
    std::cout << "Found " << image_files.size() << " JPEG images" << std::endl;
    
    return image_files;
}

void streamImages() {
    std::cout << "Starting image stream from: " << g_images_dir << std::endl;
    
    auto frame_duration = std::chrono::milliseconds(100); // 10 FPS
    size_t image_index = 0;
    
    while (g_streaming.load()) {
        if (g_image_files.empty()) {
            std::this_thread::sleep_for(frame_duration);
            continue;
        }
        
        std::string image_path = g_image_files[image_index];
        
        // Read image file
        std::ifstream file(image_path, std::ios::binary);
        if (!file) {
            std::cerr << "Cannot read: " << image_path << std::endl;
            image_index = (image_index + 1) % g_image_files.size();
            continue;
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read file data
        std::vector<char> image_data(file_size);
        file.read(image_data.data(), file_size);
        file.close();
        
        // Send to all clients
        {
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            for (auto& client : g_clients) {
                mg_ws_send(client.first, image_data.data(), image_data.size(), WEBSOCKET_OP_BINARY);
            }
            
            if (image_index % 50 == 0) {
                std::cout << "Sent image " << image_index << " to " << g_clients.size() << " clients" << std::endl;
            }
        }
        
        image_index = (image_index + 1) % g_image_files.size();
        std::this_thread::sleep_for(frame_duration);
    }
}

static void event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        
        if (mg_match(hm->uri, mg_str("/ws"), nullptr)) {
            mg_ws_upgrade(c, hm, nullptr);
            
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            std::string client_id = "client_" + std::to_string(reinterpret_cast<uintptr_t>(c));
            g_clients[c] = client_id;
            
            std::cout << "WebSocket client connected: " << client_id << std::endl;
            
            std::string welcome = "{\"type\":\"welcome\",\"clientId\":\"" + client_id + "\"}";
            mg_ws_send(c, welcome.c_str(), welcome.length(), WEBSOCKET_OP_TEXT);
            
        } else {
            struct mg_http_serve_opts opts = {};
            opts.root_dir = ".";
            mg_http_serve_dir(c, hm, &opts);
        }
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        
        if (message.find("\"start_stream\"") != std::string::npos) {
            std::cout << "Starting image stream..." << std::endl;
            
            if (!g_streaming.load()) {
                g_streaming.store(true);
                g_streaming_thread = std::thread(streamImages);
            }
            
        } else if (message.find("\"stop_stream\"") != std::string::npos) {
            std::cout << "Stopping image stream..." << std::endl;
            g_streaming.store(false);
            
            if (g_streaming_thread.joinable()) {
                g_streaming_thread.join();
            }
        }
        
    } else if (ev == MG_EV_CLOSE) {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        auto it = g_clients.find(c);
        if (it != g_clients.end()) {
            std::cout << "Client disconnected: " << it->second << std::endl;
            g_clients.erase(it);
        }
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    
    int port = 8080;
    g_images_dir = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613/flir_id8_image_resized";
    
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) g_images_dir = argv[2];
    
    std::cout << "WebSocket Image Streamer" << std::endl;
    std::cout << "Images: " << g_images_dir << std::endl;
    std::cout << "Port: " << port << std::endl;
    
    g_image_files = getImageFiles(g_images_dir);
    if (g_image_files.empty()) {
        std::cerr << "No images found!" << std::endl;
        return 1;
    }
    
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    std::string listen_addr = "http://0.0.0.0:" + std::to_string(port);
    struct mg_connection *c = mg_http_listen(&mgr, listen_addr.c_str(), event_handler, nullptr);
    
    if (c == nullptr) {
        std::cerr << "Failed to create listener" << std::endl;
        return 1;
    }
    
    std::cout << "Server started: " << listen_addr << std::endl;
    std::cout << "Open: http://localhost:" << port << "/viewer.html" << std::endl;
    
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50);
    }
    
    g_streaming.store(false);
    if (g_streaming_thread.joinable()) {
        g_streaming_thread.join();
    }
    
    mg_mgr_free(&mgr);
    return 0;
}