#include "webrtc_manager.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstddef>

#ifdef WEBRTC_ENABLED

WebRTCManager::WebRTCManager(const std::string& thing_name, PublishCallback publish_cb) 
    : thing_name_(thing_name), publish_callback_(publish_cb) {
    std::cout << "✅ WebRTC Manager initialized with libdatachannel" << std::endl;
}

WebRTCManager::~WebRTCManager() {
    // Stop all streaming
    for (auto& [peer_id, active] : streaming_active_) {
        stopVideoStreaming(peer_id);
    }
    
    // Close all peer connections
    for (auto& [peer_id, pc] : peer_connections_) {
        if (pc) {
            pc->close();
        }
    }
    peer_connections_.clear();
    video_tracks_.clear();
    streaming_active_.clear();
    streaming_threads_.clear();
    std::cout << "🧹 WebRTC Manager cleaned up" << std::endl;
}

rtc::Configuration WebRTCManager::getRTCConfig() {
    rtc::Configuration config;
    
    // Add STUN servers
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    
    return config;
}

std::shared_ptr<rtc::PeerConnection> WebRTCManager::createPeerConnection(const std::string& peer_id) {
    auto config = getRTCConfig();
    auto pc = std::make_shared<rtc::PeerConnection>(config);
    
    // Set up connection state callback
    pc->onStateChange([this, peer_id](rtc::PeerConnection::State state) {
        std::cout << "🔗 Peer " << peer_id << " connection state: ";
        switch (state) {
            case rtc::PeerConnection::State::New:
                std::cout << "New" << std::endl;
                break;
            case rtc::PeerConnection::State::Connecting:
                std::cout << "Connecting" << std::endl;
                break;
            case rtc::PeerConnection::State::Connected:
                std::cout << "Connected" << std::endl;
                std::cout << "✅ WebRTC connection established for " << peer_id << std::endl;
                std::cout << "🎯 Ready for video streaming via WebSocket" << std::endl;
                break;
            case rtc::PeerConnection::State::Disconnected:
                std::cout << "Disconnected" << std::endl;
                break;
            case rtc::PeerConnection::State::Failed:
                std::cout << "Failed" << std::endl;
                std::cout << "❌ WebRTC connection failed for " << peer_id << " - check network connectivity" << std::endl;
                break;
            case rtc::PeerConnection::State::Closed:
                std::cout << "Closed" << std::endl;
                break;
        }
    });
    
    // Set up gathering state callback
    pc->onGatheringStateChange([peer_id](rtc::PeerConnection::GatheringState state) {
        std::cout << "🧊 Peer " << peer_id << " ICE gathering: ";
        switch (state) {
            case rtc::PeerConnection::GatheringState::New:
                std::cout << "New" << std::endl;
                break;
            case rtc::PeerConnection::GatheringState::InProgress:
                std::cout << "In Progress" << std::endl;
                break;
            case rtc::PeerConnection::GatheringState::Complete:
                std::cout << "Complete" << std::endl;
                break;
        }
    });
    
    // Video track will be added after remote description is set
    
    // Set up ICE candidate handling
    setupICEHandling(peer_id, pc);
    
    return pc;
}

