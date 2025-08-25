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
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
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
    std::cout << "Shutting down H.264 WebSocket client..." << std::endl;
    g_running.store(false);
    g_streaming.store(false);
}

// Stream H.264 video to WebSocket server
void streamH264ToServer() {
    std::cout << "📹 Starting H.264 video stream: " << g_video_file << std::endl;
    
    // Initialize FFmpeg
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, g_video_file.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "❌ Cannot open video file: " << g_video_file << std::endl;
        return;
    }
    
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "❌ Cannot find stream info" << std::endl;
        avformat_close_input(&format_ctx);
        return;
    }
    
    // Find video stream
    int video_stream_idx = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    
    if (video_stream_idx == -1) {
        std::cerr << "❌ No video stream found" << std::endl;
        avformat_close_input(&format_ctx);
        return;
    }
    
    AVStream* video_stream = format_ctx->streams[video_stream_idx];
    std::cout << "📺 Video info: " << video_stream->codecpar->width << "x" << video_stream->codecpar->height << std::endl;
    std::cout << "🎬 Codec: " << avcodec_get_name(video_stream->codecpar->codec_id) << std::endl;
    
    AVPacket packet;
    int frame_count = 0;
    auto frame_duration = std::chrono::milliseconds(33); // 30 FPS
    
    while (g_streaming.load() && g_running.load()) {
        if (!g_connected.load()) {
            std::cout << "⚠️ Not connected, pausing stream..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        
        if (av_read_frame(format_ctx, &packet) >= 0) {
            if (packet.stream_index == video_stream_idx) {
                std::lock_guard<std::mutex> lock(g_connection_mutex);
                
                if (g_websocket_client && g_connected.load()) {
                    // Send H.264 frame metadata
                    std::string metadata = "{"
                        "\"type\":\"h264_frame\","
                        "\"from\":\"jetson_h264\","
                        "\"size\":" + std::to_string(packet.size) + ","
                        "\"pts\":" + std::to_string(packet.pts) + ","
                        "\"dts\":" + std::to_string(packet.dts) + ","
                        "\"key_frame\":" + (packet.flags & AV_PKT_FLAG_KEY ? "true" : "false") + ","
                        "\"frame_number\":" + std::to_string(frame_count) + ","
                        "\"format\":\"h264\","
                        "\"timestamp\":\"" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()) + "\""
                    "}";
                    
                    // Send metadata
                    mg_ws_send(g_websocket_client, metadata.c_str(), metadata.length(), WEBSOCKET_OP_TEXT);
                    
                    // Send H.264 packet data
                    mg_ws_send(g_websocket_client, packet.data, packet.size, WEBSOCKET_OP_BINARY);
                    
                    frame_count++;
                    if (frame_count % 30 == 0) {
                        std::cout << "📤 Sent H.264 frame #" << frame_count << " (" << packet.size << " bytes)" << std::endl;
                    }
                }
            }
            av_packet_unref(&packet);
            std::this_thread::sleep_for(frame_duration);
        } else {
            // Loop video
            av_seek_frame(format_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
            frame_count = 0;
            std::cout << "🔄 Looping video..." << std::endl;
        }
    }
    
    avformat_close_input(&format_ctx);
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
            "\"streamType\":\"h264\""
        "}";
        
        mg_ws_send(c, register_msg.c_str(), register_msg.length(), WEBSOCKET_OP_TEXT);
        std::cout << "📤 Registered as Jetson H.264 client" << std::endl;
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        std::string message(wm->data.buf, wm->data.len);
        
        std::cout << "📨 Received from server: " << message << std::endl;
        
        if (message.find("\"start_streaming\"") != std::string::npos) {
            std::cout << "🚀 Server requested to start H.264 streaming" << std::endl;
            if (!g_streaming.load()) {
                g_streaming.store(true);
                g_streaming_thread = std::thread(streamH264ToServer);
            }
        } else if (message.find("\"stop_streaming\"") != std::string::npos) {
            std::cout << "🛑 Server requested to stop streaming" << std::endl;
            g_streaming.store(false);
            if (g_streaming_thread.joinable()) {
                g_streaming_thread.join();
            }
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
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "🚀 Jetson H.264 WebSocket Client" << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Default to first MP4 file
    g_video_file = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613/flir_id8_image_resized_30fps.mp4";
    
    if (argc > 1) g_server_url = argv[1];
    if (argc > 2) g_video_file = argv[2];
    
    std::cout << "🌐 WebSocket Server: " << g_server_url << std::endl;
    std::cout << "📹 H.264 Video: " << g_video_file << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Check if video file exists
    struct stat st;
    if (stat(g_video_file.c_str(), &st) != 0) {
        std::cerr << "❌ Video file not found: " << g_video_file << std::endl;
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
    
    // Auto-start streaming after connection
    std::thread auto_start_thread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (g_connected.load() && !g_streaming.load()) {
            std::cout << "🚀 Auto-starting H.264 stream..." << std::endl;
            g_streaming.store(true);
            g_streaming_thread = std::thread(streamH264ToServer);
        }
    });
    
    std::cout << std::endl;
    std::cout << "💡 Streaming H.264 video format" << std::endl;
    std::cout << "   • Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;
    
    // Main event loop
    while (g_running.load()) {
        mg_mgr_poll(&mgr, 50);
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
    std::cout << "👋 H.264 WebSocket client stopped" << std::endl;
    
    return 0;
}