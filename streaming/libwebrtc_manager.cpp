#include "libwebrtc_manager.hpp"
#include <iostream>
#include <thread>

// Observer implementations
class LibWebRTCManager::PeerConnectionObserver : public webrtc::PeerConnectionObserver {
public:
    PeerConnectionObserver(LibWebRTCManager* manager, const std::string& peer_id)
        : manager_(manager), peer_id_(peer_id) {}
        
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {
        std::cout << "🔄 Peer " << peer_id_ << " signaling state: " << static_cast<int>(new_state) << std::endl;
    }
    
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override {
        std::cout << "🔗 Peer " << peer_id_ << " connection state: " << static_cast<int>(new_state) << std::endl;
        manager_->onConnectionStateChange(peer_id_, new_state);
    }
    
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        std::cout << "🧊 ICE candidate generated for " << peer_id_ << std::endl;
        manager_->onIceCandidate(peer_id_, candidate);
    }
    
    // Other required overrides (minimal implementation)
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
    void OnRenegotiationNeeded() override {}
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
    void OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override {}
                    
private:
    LibWebRTCManager* manager_;
    std::string peer_id_;
};

class LibWebRTCManager::CreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
public:
    CreateSessionDescriptionObserver(LibWebRTCManager* manager, const std::string& peer_id)
        : manager_(manager), peer_id_(peer_id) {}
        
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        std::cout << "✅ SDP answer created successfully for " << peer_id_ << std::endl;
        
        // Set local description
        auto pc = manager_->peer_connections_[peer_id_];
        if (pc) {
            auto set_observer = new rtc::RefCountedObject<SetSessionDescriptionObserver>(manager_, peer_id_);
            pc->SetLocalDescription(set_observer, desc);
        }
    }
    
    void OnFailure(webrtc::RTCError error) override {
        std::cerr << "❌ Failed to create SDP answer for " << peer_id_ << ": " << error.message() << std::endl;
    }
    
private:
    LibWebRTCManager* manager_;
    std::string peer_id_;
};

class LibWebRTCManager::SetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
public:
    SetSessionDescriptionObserver(LibWebRTCManager* manager, const std::string& peer_id)
        : manager_(manager), peer_id_(peer_id) {}
        
    void OnSuccess() override {
        std::cout << "✅ Local/Remote description set successfully for " << peer_id_ << std::endl;
        
        // If this is setting local description (answer), publish it
        auto pc = manager_->peer_connections_[peer_id_];
        if (pc && pc->local_description()) {
            std::string sdp;
            pc->local_description()->ToString(&sdp);
            
            // Publish answer (like robot_simulator does)
            std::string answer_topic = manager_->thing_name_ + "/robot-control/" + peer_id_ + "/answer";
            std::cout << "📡 Publishing answer to MQTT topic: " << answer_topic << std::endl;
            
            if (manager_->publish_callback_) {
                manager_->publish_callback_(answer_topic, sdp);
            }
        }
    }
    
    void OnFailure(webrtc::RTCError error) override {
        std::cerr << "❌ Failed to set session description for " << peer_id_ << ": " << error.message() << std::endl;
    }
    
private:
    LibWebRTCManager* manager_;
    std::string peer_id_;
};

LibWebRTCManager::LibWebRTCManager(const std::string& thing_name, PublishCallback publish_callback)
    : thing_name_(thing_name), publish_callback_(publish_callback) {
    std::cout << "🚀 LibWebRTCManager initialized (using Google's libwebrtc like robot_simulator)" << std::endl;
}

LibWebRTCManager::~LibWebRTCManager() {
    std::cout << "🛑 LibWebRTCManager destructor" << std::endl;
}