void WebRTCManager::setupICEHandling(const std::string& peer_id, std::shared_ptr<rtc::PeerConnection> pc) {
    // Store local candidates for batching
    static std::map<std::string, Json::Value> localCandidates;
    
    pc->onLocalCandidate([this, peer_id](rtc::Candidate candidate) {
        std::cout << "🧊 Local ICE candidate for " << peer_id << ": " << candidate.candidate() << std::endl;
        
        // Create candidate JSON object
        Json::Value candidateJson;
        candidateJson["candidate"] = candidate.candidate();
        candidateJson["sdpMid"] = candidate.mid();
        candidateJson["sdpMLineIndex"] = 0; // Default to 0, adjust as needed
        
        // Add to local candidates array for this peer
        if (localCandidates.find(peer_id) == localCandidates.end()) {
            localCandidates[peer_id] = Json::Value(Json::arrayValue);
        }
        localCandidates[peer_id].append(candidateJson);
    });
    
    pc->onGatheringStateChange([this, peer_id](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            std::cout << "🧊 Peer " << peer_id << " ICE gathering: Complete" << std::endl;
            
            // Publish all collected local ICE candidates to /rmcs topic
            std::string rmcs_topic = thing_name_ + "/robot-control/" + peer_id + "/candidate/rmcs";
            
            // Get collected candidates for this peer
            if (localCandidates.find(peer_id) != localCandidates.end()) {
                Json::StreamWriterBuilder builder;
                std::string candidatesStr = Json::writeString(builder, localCandidates[peer_id]);
                
                if (publish_callback_) {
                    publish_callback_(rmcs_topic, candidatesStr);
                    std::cout << "📤 Published " << localCandidates[peer_id].size() << " local ICE candidates to rmcs topic for " << peer_id << std::endl;
                }
                
                // Clear candidates for this peer
                localCandidates.erase(peer_id);
            }
        } else {
            std::cout << "🧊 Peer " << peer_id << " ICE gathering: In Progress" << std::endl;
        }
    });
    
    pc->onLocalDescription([this, peer_id](rtc::Description description) {
        std::cout << "📤 Local description ready for " << peer_id << std::endl;
        
        // Publish answer to MQTT
        std::string answer_topic = thing_name_ + "/robot-control/" + peer_id + "/answer";
        
        // Publish raw SDP answer (to match the format from response.md)
        std::string sdp_answer = description;
        
        if (publish_callback_) {
            publish_callback_(answer_topic, sdp_answer);
            std::cout << "✅ Raw SDP answer published for peer " << peer_id << std::endl;
            std::cout << "📄 Answer SDP length: " << sdp_answer.length() << " characters" << std::endl;
        }
    });
}

bool WebRTCManager::handleOffer(const std::string& peer_id, const std::string& offer_sdp) {
    try {
        std::cout << "🚀 Creating WebRTC peer connection for: " << peer_id << std::endl;
        
        // Create new peer connection
        auto pc = createPeerConnection(peer_id);
        
        // Store the peer connection
        peer_connections_[peer_id] = pc;
        
        // Parse and set remote description
        rtc::Description offer(offer_sdp, rtc::Description::Type::Offer);
        pc->setRemoteDescription(offer);
        
        std::cout << "📥 Remote description set for " << peer_id << std::endl;
        
        // Now add video track after remote description is set
        try {
            std::cout << "🎬 Adding video track to peer connection" << std::endl;
            
            // Create video media description with H264 codec
            rtc::Description::Video video("video0", rtc::Description::Direction::SendOnly);
            video.addH264Codec(96, "baseline"); // PT 96 for H264
            video.setBitrate(1000); // 1 Mbps
            
            auto video_track = pc->addTrack(video);
            video_tracks_[peer_id] = video_track;
            
            // Set up track callbacks
            video_track->onOpen([this, peer_id]() {
                std::cout << "✅ Video track opened for " << peer_id << std::endl;
                
                // Auto-start H264 video streaming when track opens
                std::string video_file = "/Users/quando/dev/m2m/jetson/bag_processor/extracted_images_20250823_115613/flir_id8_image_resized_30fps.mp4";
                std::cout << "🎬 Auto-starting H264 video streaming via WebRTC..." << std::endl;
                this->startH264FileStreaming(peer_id, video_file);
            });
            
            video_track->onClosed([peer_id]() {
                std::cout << "❌ Video track closed for " << peer_id << std::endl;
            });
            
            std::cout << "✅ Video track with H264 codec added successfully" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "⚠️  Failed to add video track: " << e.what() << std::endl;
        }
        
        // The answer will be automatically generated and published via onLocalDescription callback
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error handling offer for " << peer_id << ": " << e.what() << std::endl;
        return false;
    }
}

