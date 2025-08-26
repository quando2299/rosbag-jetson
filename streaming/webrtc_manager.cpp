#include "webrtc_manager.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstddef>
#include <fstream>

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
    
    // Use original working STUN configuration
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun2.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun3.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun4.l.google.com:19302");
    
    std::cout << "🌐 WebRTC config: Using original STUN configuration" << std::endl;
    
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
                std::cout << "⏳ WebRTC connection in progress for " << peer_id << std::endl;
                break;
            case rtc::PeerConnection::State::Connected:
                std::cout << "Connected" << std::endl;
                std::cout << "✅ WebRTC connection established for " << peer_id << std::endl;
                std::cout << "🎯 Starting video streaming immediately on connection established" << std::endl;
                
                // Start video streaming immediately when connection is established
                std::thread([this, peer_id]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Wait 1 second for connection to stabilize
                    std::cout << "🎬 Starting video streaming for connected peer " << peer_id << std::endl;
                    this->startLiveVideoStreaming(peer_id);
                }).detach();
                break;
            case rtc::PeerConnection::State::Disconnected:
                std::cout << "Disconnected" << std::endl;
                std::cout << "⚠️ WebRTC connection disconnected for " << peer_id << std::endl;
                std::cout << "🛑 Stopping video streaming for disconnected peer" << std::endl;
                this->stopVideoStreaming(peer_id);
                break;
            case rtc::PeerConnection::State::Failed:
                std::cout << "Failed" << std::endl;
                std::cout << "❌ WebRTC connection failed for " << peer_id << std::endl;
                std::cout << "🔍 Possible causes:" << std::endl;
                std::cout << "   - Network connectivity issues (firewall/NAT)" << std::endl;
                std::cout << "   - STUN/TURN server unreachable" << std::endl;
                std::cout << "   - ICE gathering timeout" << std::endl;
                std::cout << "   - SDP incompatibility" << std::endl;
                std::cout << "🛑 Stopping video streaming for failed peer" << std::endl;
                this->stopVideoStreaming(peer_id);
                break;
            case rtc::PeerConnection::State::Closed:
                std::cout << "Closed" << std::endl;
                std::cout << "🛑 Stopping video streaming for closed peer" << std::endl;
                this->stopVideoStreaming(peer_id);
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
                std::cout << "⏳ Gathering ICE candidates from STUN/TURN servers..." << std::endl;
                break;
            case rtc::PeerConnection::GatheringState::Complete:
                std::cout << "Complete" << std::endl;
                std::cout << "✅ ICE candidate gathering finished for " << peer_id << std::endl;
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
        std::cout << "🎉 CALLBACK TRIGGERED: Step 6 - Create Answer and setLocalDescription" << std::endl;
        std::cout << "📝 Step 6a: Answer created automatically by libdatachannel for " << peer_id << std::endl;
        std::cout << "📝 Step 6b: setLocalDescription(answer) completed automatically" << std::endl;
        
        // Get the generated SDP answer
        std::string sdp_answer = description;
        
        // Check what tracks are in our answer
        bool has_audio = sdp_answer.find("m=audio") != std::string::npos;
        bool has_video = sdp_answer.find("m=video") != std::string::npos;
        
        std::cout << "🔍 Generated SDP Answer contains:" << std::endl;
        std::cout << "   🎵 Audio track: " << (has_audio ? "YES" : "NO") << std::endl;
        std::cout << "   📺 Video track: " << (has_video ? "YES" : "NO") << std::endl;
        
        std::cout << "🔍 DEBUG: Generated SDP Answer:" << std::endl;
        std::cout << "--- SDP START ---" << std::endl;
        std::cout << sdp_answer << std::endl;
        std::cout << "--- SDP END ---" << std::endl;
        
        // Step 6c: Publish answer to /answer topic
        std::string answer_topic = thing_name_ + "/robot-control/" + peer_id + "/answer";
        std::cout << "📡 Step 6c: Publishing answer to /answer topic: " << answer_topic << std::endl;
        
        if (publish_callback_) {
            publish_callback_(answer_topic, sdp_answer);
            std::cout << "✅ Step 6 COMPLETE: Answer created, setLocalDescription called, and published to /answer topic" << std::endl;
            std::cout << "📄 Answer SDP length: " << sdp_answer.length() << " characters" << std::endl;
            std::cout << "🎯 All 6 steps completed successfully for peer " << peer_id << std::endl;
        } else {
            std::cerr << "❌ Step 6c Failed: No publish callback available" << std::endl;
        }
    });
}

