#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>

#ifdef WEBRTC_ENABLED
#include <rtc/rtc.hpp>
#endif

class WebRTCManager {
public:
    // Callback type for publishing MQTT messages
    using PublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
    
    WebRTCManager(const std::string& thing_name, PublishCallback publish_cb);
    ~WebRTCManager();
    
    // Handle incoming offer and create peer connection
    bool handleOffer(const std::string& peer_id, const std::string& offer_sdp);
    
    // Cleanup peer connection
    void closePeerConnection(const std::string& peer_id);
    
    // Get status
    bool isWebRTCEnabled() const;
    
private:
    std::string thing_name_;
    PublishCallback publish_callback_;
    
#ifdef WEBRTC_ENABLED
    // Store peer connections by peerId
    std::map<std::string, std::shared_ptr<rtc::PeerConnection>> peer_connections_;
    
    // WebRTC configuration
    rtc::Configuration getRTCConfig();
    
    // Create peer connection for specific peerId
    std::shared_ptr<rtc::PeerConnection> createPeerConnection(const std::string& peer_id);
    
    // Handle ICE candidates
    void setupICEHandling(const std::string& peer_id, std::shared_ptr<rtc::PeerConnection> pc);
#endif
};

// Mock implementation when WebRTC is disabled
#ifdef WEBRTC_DISABLED
class MockWebRTCManager {
public:
    using PublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
    
    MockWebRTCManager(const std::string& thing_name, PublishCallback publish_cb);
    bool handleOffer(const std::string& peer_id, const std::string& offer_sdp);
    void closePeerConnection(const std::string& peer_id);
    bool isWebRTCEnabled() const { return false; }
    
private:
    std::string thing_name_;
    PublishCallback publish_callback_;
};
#endif