bool WebRTCManager::handleCandidates(const std::string& peer_id, const Json::Value& candidates) {
#ifdef JSON_ENABLED
    try {
        // Find the peer connection
        auto it = peer_connections_.find(peer_id);
        if (it == peer_connections_.end()) {
            std::cout << "⚠️  No peer connection found for " << peer_id << std::endl;
            return false;
        }
        
        auto pc = it->second;
        if (!pc) {
            std::cout << "⚠️  Invalid peer connection for " << peer_id << std::endl;
            return false;
        }
        
        std::cout << "🧊 Processing " << candidates.size() << " ICE candidates for " << peer_id << std::endl;
        
        // Process each candidate
        for (const auto& candidateJson : candidates) {
            if (candidateJson.isMember("candidate") && candidateJson.isMember("sdpMid")) {
                std::string candidateStr = candidateJson["candidate"].asString();
                std::string sdpMid = candidateJson["sdpMid"].asString();
                int sdpMLineIndex = candidateJson.get("sdpMLineIndex", 0).asInt();
                
                // Create rtc::Candidate and add to peer connection
                rtc::Candidate candidate(candidateStr, sdpMid);
                pc->addRemoteCandidate(candidate);
                
                std::cout << "✅ Added ICE candidate: " << candidateStr << " (mid: " << sdpMid << ")" << std::endl;
            } else {
                std::cout << "⚠️  Invalid candidate format - missing required fields" << std::endl;
            }
        }
        
        // Note: Remote candidates from Flutter are processed above and set on peer connection
        // Local robot candidates are automatically published to /rmcs when generated
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error handling ICE candidates for " << peer_id << ": " << e.what() << std::endl;
        return false;
    }
#else
    std::cout << "⚠️  JSON parsing disabled - cannot handle ICE candidates" << std::endl;
    return false;
#endif
}

void WebRTCManager::closePeerConnection(const std::string& peer_id) {
    auto it = peer_connections_.find(peer_id);
    if (it != peer_connections_.end()) {
        if (it->second) {
            it->second->close();
        }
        peer_connections_.erase(it);
        std::cout << "🔒 Closed peer connection for " << peer_id << std::endl;
    }
}

bool WebRTCManager::startVideoStreaming(const std::string& peer_id, const std::string& images_dir_path) {
    try {
        auto it = peer_connections_.find(peer_id);
        if (it == peer_connections_.end()) {
            std::cout << "⚠️  No peer connection found for " << peer_id << std::endl;
            return false;
        }
        
        auto pc = it->second;
        if (!pc) {
            std::cout << "⚠️  Invalid peer connection for " << peer_id << std::endl;
            return false;
        }
        
        std::cout << "🎥 Starting live image streaming for " << peer_id << std::endl;
        std::cout << "📁 Images directory: " << images_dir_path << std::endl;
        
        // Get existing video track (created during peer connection setup)
        auto track_it = video_tracks_.find(peer_id);
        if (track_it == video_tracks_.end()) {
            std::cout << "⚠️  No video track found for " << peer_id << std::endl;
            return false;
        }
        
        // Wait for track to be ready before starting streaming
        auto track = track_it->second;
        std::cout << "⏳ Waiting for video track to be ready..." << std::endl;
        
        // Start streaming in background thread with track readiness check
        streaming_active_[peer_id] = true;
        streaming_threads_[peer_id] = std::thread([this, peer_id, images_dir_path, track]() {
            // Wait for track to be open
            int wait_count = 0;
            while (wait_count < 50 && !track->isOpen()) {  // Wait up to 5 seconds
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                wait_count++;
            }
            
            if (track->isOpen()) {
                std::cout << "✅ Track is ready, starting streaming..." << std::endl;
                this->streamImagesFromDirectory(peer_id, images_dir_path);
            } else {
                std::cout << "❌ Track failed to open within timeout" << std::endl;
            }
        });
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error starting image streaming for " << peer_id << ": " << e.what() << std::endl;
        return false;
    }
}

