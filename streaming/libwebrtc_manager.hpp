#pragma once

#include <string>
#include <memory>
#include <map>
#include <functional>

// Google libwebrtc includes (equivalent to robot_simulator's WebRTC)
#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"
#include "api/media_stream_interface.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "modules/video_capture/video_capture_factory.h"
#include "pc/video_track_source.h"
#include "test/vcm_capturer.h"

#ifdef JSON_ENABLED
#include <json/json.h>
#endif

// This class mimics robot_simulator's WebRTC approach using Google's libwebrtc
// It provides getUserMedia() equivalent functionality for C++
class LibWebRTCManager {
public:
    using PublishCallback = std::function<void(const std::string&, const std::string&)>;
    
    LibWebRTCManager(const std::string& thing_name, PublishCallback publish_callback);
    ~LibWebRTCManager();
    
    // Initialize WebRTC factory (equivalent to Flutter WebRTC initialization)
    bool initialize();
    
    // getUserMedia() equivalent - creates MediaStream with video track
    bool getUserMedia();
    
    // Handle WebRTC offer (like robot_simulator's handleOffer)
    bool handleOffer(const std::string& peer_id, const std::string& offer_sdp);
    
    // Handle ICE candidates
    bool handleCandidates(const std::string& peer_id, const Json::Value& candidates);
    
    // Cleanup
    void closePeerConnection(const std::string& peer_id);
    
private:
    std::string thing_name_;
    PublishCallback publish_callback_;
    
    // Core WebRTC objects (same as robot_simulator uses internally)
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
    rtc::scoped_refptr<webrtc::MediaStreamInterface> local_stream_;
    
    // Peer connections map
    std::map<std::string, rtc::scoped_refptr<webrtc::PeerConnectionInterface>> peer_connections_;
    
    // Video capture (equivalent to camera access in getUserMedia)
    std::unique_ptr<webrtc::test::VcmCapturer> video_capturer_;
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
    
    // Observer classes
    class PeerConnectionObserver;
    class CreateSessionDescriptionObserver;
    class SetSessionDescriptionObserver;
    
    // Helper methods
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> createPeerConnection(const std::string& peer_id);
    void onIceCandidate(const std::string& peer_id, const webrtc::IceCandidateInterface* candidate);
    void onConnectionStateChange(const std::string& peer_id, webrtc::PeerConnectionInterface::PeerConnectionState state);
};