bool LibWebRTCManager::initialize() {
    std::cout << "🔧 Initializing WebRTC factory..." << std::endl;
    
    // Create PeerConnectionFactory (equivalent to WebRTC initialization in Flutter)
    peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
        nullptr, // network_thread
        nullptr, // worker_thread
        nullptr, // signaling_thread
        nullptr, // default_adm
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        webrtc::CreateBuiltinVideoEncoderFactory(),
        webrtc::CreateBuiltinVideoDecoderFactory(),
        nullptr, // audio_mixer
        nullptr  // audio_processing
    );
    
    if (!peer_connection_factory_) {
        std::cerr << "❌ Failed to create PeerConnectionFactory" << std::endl;
        return false;
    }
    
    std::cout << "✅ PeerConnectionFactory created successfully" << std::endl;
    return true;
}

bool LibWebRTCManager::getUserMedia() {
    std::cout << "📷 getUserMedia() - Starting camera capture (like robot_simulator)" << std::endl;
    
    // Create video capturer (equivalent to camera access in getUserMedia)
    video_capturer_ = webrtc::test::VcmCapturer::Create(320, 240, 30, 0); // width, height, fps, device_id
    if (!video_capturer_) {
        std::cerr << "❌ Failed to create video capturer (camera not available)" << std::endl;
        return false;
    }
    
    // Create video source
    video_source_ = new rtc::RefCountedObject<webrtc::VideoTrackSource>(video_capturer_.get());
    if (!video_source_) {
        std::cerr << "❌ Failed to create video source" << std::endl;
        return false;
    }
    
    // Create video track (like robot_simulator's MediaStreamTrack)
    video_track_ = peer_connection_factory_->CreateVideoTrack("video_track", video_source_);
    if (!video_track_) {
        std::cerr << "❌ Failed to create video track" << std::endl;
        return false;
    }
    
    // Create local stream (like robot_simulator's MediaStream)
    local_stream_ = peer_connection_factory_->CreateLocalMediaStream("local_stream");
    if (!local_stream_) {
        std::cerr << "❌ Failed to create local media stream" << std::endl;
        return false;
    }
    
    // Add video track to stream (equivalent to robot_simulator's approach)
    if (!local_stream_->AddTrack(video_track_)) {
        std::cerr << "❌ Failed to add video track to local stream" << std::endl;
        return false;
    }
    
    std::cout << "✅ getUserMedia() successful - Local MediaStream with video track created" << std::endl;
    std::cout << "📺 Camera capture started (320x240 @ 30fps)" << std::endl;
    return true;
}