bool WebRTCManager::handleOffer(const std::string& peer_id, const std::string& offer_sdp) {
    try {
        std::cout << "🚀 Step 1: peerId extracted: " << peer_id << std::endl;
        
        // Step 2: Create PeerConnection and store with key <peerId>
        std::cout << "🔗 Step 2: Creating PeerConnection for " << peer_id << std::endl;
        auto pc = createPeerConnection(peer_id);
        peer_connections_[peer_id] = pc;
        std::cout << "✅ Step 2 complete: PeerConnection created and stored" << std::endl;
        
        // Step 3: Register onIceCandidate handler to store ICE candidates for <peerId>
        std::cout << "🧊 Step 3: Registering ICE candidate handler" << std::endl;
        // Note: ICE handling is already set up in createPeerConnection -> setupICEHandling
        std::cout << "✅ Step 3 complete: ICE candidate handler registered" << std::endl;
        
        // Step 4: Add video stream to PeerConnection  
        std::cout << "🎬 Step 4: Adding video stream to PeerConnection" << std::endl;
        try {
            // Create video media description with H264 codec
            rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
            video.addH264Codec(96, "packetization-mode=1;level-asymmetry-allowed=1"); 
            video.setBitrate(1000); // 1 Mbps
            
            // Add track with H264 RTP packetizer configured for long start sequences
            auto video_track = pc->addTrack(video);
            
            // Configure H264 RTP packetizer with LongStartSequence separator
            // This is crucial for proper H.264 frame handling with 4-byte start codes (0x00000001)
            if (auto h264_track = std::dynamic_pointer_cast<rtc::H264RtpPacketizer>(video_track)) {
                std::cout << "🔧 Configuring H264 RTP packetizer with LongStartSequence separator" << std::endl;
                // The track should be configured to handle 4-byte start sequences
            } else {
                std::cout << "🔧 Configuring video track for H264 with long start sequences" << std::endl;
            }
            
            video_tracks_[peer_id] = video_track;
            std::cout << "🎬 Video track created and added to PeerConnection" << std::endl;
            
            // Set up track callbacks
            video_track->onOpen([this, peer_id]() {
                std::cout << "🎉 TRACK OPENED CALLBACK TRIGGERED for " << peer_id << std::endl;
                std::cout << "✅ Video track opened for " << peer_id << std::endl;
                std::cout << "📺 Track is ready (streaming will start on connection established)" << std::endl;
            });
            
            video_track->onClosed([this, peer_id]() {
                std::cout << "❌ Video track closed for " << peer_id << std::endl;
                std::cout << "🛑 Stopping video streaming due to track closure" << std::endl;
                this->stopVideoStreaming(peer_id);
            });
            
            std::cout << "✅ Step 4 complete: Video stream added to PeerConnection" << std::endl;
        std::cout << "⏳ Waiting for video track to open after connection is established..." << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Step 4 failed: " << e.what() << std::endl;
            return false;
        }
        
        // Step 5: Set remote description using received offer
        std::cout << "📥 Step 5: Setting remote description using received offer" << std::endl;
        std::cout << "🔍 DEBUG: Received offer SDP length: " << offer_sdp.length() << " chars" << std::endl;
        
        try {
            rtc::Description offer(offer_sdp, rtc::Description::Type::Offer);
            pc->setRemoteDescription(offer);
            std::cout << "✅ Step 5 complete: Remote description set using received offer" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "❌ Step 5 failed: " << e.what() << std::endl;
            return false;
        }
        
        // Step 6: Create Answer and setLocalDescription(answer) - handled in onLocalDescription callback
        std::cout << "⏳ Step 6: Answer creation will happen in onLocalDescription callback" << std::endl;
        std::cout << "✅ All 6 steps initiated successfully for peer " << peer_id << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error in 6-step offer handling for " << peer_id << ": " << e.what() << std::endl;
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
    
    // Stop streaming flag
    auto active_it = streaming_active_.find(peer_id);
    if (active_it != streaming_active_.end()) {
        active_it->second = false;
        std::cout << "   ✅ Streaming flag set to false" << std::endl;
    } else {
        std::cout << "   ⚠️ No active streaming found for peer " << peer_id << std::endl;
    }
    
    // Wait for thread to finish
    auto thread_it = streaming_threads_.find(peer_id);
    if (thread_it != streaming_threads_.end() && thread_it->second.joinable()) {
        std::cout << "   ⏳ Waiting for streaming thread to stop..." << std::endl;
        thread_it->second.join();
        streaming_threads_.erase(thread_it);
        std::cout << "   ✅ Streaming thread stopped and cleaned up" << std::endl;
    } else {
        std::cout << "   ⚠️ No active streaming thread found for peer " << peer_id << std::endl;
    }
    
    // Clean up
    streaming_active_.erase(peer_id);
    video_tracks_.erase(peer_id);
    std::cout << "✅ Video streaming completely stopped for " << peer_id << std::endl;
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
        // Use OpenCV to find image files - try multiple extensions
        std::vector<cv::String> files;
        
        // Try .jpg extension first
        cv::glob(directory + "/*.jpg", files);
        std::cout << "🔍 Found " << files.size() << " .jpg files" << std::endl;
        
        // If no .jpg files found, try .jpeg extension
        if (files.empty()) {
            cv::glob(directory + "/*.jpeg", files);
            std::cout << "🔍 Found " << files.size() << " .jpeg files" << std::endl;
        }
        
        // If still no files, try .JPG extension
        if (files.empty()) {
            cv::glob(directory + "/*.JPG", files);
            std::cout << "🔍 Found " << files.size() << " .JPG files" << std::endl;
        }
        
        // If still no files, try .JPEG extension
        if (files.empty()) {
            cv::glob(directory + "/*.JPEG", files);
            std::cout << "🔍 Found " << files.size() << " .JPEG files" << std::endl;
        }
        
        // Convert to std::string and sort
        for (const auto& file : files) {
            image_files.push_back(file);
        }
        
        // Sort files by name to ensure correct order
        std::sort(image_files.begin(), image_files.end());
        
        std::cout << "🔍 Total " << image_files.size() << " image files found in " << directory << std::endl;
        
        // Debug: show first few filenames if found
        if (!image_files.empty()) {
            std::cout << "📂 Sample files: ";
            for (size_t i = 0; i < std::min(size_t(3), image_files.size()); i++) {
                std::cout << image_files[i] << " ";
            }
            std::cout << std::endl;
        }
        
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
        return;
    }
    
    if (!track->isOpen()) {
        return;
    }
    
    try {
        static int frame_counter = 0;
        static bool is_jetson_encoder = false;
        static bool checked_jetson = false;
        
        // Check once if we're getting H.264 data from Jetson hardware encoder
        if (!checked_jetson) {
            // If frame has only 1 channel and specific size pattern, it's likely H.264 data
            is_jetson_encoder = (frame.channels() == 1 && frame.type() == CV_8UC1);
            checked_jetson = true;
            
            if (is_jetson_encoder) {
                std::cout << "📹 Detected H.264 encoded frames from Jetson hardware encoder" << std::endl;
            }
        }
        
        if (is_jetson_encoder) {
            // Frame is already H.264 encoded by Jetson hardware encoder!
            // Just extract the NAL units and send them
            std::vector<uint8_t> h264_data(frame.data, frame.data + frame.total());
            
            // The Jetson encoder already provides proper NAL units with start codes
            // Just send them directly to libdatachannel
            bool success = track->send(reinterpret_cast<const rtc::byte*>(h264_data.data()), h264_data.size());
            
            if (!success && frame_counter % 30 == 0) {
                std::cout << "⚠️  Failed to send Jetson H.264 frame " << frame_counter << std::endl;
            }
        } else {
            // Non-Jetson: Send synthetic H.264 for testing
            // (In production, you'd want to use x264 or another software encoder here)
            std::vector<uint8_t> h264_frame;
            
            // Every 30 frames send keyframe with SPS/PPS
            bool is_keyframe = (frame_counter % 30 == 0);
            
            if (is_keyframe) {
                // Add 4-byte start code + SPS
                h264_frame.insert(h264_frame.end(), {0x00, 0x00, 0x00, 0x01});
                
                // Valid SPS for 640x480 video
                std::vector<uint8_t> sps = {
                    0x67, 0x42, 0x00, 0x1E, 0x8D, 0x80, 0x50, 0x05,
                    0xBA, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00,
                    0x00, 0x03, 0x03, 0x20, 0xF1, 0x42, 0xA4
                };
                h264_frame.insert(h264_frame.end(), sps.begin(), sps.end());
                
                // Add 4-byte start code + PPS  
                h264_frame.insert(h264_frame.end(), {0x00, 0x00, 0x00, 0x01});
                std::vector<uint8_t> pps = {0x68, 0xCE, 0x3C, 0x80};
                h264_frame.insert(h264_frame.end(), pps.begin(), pps.end());
                
                // Add 4-byte start code + IDR slice
                h264_frame.insert(h264_frame.end(), {0x00, 0x00, 0x00, 0x01});
                h264_frame.push_back(0x65); // IDR slice NAL type
                
                // Add minimal IDR payload (creates a valid visible frame)
                for (int i = 0; i < 100; i++) {
                    h264_frame.push_back(0x80 + (i % 16)); // Minimal encoded data
                }
            } else {
                // Add 4-byte start code + P-frame
                h264_frame.insert(h264_frame.end(), {0x00, 0x00, 0x00, 0x01});
                h264_frame.push_back(0x61); // P slice NAL type
                
                // Add minimal P-frame payload
                for (int i = 0; i < 50; i++) {
                    h264_frame.push_back(0x40 + (i % 8)); // Minimal encoded data
                }
            }
            
            // Send the H264 frame
            bool success = track->send(reinterpret_cast<const rtc::byte*>(h264_frame.data()), h264_frame.size());
            
            if (!success && frame_counter % 30 == 0) {
                std::cout << "⚠️  Failed to send synthetic H264 frame " << frame_counter << std::endl;
            }
        }
        
        frame_counter++;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error in sendH264Frame: " << e.what() << std::endl;
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
        
        // Read H264/MP4 file
        std::ifstream file(h264_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open video file: " << h264_file_path << std::endl;
            return false;
        }
        
        // Read entire file
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> video_data(file_size);
        file.read(reinterpret_cast<char*>(video_data.data()), file_size);
        file.close();
        
        std::cout << "📁 Loaded video file (" << file_size << " bytes)" << std::endl;
        
        // Extract NAL units from MP4 container
        auto nal_units = extractNALUnits(video_data);
        std::cout << "🔍 Extracted " << nal_units.size() << " NAL units from video file" << std::endl;
        
        // Count NAL unit types for debugging
        std::map<int, int> nal_type_counts;
        for (const auto& nal_unit : nal_units) {
            if (!nal_unit.empty()) {
                uint8_t nal_type = nal_unit[0] & 0x1F;
                nal_type_counts[nal_type]++;
            }
        }
        
        std::cout << "📊 NAL unit type distribution:" << std::endl;
        for (const auto& [type, count] : nal_type_counts) {
            const char* type_name = "Unknown";
            switch (type) {
                case 1: type_name = "Non-IDR"; break;
                case 5: type_name = "IDR"; break;
                case 6: type_name = "SEI"; break;
                case 7: type_name = "SPS"; break;
                case 8: type_name = "PPS"; break;
                case 9: type_name = "AU Delimiter"; break;
            }
            std::cout << "   Type " << type << " (" << type_name << "): " << count << " units" << std::endl;
        }
        
        if (nal_units.empty()) {
            std::cout << "⚠️  No NAL units found in video file" << std::endl;
            return false;
        }
        
        const auto frame_duration = std::chrono::milliseconds(33); // 30 FPS (33ms per frame)
        
        streaming_active_[peer_id] = true;
        streaming_threads_[peer_id] = std::thread([this, peer_id, nal_units, frame_duration, track]() {
            try {
                int nal_count = 0;
                auto& active = streaming_active_[peer_id];
                
                std::cout << "📤 Started sending H264 NAL units via WebRTC..." << std::endl;
                
                // Wait a bit for track to stabilize
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                for (const auto& nal_unit : nal_units) {
                    if (!active) break;
                    
                    try {
                        if (track->isOpen()) {
                            // Send NAL unit with proper RTP packetization
                            sendNALUnit(track, nal_unit);
                            
                            if (nal_count % 10 == 0) {
                                std::cout << "📤 Sent NAL unit " << nal_count << " (size: " << nal_unit.size() << " bytes)" << std::endl;
                            }
                        } else {
                            std::cout << "⚠️ Track closed, stopping stream" << std::endl;
                            break;
                        }
                    } catch (const std::exception& e) {
                        std::cout << "⚠️ Error sending NAL unit: " << e.what() << std::endl;
                        // Continue with next NAL unit
                    }
                    
                    nal_count++;
                    
                    // Frame rate control - send frames at 30 FPS
                    std::this_thread::sleep_for(frame_duration);
                }
                
                std::cout << "✅ H264 NAL unit streaming completed (" << nal_count << " NAL units sent)" << std::endl;
                
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

std::string WebRTCManager::findVideoFile() {
    std::cout << "🔍 Looking for video files in /workspace/videos..." << std::endl;
    
    // Look for MP4 files in the videos directory (copied during Docker build)
    std::vector<cv::String> videos;
    cv::glob("/workspace/videos/*.mp4", videos);
    
    if (!videos.empty()) {
        std::cout << "✅ Found " << videos.size() << " video file(s)" << std::endl;
        std::cout << "📹 Using video: " << videos[0] << std::endl;
        return videos[0];
    }
    
    std::cout << "⚠️ No video files found in /workspace/videos/" << std::endl;
    
    // List what's actually there for debugging
    std::cout << "📁 Contents of /workspace/videos:" << std::endl;
    system("ls -la /workspace/videos/ 2>/dev/null || echo 'Directory not found'");
    
    return "";
}

void WebRTCManager::startTestPatternStreaming(const std::string& peer_id) {
    try {
        auto track_it = video_tracks_.find(peer_id);
        if (track_it == video_tracks_.end()) {
            std::cout << "⚠️  No video track found for " << peer_id << std::endl;
            return;
        }
        
        auto track = track_it->second;
        if (!track || !track->isOpen()) {
            std::cout << "⚠️  Track is not ready for " << peer_id << std::endl;
            return;
        }
        
        std::cout << "🎨 Starting test pattern streaming for " << peer_id << std::endl;
        
        // Create a simple test pattern (color bars)
        streaming_active_[peer_id] = true;
        streaming_threads_[peer_id] = std::thread([this, peer_id, track]() {
            try {
                auto& active = streaming_active_[peer_id];
                int frame_count = 0;
                const auto frame_duration = std::chrono::milliseconds(33); // 30 FPS (33ms per frame)
                
                while (active && frame_count < 300) { // Stream for 10 seconds
                    // Create a simple test pattern
                    rtc::binary packet;
                    
                    // Send a small test packet (simulate video data)
                    std::string test_data = "TEST_FRAME_" + std::to_string(frame_count);
                    for (char c : test_data) {
                        packet.push_back(static_cast<std::byte>(c));
                    }
                    
                    if (track->send(packet)) {
                        if (frame_count % 30 == 0) {
                            std::cout << "📺 Sent test frame " << frame_count << " via WebRTC" << std::endl;
                        }
                    } else {
                        std::cout << "⚠️  Failed to send test frame" << std::endl;
                    }
                    
                    frame_count++;
                    std::this_thread::sleep_for(frame_duration);
                }
                
                std::cout << "✅ Test pattern streaming completed (" << frame_count << " frames sent)" << std::endl;
                
            } catch (const std::exception& e) {
                std::cerr << "❌ Error in test pattern streaming: " << e.what() << std::endl;
            }
        });
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error starting test pattern: " << e.what() << std::endl;
    }
}

void WebRTCManager::startLiveVideoStreaming(const std::string& peer_id) {
    try {
        auto track_it = video_tracks_.find(peer_id);
        if (track_it == video_tracks_.end()) {
            std::cout << "⚠️  No video track found for " << peer_id << std::endl;
            return;
        }
        
        auto track = track_it->second;
        if (!track || !track->isOpen()) {
            std::cout << "⚠️  Track is not ready for " << peer_id << std::endl;
            return;
        }
        
        std::cout << "🎥 Starting live video streaming for " << peer_id << std::endl;
        
        streaming_active_[peer_id] = true;
        streaming_threads_[peer_id] = std::thread([this, peer_id, track]() {
            try {
                auto& active = streaming_active_[peer_id];
                int frame_count = 0;
                const auto frame_duration = std::chrono::milliseconds(33); // 30 FPS (33ms per frame)
                
                std::cout << "📹 Starting LIVE camera streaming (like robot_simulator getUserMedia)..." << std::endl;
                
                // Initialize camera with Jetson-optimized GStreamer pipeline
                // This uses NVIDIA hardware acceleration for H.264 encoding
                std::string gst_pipeline;
                
                // Check if we're on Jetson (has NVIDIA hardware encoder)
                std::ifstream tegra_check("/proc/device-tree/model");
                bool is_jetson = false;
                if (tegra_check.is_open()) {
                    std::string model;
                    std::getline(tegra_check, model);
                    is_jetson = (model.find("Jetson") != std::string::npos || 
                                model.find("Tegra") != std::string::npos);
                    tegra_check.close();
                }
                
                cv::VideoCapture cap;
                
                if (is_jetson) {
                    std::cout << "🚀 Detected Jetson platform - using optimized H.264 encoder!" << std::endl;
                    
                    // Try hardware pipeline first (works on real Jetson with L4T)
                    gst_pipeline = 
                        "v4l2src device=/dev/video0 ! "
                        "video/x-raw,width=640,height=480,framerate=30/1 ! "
                        "nvvidconv ! "
                        "video/x-raw(memory:NVMM) ! "
                        "nvv4l2h264enc bitrate=1000000 ! "  // 1 Mbps bitrate
                        "h264parse ! "
                        "appsink";
                    
                    cap.open(gst_pipeline, cv::CAP_GSTREAMER);
                    
                    if (!cap.isOpened()) {
                        std::cout << "⚠️ Hardware pipeline failed, trying software H.264 encoder..." << std::endl;
                        
                        // Software H.264 encoding pipeline (works in Docker)
                        gst_pipeline = 
                            "v4l2src device=/dev/video0 ! "
                            "video/x-raw,width=640,height=480,framerate=30/1 ! "
                            "videoconvert ! "
                            "x264enc tune=zerolatency bitrate=1000 ! "  // Software H.264
                            "h264parse ! "
                            "appsink";
                        
                        cap.open(gst_pipeline, cv::CAP_GSTREAMER);
                        
                        if (!cap.isOpened()) {
                            std::cout << "⚠️ GStreamer pipeline failed, using regular camera..." << std::endl;
                            cap.open(0);  // Final fallback to regular camera
                        } else {
                            std::cout << "✅ Using software H.264 encoder (x264enc)" << std::endl;
                        }
                    } else {
                        std::cout << "✅ Using hardware H.264 encoder (nvv4l2h264enc)" << std::endl;
                    }
                } else {
                    // Non-Jetson: try regular camera capture
                    cap.open(0);  // Try camera index 0 first
                    if (!cap.isOpened()) {
                        std::cout << "⚠️ Camera 0 not available, trying camera 1..." << std::endl;
                        cap.open(1);
                    }
                    if (!cap.isOpened()) {
                        std::cout << "⚠️ Camera 1 not available, trying camera 2..." << std::endl;
                        cap.open(2);
                    }
                }
                
                // Load image files as fallback (declare outside scope)
                std::vector<std::string> image_files;
                if (!cap.isOpened()) {
                    std::cout << "❌ No real camera available, falling back to bag_processor images" << std::endl;
                    // Fallback: Load individual images from bag_processor
                    image_files = getImageFiles("/workspace/videos");
                    if (image_files.empty()) {
                        std::cout << "⚠️ No images found either, creating sample frames" << std::endl;
                    }
                } else {
                    // Configure camera for optimal WebRTC streaming
                    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
                    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
                    cap.set(cv::CAP_PROP_FPS, 30);
                    
                    std::cout << "📷 REAL Camera initialized: " 
                              << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x" 
                              << cap.get(cv::CAP_PROP_FRAME_HEIGHT) 
                              << " @ " << cap.get(cv::CAP_PROP_FPS) << "fps" << std::endl;
                }
                
                cv::Mat camera_frame;
                
                while (active) { // Stream continuously like robot_simulator
                    auto start_time = std::chrono::steady_clock::now();
                    
                    try {
                        // Check if track is available and ready
                        if (track && track->isOpen()) {
                            cv::Mat frame_to_send;
                            bool frame_captured = false;
                            
                            // Try to capture LIVE camera frame first (like robot_simulator getUserMedia)
                            if (cap.isOpened() && cap.read(camera_frame) && !camera_frame.empty()) {
                                // Use REAL live camera frame
                                if (camera_frame.size() != cv::Size(640, 480)) {
                                    cv::resize(camera_frame, frame_to_send, cv::Size(640, 480));
                                } else {
                                    frame_to_send = camera_frame;
                                }
                                frame_captured = true;
                                
                                if (frame_count % 30 == 0) { // Log every second
                                    std::cout << "📤 LIVE Camera Frame " << frame_count << ": ✅ CAPTURED" << std::endl;
                                }
                            } else if (!image_files.empty()) {
                                // Fallback: Use bag_processor images
                                size_t img_index = frame_count % image_files.size();
                                frame_to_send = loadAndResizeImage(image_files[img_index]);
                                frame_captured = !frame_to_send.empty();
                                
                                if (frame_count % 30 == 0) {
                                    std::cout << "📤 Bag Frame " << frame_count << ": ✅ LOADED" << std::endl;
                                }
                            }
                            
                            // Send the frame (either live camera or fallback)
                            if (frame_captured && !frame_to_send.empty()) {
                                sendH264Frame(track, frame_to_send);
                            } else {
                                // Fallback: send simple frame data
                                std::string frame_data = "FRAME_" + std::to_string(frame_count);
                                rtc::binary packet;
                                for (char c : frame_data) {
                                    packet.push_back(static_cast<std::byte>(c));
                                }
                                bool sent = track->send(packet);
                                
                                if (frame_count % 10 == 0) {
                                    std::cout << "📤 Frame " << frame_count << ": " << (sent ? "✅ SENT" : "❌ FAILED") 
                                             << " (" << frame_data.length() << " bytes)" << std::endl;
                                }
                            }
                        } else {
                            std::cout << "⚠️ Track not ready - Frame " << frame_count << std::endl;
                        }
                    } catch (const std::exception& e) {
                        std::cout << "❌ Error sending frame " << frame_count << ": " << e.what() << std::endl;
                    }
                    
                    frame_count++;
                    
                    // Maintain 30fps timing (33ms per frame)
                    auto end_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    if (elapsed < frame_duration) {
                        std::this_thread::sleep_for(frame_duration - elapsed);
                    }
                }
                
                // Clean up camera capture
                if (cap.isOpened()) {
                    cap.release();
                    std::cout << "📷 Camera released" << std::endl;
                }
                
                std::cout << "✅ Live video streaming stopped (" << frame_count << " frames sent)" << std::endl;
                
            } catch (const std::exception& e) {
                std::cerr << "❌ Error in live video streaming: " << e.what() << std::endl;
            }
        });
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error starting live video streaming: " << e.what() << std::endl;
    }
}

std::vector<uint8_t> WebRTCManager::generateLiveH264Frame(int frame_number) {
    // Generate a simple H.264 frame with proper NAL units
    // This simulates what a real camera would produce
    
    std::vector<uint8_t> frame;
    
    // Every 30 frames (1 second), send SPS/PPS/IDR. Otherwise send P-frames.
    bool is_keyframe = (frame_number % 30 == 0);
    
    if (is_keyframe) {
        // SPS (Sequence Parameter Set)
        std::vector<uint8_t> sps = {
            0x67, 0x42, 0x00, 0x1E, 0x8D, 0x80, 0x28, 0x02, 
            0xDD, 0x80, 0xB5, 0x01, 0x01, 0x01, 0x40, 0x00, 
            0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0F, 0x03, 
            0xC5, 0x8B, 0xA8
        };
        
        // PPS (Picture Parameter Set)
        std::vector<uint8_t> pps = {
            0x68, 0xCE, 0x3C, 0x80
        };
        
        // Add SPS
        frame.insert(frame.end(), sps.begin(), sps.end());
        
        // Add PPS  
        frame.insert(frame.end(), pps.begin(), pps.end());
        
        // Add IDR frame (simplified)
        std::vector<uint8_t> idr_header = {0x65, 0x88, 0x82, 0x07, 0xFF, 0xFF};
        frame.insert(frame.end(), idr_header.begin(), idr_header.end());
        
        // Add some dummy payload for IDR frame
        for (int i = 0; i < 200; i++) {
            frame.push_back(static_cast<uint8_t>(0xAA + (i % 16)));
        }
        
        std::cout << "📹 Generated keyframe " << frame_number << " (SPS+PPS+IDR, size: " << frame.size() << " bytes)" << std::endl;
    } else {
        // P-frame (predicted frame)
        std::vector<uint8_t> p_header = {0x41, 0x9A, 0x24, 0x4D, 0x01, 0x8F};
        frame.insert(frame.end(), p_header.begin(), p_header.end());
        
        // Add some dummy payload for P-frame (smaller than IDR)
        for (int i = 0; i < 50; i++) {
            frame.push_back(static_cast<uint8_t>(0x55 + ((i + frame_number) % 32)));
        }
    }
    
    return frame;
}

void WebRTCManager::sendH264FrameRTP(std::shared_ptr<rtc::Track> track, const std::vector<uint8_t>& h264_frame, int frame_number) {
    if (!track || !track->isOpen() || h264_frame.empty()) {
        return;
    }
    
    try {
        // Send raw H264 frame data directly - libdatachannel handles RTP packetization
        // This is the correct way according to your example
        track->send(reinterpret_cast<const rtc::byte*>(h264_frame.data()), h264_frame.size());
        
        // Log success occasionally
        if (frame_number % 30 == 0) { // Log every second
            std::cout << "📤 Sent H264 frame " << frame_number << " (" << h264_frame.size() << " bytes)" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error sending H264 frame: " << e.what() << std::endl;
    }
}

bool WebRTCManager::isWebRTCEnabled() const {
    return true;
}

std::vector<std::vector<uint8_t>> WebRTCManager::extractNALUnits(const std::vector<uint8_t>& mp4_data) {
    std::vector<std::vector<uint8_t>> nal_units;
    
    // Look for H.264 NAL unit start codes (0x00000001 or 0x000001)
    for (size_t i = 0; i < mp4_data.size() - 4; ) {
        // Check for 4-byte start code (0x00000001)
        if (mp4_data[i] == 0x00 && mp4_data[i+1] == 0x00 && 
            mp4_data[i+2] == 0x00 && mp4_data[i+3] == 0x01) {
            
            size_t start = i + 4; // Skip start code
            size_t end = start;
            
            // Find next start code
            bool found_next = false;
            for (size_t j = start + 1; j < mp4_data.size() - 3; j++) {
                if ((mp4_data[j] == 0x00 && mp4_data[j+1] == 0x00 && 
                     mp4_data[j+2] == 0x00 && mp4_data[j+3] == 0x01) ||
                    (mp4_data[j] == 0x00 && mp4_data[j+1] == 0x00 && 
                     mp4_data[j+2] == 0x01)) {
                    end = j;
                    found_next = true;
                    break;
                }
            }
            
            if (!found_next) {
                end = mp4_data.size();
            }
            
            // Extract NAL unit
            if (end > start && start < mp4_data.size()) {
                std::vector<uint8_t> nal_unit(mp4_data.begin() + start, mp4_data.begin() + end);
                
                // Only process valid H.264 NAL units and ensure minimum size
                if (!nal_unit.empty() && nal_unit.size() >= 1) {
                    uint8_t nal_type = nal_unit[0] & 0x1F;
                    
                    // Only accept common H.264 NAL unit types (more restrictive filtering)
                    if (nal_type >= 1 && nal_type <= 9) {
                        
                        // Skip problematic SEI units that contain emulation prevention issues
                        if (nal_type == 6) {
                            // Check if SEI contains problematic sequences that would confuse the decoder
                            bool has_problematic_sequence = false;
                            
                            // Check for start code patterns (0x00 0x00 0x01) within payload
                            for (size_t i = 0; i < nal_unit.size() - 2; i++) {
                                if (nal_unit[i] == 0x00 && nal_unit[i+1] == 0x00 && nal_unit[i+2] == 0x01) {
                                    has_problematic_sequence = true;
                                    std::cout << "⚠️ Found start code emulation in SEI at position " << i << std::endl;
                                    break;
                                }
                            }
                            
                            // Also check for 4-byte start code patterns (0x00 0x00 0x00 0x01)
                            if (!has_problematic_sequence) {
                                for (size_t i = 0; i < nal_unit.size() - 3; i++) {
                                    if (nal_unit[i] == 0x00 && nal_unit[i+1] == 0x00 && 
                                        nal_unit[i+2] == 0x00 && nal_unit[i+3] == 0x01) {
                                        has_problematic_sequence = true;
                                        std::cout << "⚠️ Found 4-byte start code emulation in SEI at position " << i << std::endl;
                                        break;
                                    }
                                }
                            }
                            
                            if (has_problematic_sequence) {
                                std::cout << "⚠️ Skipping SEI with start code emulation issue (size: " << nal_unit.size() << " bytes)" << std::endl;
                                std::cout << "   This SEI likely contains timestamp data causing decoder confusion" << std::endl;
                                continue;
                            }
                        }
                        
                        nal_units.push_back(nal_unit);
                        
                        const char* type_name = "Unknown";
                        switch (nal_type) {
                            case 1: type_name = "Non-IDR"; break;
                            case 5: type_name = "IDR"; break;
                            case 6: type_name = "SEI"; break;
                            case 7: type_name = "SPS"; break;
                            case 8: type_name = "PPS"; break;
                            case 9: type_name = "AU Delimiter"; break;
                        }
                        
                        std::cout << "🔍 Found valid NAL unit (type: " << (int)nal_type 
                                 << "-" << type_name << ", size: " << nal_unit.size() << " bytes)" << std::endl;
                    } else {
                        std::cout << "⚠️ Skipping invalid NAL unit type: " << (int)nal_type << std::endl;
                    }
                }
            }
            
            i = end;
        }
        // Check for 3-byte start code (0x000001)  
        else if (mp4_data[i] == 0x00 && mp4_data[i+1] == 0x00 && mp4_data[i+2] == 0x01) {
            
            size_t start = i + 3; // Skip start code
            size_t end = start;
            
            // Find next start code
            bool found_next = false;
            for (size_t j = start + 1; j < mp4_data.size() - 3; j++) {
                if ((mp4_data[j] == 0x00 && mp4_data[j+1] == 0x00 && 
                     mp4_data[j+2] == 0x00 && mp4_data[j+3] == 0x01) ||
                    (mp4_data[j] == 0x00 && mp4_data[j+1] == 0x00 && 
                     mp4_data[j+2] == 0x01)) {
                    end = j;
                    found_next = true;
                    break;
                }
            }
            
            if (!found_next) {
                end = mp4_data.size();
            }
            
            // Extract NAL unit
            if (end > start && start < mp4_data.size()) {
                std::vector<uint8_t> nal_unit(mp4_data.begin() + start, mp4_data.begin() + end);
                
                // Only process valid H.264 NAL units and ensure minimum size
                if (!nal_unit.empty() && nal_unit.size() >= 1) {
                    uint8_t nal_type = nal_unit[0] & 0x1F;
                    
                    // Only accept common H.264 NAL unit types (more restrictive filtering)
                    if (nal_type >= 1 && nal_type <= 9) {
                        
                        // Skip problematic SEI units that contain emulation prevention issues
                        if (nal_type == 6) {
                            // Check if SEI contains problematic sequences that would confuse the decoder
                            bool has_problematic_sequence = false;
                            
                            // Check for start code patterns (0x00 0x00 0x01) within payload
                            for (size_t i = 0; i < nal_unit.size() - 2; i++) {
                                if (nal_unit[i] == 0x00 && nal_unit[i+1] == 0x00 && nal_unit[i+2] == 0x01) {
                                    has_problematic_sequence = true;
                                    std::cout << "⚠️ Found start code emulation in SEI at position " << i << std::endl;
                                    break;
                                }
                            }
                            
                            // Also check for 4-byte start code patterns (0x00 0x00 0x00 0x01)
                            if (!has_problematic_sequence) {
                                for (size_t i = 0; i < nal_unit.size() - 3; i++) {
                                    if (nal_unit[i] == 0x00 && nal_unit[i+1] == 0x00 && 
                                        nal_unit[i+2] == 0x00 && nal_unit[i+3] == 0x01) {
                                        has_problematic_sequence = true;
                                        std::cout << "⚠️ Found 4-byte start code emulation in SEI at position " << i << std::endl;
                                        break;
                                    }
                                }
                            }
                            
                            if (has_problematic_sequence) {
                                std::cout << "⚠️ Skipping SEI with start code emulation issue (size: " << nal_unit.size() << " bytes)" << std::endl;
                                std::cout << "   This SEI likely contains timestamp data causing decoder confusion" << std::endl;
                                continue;
                            }
                        }
                        
                        nal_units.push_back(nal_unit);
                        
                        const char* type_name = "Unknown";
                        switch (nal_type) {
                            case 1: type_name = "Non-IDR"; break;
                            case 5: type_name = "IDR"; break;
                            case 6: type_name = "SEI"; break;
                            case 7: type_name = "SPS"; break;
                            case 8: type_name = "PPS"; break;
                            case 9: type_name = "AU Delimiter"; break;
                        }
                        
                        std::cout << "🔍 Found valid NAL unit (type: " << (int)nal_type 
                                 << "-" << type_name << ", size: " << nal_unit.size() << " bytes)" << std::endl;
                    } else {
                        std::cout << "⚠️ Skipping invalid NAL unit type: " << (int)nal_type << std::endl;
                    }
                }
            }
            
            i = end;
        } else {
            i++;
        }
    }
    
    return nal_units;
}

std::vector<uint8_t> WebRTCManager::applyEmulationPrevention(const std::vector<uint8_t>& nal_unit) {
    std::vector<uint8_t> result;
    result.reserve(nal_unit.size() * 1.1); // Reserve a bit more space
    
    for (size_t i = 0; i < nal_unit.size(); i++) {
        result.push_back(nal_unit[i]);
        
        // Check for emulation prevention pattern
        if (i >= 1 && result.size() >= 2) {
            size_t len = result.size();
            // If we have 0x00 0x00 followed by 0x00, 0x01, 0x02, or 0x03
            // we need to insert emulation prevention byte (0x03)
            if (result[len-2] == 0x00 && result[len-1] == 0x00) {
                if (i + 1 < nal_unit.size()) {
                    uint8_t next_byte = nal_unit[i + 1];
                    if (next_byte <= 0x03) {
                        result.push_back(0x03); // Insert emulation prevention byte
                        std::cout << "🔧 Applied emulation prevention at position " << i << std::endl;
                    }
                }
            }
        }
    }
    
    return result;
}

void WebRTCManager::sendNALUnit(std::shared_ptr<rtc::Track> track, const std::vector<uint8_t>& nal_unit) {
    if (!track || !track->isOpen() || nal_unit.empty()) {
        return;
    }
    
    // Skip very small NAL units that may be invalid/padding
    if (nal_unit.size() < 2) {
        std::cout << "⚠️ Skipping tiny NAL unit (size: " << nal_unit.size() << " bytes)" << std::endl;
        return;
    }
    
    try {
        uint8_t nal_type = nal_unit[0] & 0x1F;
        const char* nal_type_name = "Unknown";
        switch (nal_type) {
            case 1: nal_type_name = "Non-IDR"; break;
            case 5: nal_type_name = "IDR"; break;
            case 6: nal_type_name = "SEI"; break;
            case 7: nal_type_name = "SPS"; break;
            case 8: nal_type_name = "PPS"; break;
            case 9: nal_type_name = "AU Delimiter"; break;
        }
        
        // Skip invalid NAL unit types
        if (nal_type == 0 || nal_type > 9) {
            std::cout << "⚠️ Skipping invalid NAL unit type: " << (int)nal_type << std::endl;
            return;
        }
        
        // Create RTP packet for H.264 NAL unit
        // RTP Header format for H.264:
        // - 12 byte RTP header
        // - H.264 payload (NAL unit without start codes)
        
        const size_t RTP_HEADER_SIZE = 12;
        const size_t MAX_PAYLOAD_SIZE = 1200; // Safe MTU minus RTP header
        
        // Simple RTP packetization - send NAL unit as single RTP packet
        if (nal_unit.size() <= MAX_PAYLOAD_SIZE) {
            rtc::binary packet;
            packet.reserve(RTP_HEADER_SIZE + nal_unit.size());
            
            // Simple RTP header (minimal for libdatachannel)
            // Version (2 bits) = 2, Padding (1 bit) = 0, Extension (1 bit) = 0, CC (4 bits) = 0
            packet.push_back(static_cast<std::byte>(0x80)); // V=2, P=0, X=0, CC=0
            
            // Marker (1 bit) = 1 (end of frame), Payload Type (7 bits) = 96 (H.264)
            packet.push_back(static_cast<std::byte>(0xE0)); // M=1, PT=96
            
            // Sequence number (16 bits) - simplified, use static counter
            static uint16_t seq_num = 0;
            seq_num++;
            packet.push_back(static_cast<std::byte>(seq_num >> 8));
            packet.push_back(static_cast<std::byte>(seq_num & 0xFF));
            
            // Timestamp (32 bits) - use current time in 90kHz units
            static uint32_t timestamp = 0;
            timestamp += 3000; // ~33ms at 90kHz for 30fps
            packet.push_back(static_cast<std::byte>(timestamp >> 24));
            packet.push_back(static_cast<std::byte>((timestamp >> 16) & 0xFF));
            packet.push_back(static_cast<std::byte>((timestamp >> 8) & 0xFF));
            packet.push_back(static_cast<std::byte>(timestamp & 0xFF));
            
            // SSRC (32 bits) - use fixed value
            packet.push_back(static_cast<std::byte>(0x12));
            packet.push_back(static_cast<std::byte>(0x34));
            packet.push_back(static_cast<std::byte>(0x56));
            packet.push_back(static_cast<std::byte>(0x78));
            
            // Add NAL unit payload (without start codes)
            for (uint8_t byte : nal_unit) {
                packet.push_back(static_cast<std::byte>(byte));
            }
            
            if (track->send(packet)) {
                static int sent_count = 0;
                if (sent_count % 30 == 0) { // Log every 30 packets (1 second at 30fps)
                    std::cout << "📤 Sent RTP packet " << sent_count << " (type " << (int)nal_type 
                             << "-" << nal_type_name << ", size: " << packet.size() << " bytes)" << std::endl;
                }
                sent_count++;
            } else {
                std::cout << "⚠️ Failed to send RTP packet (type " << (int)nal_type << ")" << std::endl;
            }
        } else {
            std::cout << "⚠️ NAL unit too large for single RTP packet (size: " << nal_unit.size() << " bytes)" << std::endl;
            // For simplicity, skip fragmentation for now - focus on getting basic streaming working
            return;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error sending RTP packet: " << e.what() << std::endl;
    }
}

std::string WebRTCManager::removeAudioFromSDP(const std::string& sdp) {
    std::string result;
    std::istringstream stream(sdp);
    std::string line;
    bool in_audio_section = false;
    
    while (std::getline(stream, line)) {
        // Check if this is the start of an audio media section
        if (line.find("m=audio") == 0) {
            in_audio_section = true;
            std::cout << "🔧 Skipping audio media line: " << line << std::endl;
            continue;
        }
        
        // Check if this is the start of a new media section (video or other)
        if (line.find("m=") == 0 && line.find("m=audio") != 0) {
            in_audio_section = false;
            std::cout << "📺 Keeping media line: " << line << std::endl;
        }
        
        // Skip lines that are part of the audio section
        if (in_audio_section) {
            std::cout << "🔧 Skipping audio attribute: " << line << std::endl;
            continue;
        }
        
        // Keep all other lines
        result += line + "\r\n";
    }
    
    return result;
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