void WebRTCManager::stopVideoStreaming(const std::string& peer_id) {
    std::cout << "🛑 Stopping video streaming for " << peer_id << std::endl;
    
    // Stop streaming
    auto active_it = streaming_active_.find(peer_id);
    if (active_it != streaming_active_.end()) {
        active_it->second = false;
    }
    
    // Wait for thread to finish
    auto thread_it = streaming_threads_.find(peer_id);
    if (thread_it != streaming_threads_.end() && thread_it->second.joinable()) {
        thread_it->second.join();
        streaming_threads_.erase(thread_it);
    }
    
    // Clean up
    streaming_active_.erase(peer_id);
    video_tracks_.erase(peer_id);
}

void WebRTCManager::streamImagesFromDirectory(const std::string& peer_id, const std::string& images_dir) {
    try {
        std::cout << "📁 Loading images from directory: " << images_dir << std::endl;
        
        // Get image files
        auto image_files = getImageFiles(images_dir);
        if (image_files.empty()) {
            std::cout << "⚠️  No image files found in: " << images_dir << std::endl;
            return;
        }
        
        std::cout << "📊 Found " << image_files.size() << " images" << std::endl;
        
        // Get video track
        auto track_it = video_tracks_.find(peer_id);
        if (track_it == video_tracks_.end()) {
            std::cout << "⚠️  No video track found for " << peer_id << std::endl;
            return;
        }
        
        auto track = track_it->second;
        if (!track) {
            std::cout << "⚠️  Invalid video track for " << peer_id << std::endl;
            return;
        }
        
        // Stream images at 30 FPS
        const int fps = 30;
        const auto frame_duration = std::chrono::milliseconds(1000 / fps);
        
        std::cout << "🎬 Starting 30 FPS image streaming..." << std::endl;
        
        size_t frame_count = 0;
        auto& active = streaming_active_[peer_id];
        
        while (active && frame_count < image_files.size()) {
            // Load and process image
            cv::Mat frame = loadAndResizeImage(image_files[frame_count]);
            if (frame.empty()) {
                std::cout << "⚠️  Failed to load image: " << image_files[frame_count] << std::endl;
                frame_count++;
                continue;
            }
            
            // Send frame
            sendH264Frame(track, frame);
            
            // Only log first and last frame
            if (frame_count == 0) {
                std::cout << "📤 Started sending frames (" << frame.cols << "x" << frame.rows << ") at 30 FPS..." << std::endl;
            }
            
            frame_count++;
            
            // Wait for next frame timing
            std::this_thread::sleep_for(frame_duration);
        }
        
        std::cout << "✅ Image streaming completed for " << peer_id << " (" << frame_count << " frames sent)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error in image streaming thread for " << peer_id << ": " << e.what() << std::endl;
    }
}

std::vector<std::string> WebRTCManager::getImageFiles(const std::string& directory) {
    std::vector<std::string> image_files;
    
    try {
        // Use OpenCV to find image files
        std::vector<cv::String> files;
        cv::glob(directory + "/*.jpg", files);
        
        // Convert to std::string and sort
        for (const auto& file : files) {
            image_files.push_back(file);
        }
        
        // Sort files by name to ensure correct order
        std::sort(image_files.begin(), image_files.end());
        
        std::cout << "🔍 Found " << image_files.size() << " JPG files in " << directory << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error reading directory " << directory << ": " << e.what() << std::endl;
    }
    
    return image_files;
}

