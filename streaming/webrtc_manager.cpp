#include "webrtc_manager.hpp"
#include <iostream>
#include <sstream>

#ifdef WEBRTC_ENABLED

WebRTCManager::WebRTCManager(const std::string& thing_name, PublishCallback publish_cb) 
    : thing_name_(thing_name), publish_callback_(publish_cb) {
    std::cout << "✅ WebRTC Manager initialized with libdatachannel" << std::endl;
}

WebRTCManager::~WebRTCManager() {
    // Close all peer connections
    for (auto& [peer_id, pc] : peer_connections_) {
        if (pc) {
            pc->close();
        }
    }
    peer_connections_.clear();
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
    pc->onStateChange([peer_id](rtc::PeerConnection::State state) {
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
                break;
            case rtc::PeerConnection::State::Disconnected:
                std::cout << "Disconnected" << std::endl;
                break;
            case rtc::PeerConnection::State::Failed:
                std::cout << "Failed" << std::endl;
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
    
    // Set up ICE candidate handling
    setupICEHandling(peer_id, pc);
    
    return pc;
}

void WebRTCManager::setupICEHandling(const std::string& peer_id, std::shared_ptr<rtc::PeerConnection> pc) {
    pc->onLocalCandidate([this, peer_id](rtc::Candidate candidate) {
        std::cout << "🧊 Local ICE candidate for " << peer_id << ": " << candidate.candidate() << std::endl;
        
        // In a real implementation, you might want to send ICE candidates separately
        // For now, we'll include them in the answer
    });
    
    pc->onLocalDescription([this, peer_id](rtc::Description description) {
        std::cout << "📤 Local description ready for " << peer_id << std::endl;
        
        // Publish answer to MQTT
        std::string answer_topic = thing_name_ + "/" + peer_id + "/answer";
        
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
        
        // The answer will be automatically generated and published via onLocalDescription callback
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error handling offer for " << peer_id << ": " << e.what() << std::endl;
        return false;
    }
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

bool WebRTCManager::isWebRTCEnabled() const {
    return true;
}

#else

// Mock implementation when WebRTC is disabled
MockWebRTCManager::MockWebRTCManager(const std::string& thing_name, PublishCallback publish_cb) 
    : thing_name_(thing_name), publish_callback_(publish_cb) {
    std::cout << "⚠️ WebRTC Manager initialized in MOCK mode (libdatachannel not available)" << std::endl;
}

bool MockWebRTCManager::handleOffer(const std::string& peer_id, const std::string& offer_sdp) {
    std::cout << "🤖 MOCK: Handling offer for peer " << peer_id << std::endl;
    
    // Send mock answer
    std::string answer_topic = thing_name_ + "/" + peer_id + "/answer";
    std::string mock_answer = "{\"connected\": true, \"mock\": true, \"message\": \"WebRTC not available\"}";
    
    if (publish_callback_) {
        publish_callback_(answer_topic, mock_answer);
        std::cout << "✅ Mock answer published for peer " << peer_id << std::endl;
    }
    
    return true;
}

void MockWebRTCManager::closePeerConnection(const std::string& peer_id) {
    std::cout << "🔒 MOCK: Closed peer connection for " << peer_id << std::endl;
}

#endif