bool LibWebRTCManager::handleOffer(const std::string& peer_id, const std::string& offer_sdp) {
    std::cout << "📥 Handling WebRTC offer for peer: " << peer_id << std::endl;
    
    try {
        // Create PeerConnection for this peer
        auto pc = createPeerConnection(peer_id);
        if (!pc) {
            std::cerr << "❌ Failed to create PeerConnection for " << peer_id << std::endl;
            return false;
        }
        
        // Add local stream to PeerConnection (like robot_simulator's pc.addTrack)
        if (!local_stream_) {
            std::cerr << "❌ No local stream available - call getUserMedia() first" << std::endl;
            return false;
        }
        
        // Add all tracks from local stream (equivalent to robot_simulator's loop)
        auto video_tracks = local_stream_->GetVideoTracks();
        for (auto& track : video_tracks) {
            auto result = pc->AddTrack(track, {local_stream_});
            if (result.ok()) {
                std::cout << "✅ Video track added to PeerConnection for " << peer_id << std::endl;
            } else {
                std::cerr << "❌ Failed to add video track: " << result.error().message() << std::endl;
            }
        }
        
        // Set remote description (offer)
        std::cout << "📝 Setting remote description (offer)..." << std::endl;
        auto desc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, offer_sdp);
        if (!desc) {
            std::cerr << "❌ Failed to parse offer SDP" << std::endl;
            return false;
        }
        
        auto set_observer = new rtc::RefCountedObject<SetSessionDescriptionObserver>(this, peer_id);
        pc->SetRemoteDescription(set_observer, desc.release());
        
        // Create answer
        std::cout << "🔄 Creating answer..." << std::endl;
        auto create_observer = new rtc::RefCountedObject<CreateSessionDescriptionObserver>(this, peer_id);
        pc->CreateAnswer(create_observer, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
        
        std::cout << "✅ Offer handling initiated for " << peer_id << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in handleOffer: " << e.what() << std::endl;
        return false;
    }
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface> LibWebRTCManager::createPeerConnection(const std::string& peer_id) {
    // WebRTC configuration (same STUN servers as robot_simulator)
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    webrtc::PeerConnectionInterface::IceServer stun_server;
    stun_server.urls.push_back("stun:stun.l.google.com:19302");
    stun_server.urls.push_back("stun:stun1.l.google.com:19302");
    config.servers.push_back(stun_server);
    
    // Create observer
    auto observer = std::make_unique<PeerConnectionObserver>(this, peer_id);
    
    // Create PeerConnection
    auto pc = peer_connection_factory_->CreatePeerConnection(config, nullptr, nullptr, observer.release());
    if (pc) {
        peer_connections_[peer_id] = pc;
        std::cout << "✅ PeerConnection created for " << peer_id << std::endl;
    }
    
    return pc;
}

void LibWebRTCManager::onIceCandidate(const std::string& peer_id, const webrtc::IceCandidateInterface* candidate) {
    std::cout << "🧊 Publishing ICE candidate for " << peer_id << std::endl;
    
    // Convert to JSON and publish (like robot_simulator)
    std::string sdp;
    if (candidate->ToString(&sdp)) {
        // Create JSON array with single candidate
        std::string candidate_json = "[{\"candidate\":\"" + sdp + 
                                   "\",\"sdpMid\":\"" + candidate->sdp_mid() + 
                                   "\",\"sdpMLineIndex\":" + std::to_string(candidate->sdp_mline_index()) + "}]";
        
        std::string topic = thing_name_ + "/robot-control/" + peer_id + "/candidate/rmcs";
        if (publish_callback_) {
            publish_callback_(topic, candidate_json);
        }
    }
}

void LibWebRTCManager::onConnectionStateChange(const std::string& peer_id, webrtc::PeerConnectionInterface::PeerConnectionState state) {
    switch (state) {
        case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
            std::cout << "✅ WebRTC connected for " << peer_id << " - Video streaming active!" << std::endl;
            break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
            std::cout << "⚠️ WebRTC disconnected for " << peer_id << std::endl;
            break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
            std::cout << "❌ WebRTC connection failed for " << peer_id << std::endl;
            break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
            std::cout << "🛑 WebRTC connection closed for " << peer_id << std::endl;
            break;
        default:
            std::cout << "🔄 WebRTC state change for " << peer_id << ": " << static_cast<int>(state) << std::endl;
            break;
    }
}

bool LibWebRTCManager::handleCandidates(const std::string& peer_id, const Json::Value& candidates) {
    std::cout << "🧊 Handling ICE candidates for " << peer_id << std::endl;
    
    auto pc = peer_connections_.find(peer_id);
    if (pc == peer_connections_.end()) {
        std::cerr << "❌ No PeerConnection found for " << peer_id << std::endl;
        return false;
    }
    
    for (const auto& candidate : candidates) {
        if (candidate.isMember("candidate") && candidate.isMember("sdpMid") && candidate.isMember("sdpMLineIndex")) {
            auto ice_candidate = webrtc::CreateIceCandidate(
                candidate["sdpMid"].asString(),
                candidate["sdpMLineIndex"].asInt(),
                candidate["candidate"].asString(),
                nullptr
            );
            
            if (ice_candidate && pc->second->AddIceCandidate(ice_candidate.get())) {
                std::cout << "✅ ICE candidate added successfully" << std::endl;
            } else {
                std::cerr << "❌ Failed to add ICE candidate" << std::endl;
            }
        }
    }
    
    return true;
}

void LibWebRTCManager::closePeerConnection(const std::string& peer_id) {
    auto it = peer_connections_.find(peer_id);
    if (it != peer_connections_.end()) {
        it->second->Close();
        peer_connections_.erase(it);
        std::cout << "🛑 PeerConnection closed for " << peer_id << std::endl;
    }
}