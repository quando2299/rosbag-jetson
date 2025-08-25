#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>

#ifdef WEBRTC_ENABLED
#include <rtc/rtc.hpp>
#include <thread>
#include <atomic>
#include <fstream>
#include <vector>
#include <opencv2/opencv.hpp>
#include <rtc/h264rtppacketizer.hpp>
#endif

#include <json/json.h>

class WebRTCManager {
public:
    // Callback type for publishing MQTT messages
    using PublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
    
    WebRTCManager(const std::string& thing_name, PublishCallback publish_cb);
    ~WebRTCManager();
    
    // Handle incoming offer and create peer connection
    bool handleOffer(const std::string& peer_id, const std::string& offer_sdp);
    
    // Handle ICE candidates array and republish
    bool handleCandidates(const std::string& peer_id, const Json::Value& candidates);
    
    // Cleanup peer connection
    void closePeerConnection(const std::string& peer_id);
    
    // Start live image streaming
    bool startVideoStreaming(const std::string& peer_id, const std::string& images_dir_path);
    
    // Start H264 file streaming
    bool startH264FileStreaming(const std::string& peer_id, const std::string& h264_file_path);
    
    // Stop video streaming
    void stopVideoStreaming(const std::string& peer_id);
    
    // Get status
    bool isWebRTCEnabled() const;
    
    // Helper function to find video file
    std::string findVideoFile();
    
private:
    std::string thing_name_;
    PublishCallback publish_callback_;
    
#ifdef WEBRTC_ENABLED
    // Store peer connections by peerId
    std::map<std::string, std::shared_ptr<rtc::PeerConnection>> peer_connections_;
    
    // Store video tracks by peerId
    std::map<std::string, std::shared_ptr<rtc::Track>> video_tracks_;
    
    // Streaming control
    std::map<std::string, std::atomic<bool>> streaming_active_;
    std::map<std::string, std::thread> streaming_threads_;
    
    // WebRTC configuration
    rtc::Configuration getRTCConfig();
    
    // Create peer connection for specific peerId
    std::shared_ptr<rtc::PeerConnection> createPeerConnection(const std::string& peer_id);
    
    // Handle ICE candidates
    void setupICEHandling(const std::string& peer_id, std::shared_ptr<rtc::PeerConnection> pc);
    
    // Live image streaming methods
    void streamImagesFromDirectory(const std::string& peer_id, const std::string& images_dir);
    std::vector<std::string> getImageFiles(const std::string& directory);
    cv::Mat loadAndResizeImage(const std::string& image_path);
    void sendH264Frame(std::shared_ptr<rtc::Track> track, const cv::Mat& frame);
    std::vector<uint8_t> encodeFrameToH264(const cv::Mat& frame);
#endif
};

// Mock implementation when WebRTC is disabled
#ifndef WEBRTC_ENABLED
class MockWebRTCManager {
public:
    using PublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
    
    MockWebRTCManager(const std::string& thing_name, PublishCallback publish_cb);
    bool handleOffer(const std::string& peer_id, const std::string& offer_sdp);
    bool handleCandidates(const std::string& peer_id, const Json::Value& candidates);
    bool startVideoStreaming(const std::string& peer_id, const std::string& images_dir_path);
    void stopVideoStreaming(const std::string& peer_id);
    void closePeerConnection(const std::string& peer_id);
    bool isWebRTCEnabled() const { return false; }
    
private:
    std::string thing_name_;
    PublishCallback publish_callback_;
};
#endif