cv::Mat WebRTCManager::loadAndResizeImage(const std::string& image_path) {
    try {
        // Load image
        cv::Mat image = cv::imread(image_path);
        if (image.empty()) {
            std::cerr << "❌ Failed to load image: " << image_path << std::endl;
            return cv::Mat();
        }
        
        // Resize to standard resolution for WebRTC (640x480)
        cv::Mat resized;
        cv::resize(image, resized, cv::Size(640, 480));
        
        return resized;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error processing image " << image_path << ": " << e.what() << std::endl;
        return cv::Mat();
    }
}

void WebRTCManager::sendH264Frame(std::shared_ptr<rtc::Track> track, const cv::Mat& frame) {
    if (!track || frame.empty()) {
        std::cout << "⚠️  Invalid track or empty frame" << std::endl;
        return;
    }
    
    if (!track->isOpen()) {
        std::cout << "⚠️  Track is not open" << std::endl;
        return;
    }
    
    try {
        // Encode frame as JPEG for WebRTC (simpler approach)
        std::vector<uchar> encoded_image;
        std::vector<int> compression_params = {cv::IMWRITE_JPEG_QUALITY, 80};
        
        if (!cv::imencode(".jpg", frame, encoded_image, compression_params)) {
            std::cout << "⚠️  Failed to encode frame" << std::endl;
            return;
        }
        
        // Convert to rtc::binary
        rtc::binary packet;
        packet.reserve(encoded_image.size());
        
        for (const auto& byte : encoded_image) {
            packet.push_back(static_cast<std::byte>(byte));
        }
        
        if (track->send(packet)) {
            // Success - frame sent
        } else {
            std::cout << "⚠️  Failed to send frame data" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error sending frame: " << e.what() << std::endl;
    }
}

std::vector<uint8_t> WebRTCManager::encodeFrameToH264(const cv::Mat& frame) {
    std::vector<uint8_t> h264_data;
    
    try {
        // For libdatachannel, we need to send raw RGB/BGR frame data
        // The library will handle H264 encoding internally
        
        // Ensure frame is continuous in memory
        cv::Mat continuous_frame;
        if (frame.isContinuous()) {
            continuous_frame = frame;
        } else {
            frame.copyTo(continuous_frame);
        }
        
        // Get raw BGR data
        size_t data_size = continuous_frame.total() * continuous_frame.elemSize();
        h264_data.assign(continuous_frame.data, continuous_frame.data + data_size);
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error preparing frame data: " << e.what() << std::endl;
    }
    
    return h264_data;
}

bool WebRTCManager::startH264FileStreaming(const std::string& peer_id, const std::string& h264_file_path) {
    try {
        auto it = peer_connections_.find(peer_id);
        if (it == peer_connections_.end()) {
            std::cout << "⚠️  No peer connection found for " << peer_id << std::endl;
            return false;
        }
        
        auto track_it = video_tracks_.find(peer_id);
        if (track_it == video_tracks_.end()) {
            std::cout << "⚠️  No video track found for " << peer_id << std::endl;
            return false;
        }
        
        auto track = track_it->second;
        if (!track || !track->isOpen()) {
            std::cout << "⚠️  Track is not ready for " << peer_id << std::endl;
            return false;
        }
        
        std::cout << "🎬 Starting H264 file streaming: " << h264_file_path << std::endl;
        
        // Read H264 file and send chunks
        std::ifstream file(h264_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open H264 file: " << h264_file_path << std::endl;
            return false;
        }
        
        // Read entire file
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> h264_data(file_size);
        file.read(reinterpret_cast<char*>(h264_data.data()), file_size);
        file.close();
        
        std::cout << "📁 Loaded H264 file (" << file_size << " bytes)" << std::endl;
        
        // Send in chunks at 30 FPS
        const size_t chunk_size = file_size / 438; // Divide into frame-sized chunks
        const auto frame_duration = std::chrono::milliseconds(1000 / 30);
        
        streaming_active_[peer_id] = true;
        streaming_threads_[peer_id] = std::thread([this, peer_id, h264_data, chunk_size, frame_duration, track]() {
            try {
                size_t offset = 0;
                int frame_num = 0;
                auto& active = streaming_active_[peer_id];
                
                std::cout << "📤 Started sending H264 data at 30 FPS..." << std::endl;
                
                while (active && offset < h264_data.size()) {
                    size_t current_chunk_size = std::min(chunk_size, h264_data.size() - offset);
                    
                    rtc::binary packet;
                    packet.reserve(current_chunk_size);
                    
                    for (size_t i = 0; i < current_chunk_size; i++) {
                        packet.push_back(static_cast<std::byte>(h264_data[offset + i]));
                    }
                    
                    if (track->send(packet)) {
                        // Success
                    } else {
                        std::cout << "⚠️  Failed to send H264 chunk" << std::endl;
                    }
                    
                    offset += current_chunk_size;
                    frame_num++;
                    
                    std::this_thread::sleep_for(frame_duration);
                }
                
                std::cout << "✅ H264 streaming completed (" << frame_num << " chunks sent)" << std::endl;
                
            } catch (const std::exception& e) {
                std::cerr << "❌ Error in H264 streaming thread: " << e.what() << std::endl;
            }
        });
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error starting H264 file streaming: " << e.what() << std::endl;
        return false;
    }
}

bool WebRTCManager::isWebRTCEnabled() const {
    return true;
}

#endif

#ifndef WEBRTC_ENABLED
// Mock implementation when WebRTC is disabled
MockWebRTCManager::MockWebRTCManager(const std::string& thing_name, PublishCallback publish_cb) 
    : thing_name_(thing_name), publish_callback_(publish_cb) {
    std::cout << "⚠️ WebRTC Manager initialized in MOCK mode (libdatachannel not available)" << std::endl;
}

bool MockWebRTCManager::handleOffer(const std::string& peer_id, const std::string& offer_sdp) {
    std::cout << "🤖 MOCK: Handling offer for peer " << peer_id << std::endl;
    
    // Send mock answer
    std::string answer_topic = thing_name_ + "/robot-control/" + peer_id + "/answer";
    std::string mock_answer = "{\"connected\": true, \"mock\": true, \"message\": \"WebRTC not available\"}";
    
    if (publish_callback_) {
        publish_callback_(answer_topic, mock_answer);
        std::cout << "✅ Mock answer published for peer " << peer_id << std::endl;
    }
    
    return true;
}

bool MockWebRTCManager::handleCandidates(const std::string& peer_id, const Json::Value& candidates) {
#ifdef JSON_ENABLED
    std::cout << "🧊 MOCK: Handling " << candidates.size() << " ICE candidates for peer " << peer_id << std::endl;
    
    // Mock republish to rmcs topic
    std::string rmcs_topic = thing_name_ + "/robot-control/" + peer_id + "/candidate/rmcs";
    
    // Convert candidates back to JSON string
    Json::StreamWriterBuilder builder;
    std::string candidatesStr = Json::writeString(builder, candidates);
    
    if (publish_callback_) {
        publish_callback_(rmcs_topic, candidatesStr);
        std::cout << "📤 MOCK: Republished ICE candidates to rmcs topic" << std::endl;
    }
    
    return true;
#else
    std::cout << "⚠️  MOCK: JSON parsing disabled - cannot handle ICE candidates" << std::endl;
    return false;
#endif
}

bool MockWebRTCManager::startVideoStreaming(const std::string& peer_id, const std::string& images_dir_path) {
    std::cout << "🎥 MOCK: Starting video streaming for " << peer_id << " with images dir: " << images_dir_path << std::endl;
    return true;
}

void MockWebRTCManager::stopVideoStreaming(const std::string& peer_id) {
    std::cout << "🛑 MOCK: Stopping video streaming for " << peer_id << std::endl;
}

void MockWebRTCManager::closePeerConnection(const std::string& peer_id) {
    std::cout << "🔒 MOCK: Closed peer connection for " << peer_id << std::endl;
}

#endif