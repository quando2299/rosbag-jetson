#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <mosquitto.h>
#include <json/json.h>
#include <rtc/rtc.hpp>
#include "h264fileparser.hpp"
#include <signal.h>

using namespace std;
using namespace rtc;

class MQTTWebRTCClient {
private:
    // Configuration
    const string mqtt_broker = "test.rmcs.d6-vnext.com";
    const int mqtt_port = 1883;
    const string thing_name = "vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af";
    const string mqtt_username = "vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af";
    const string mqtt_password = "7#TlDprf";
    const string h264_base_dir = "/app/h264_samples";
    
    // MQTT client
    struct mosquitto* mosq = nullptr;
    
    // WebRTC connections
    map<string, shared_ptr<PeerConnection>> peer_connections;
    map<string, shared_ptr<Track>> video_tracks;
    map<string, atomic<bool>> streaming_active;
    map<string, thread> streaming_threads;
    map<string, vector<string>> local_candidates; // Store local candidates per peer
    
    // H264 file parser
    shared_ptr<H264FileParser> h264_parser;
    
    // Static callbacks for mosquitto
    static MQTTWebRTCClient* instance;
    
    static void on_connect_callback(struct mosquitto* mosq, void* userdata, int result) {
        if (instance) instance->on_connect(result);
    }
    
    static void on_message_callback(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
        if (instance) instance->on_message(message);
    }
    
    // Helper functions
    string extract_peer_id(const string& topic) {
        size_t start = topic.find("/robot-control/");
        if (start == string::npos) return "";
        start += 15;
        size_t end = topic.find("/", start);
        if (end == string::npos) return "";
        return topic.substr(start, end - start);
    }
    
    void mqtt_publish(const string& topic, const string& message) {
        cout << "[MQTT] Publishing to: " << topic << endl;
        int ret = mosquitto_publish(mosq, nullptr, topic.c_str(), 
                                   message.length(), message.c_str(), 0, false);
        if (ret != MOSQ_ERR_SUCCESS) {
            cerr << "[MQTT] Publish failed: " << mosquitto_strerror(ret) << endl;
        }
    }
    
    void initialize_h264_parser() {
        try {
            cout << "[H264] Initializing parser with directory: " << h264_base_dir << endl;
            h264_parser = make_shared<H264FileParser>(h264_base_dir, 30, true);
            h264_parser->start();
            cout << "[H264] Parser initialized successfully" << endl;
            
            // Test first sample
            auto sample = h264_parser->getSample();
            cout << "[H264] First sample size: " << sample.size() << " bytes" << endl;
        } catch (const exception& e) {
            cerr << "[H264] Parser initialization error: " << e.what() << endl;
        }
    }
    
    void stream_h264_to_peer(const string& peer_id) {
        cout << "[Stream] Starting H264 streaming for peer: " << peer_id << endl;
        
        auto track_it = video_tracks.find(peer_id);
        if (track_it == video_tracks.end()) {
            cerr << "[Stream] No video track found for peer: " << peer_id << endl;
            return;
        }
        
        auto track = track_it->second;
        
        if (!h264_parser) {
            cerr << "[Stream] H264 parser not initialized" << endl;
            return;
        }
        
        streaming_active[peer_id] = true;
        uint32_t frame_count = 0;
        
        // Send initial NALU units (SPS/PPS/IDR)
        try {
            auto initial_nalus = h264_parser->initialNALUS();
            if (!initial_nalus.empty()) {
                track->send(initial_nalus);
                cout << "[Stream] Sent initial NALUs (" << initial_nalus.size() << " bytes) to " << peer_id << endl;
            }
        } catch (const exception& e) {
            cerr << "[Stream] Error sending initial NALUs: " << e.what() << endl;
        }
        
        while (streaming_active[peer_id] && peer_connections.find(peer_id) != peer_connections.end()) {
            try {
                // Get next H264 sample
                auto sample = h264_parser->getSample();
                
                if (frame_count % 30 == 0) {  // Log every second at 30fps
                    cout << "[Stream] Frame " << frame_count << " sample size: " << sample.size() << " bytes" << endl;
                }
                
                if (!sample.empty()) {
                    // Send frame
                    track->send(sample);
                    
                    if (frame_count % 30 == 0) {  // Log every second at 30fps
                        cout << "[Stream] Sent frame " << frame_count << " (" << sample.size() << " bytes) to " << peer_id << endl;
                    }
                    frame_count++;
                } else {
                    if (frame_count % 30 == 0) {
                        cout << "[Stream] Empty sample at frame " << frame_count << endl;
                    }
                    frame_count++;
                }
                
                // Load next sample
                h264_parser->loadNextSample();
                
                // Wait for next frame (30 FPS)
                this_thread::sleep_for(chrono::milliseconds(33));
                
            } catch (const exception& e) {
                cerr << "[Stream] Error sending frame: " << e.what() << endl;
            }
        }
        
        cout << "[Stream] Stopped streaming for peer: " << peer_id << endl;
        streaming_active[peer_id] = false;
    }
    
    void create_peer_connection(const string& peer_id, const string& offer_sdp) {
        cout << "[WebRTC] Creating peer connection for: " << peer_id << endl;
        
        // Configure WebRTC
        Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        config.disableAutoNegotiation = true;
        
        auto pc = make_shared<PeerConnection>(config);
        peer_connections[peer_id] = pc;
        
        // Add video track
        auto video = Description::Video("video-stream");
        video.addH264Codec(102, "profile-level-id=42c015;packetization-mode=1");
        video.addSSRC(1, "video-stream", "stream1", "video");
        auto track = pc->addTrack(video);
        
        // Configure RTP
        auto rtpConfig = make_shared<RtpPacketizationConfig>(1, "video", 102, 90000);
        auto packetizer = make_shared<H264RtpPacketizer>(H264RtpPacketizer::Separator::Length, rtpConfig);
        track->setMediaHandler(packetizer);
        
        video_tracks[peer_id] = track;
        
        // Handle state changes
        pc->onStateChange([this, peer_id](PeerConnection::State state) {
            cout << "[WebRTC] State for " << peer_id << ": " << static_cast<int>(state) << endl;
            
            if (state == PeerConnection::State::Connected) {
                // Start streaming in a separate thread
                if (streaming_threads.find(peer_id) != streaming_threads.end() && streaming_threads[peer_id].joinable()) {
                    streaming_threads[peer_id].join();
                }
                streaming_threads[peer_id] = thread(&MQTTWebRTCClient::stream_h264_to_peer, this, peer_id);
            } else if (state == PeerConnection::State::Disconnected || 
                      state == PeerConnection::State::Failed ||
                      state == PeerConnection::State::Closed) {
                // Stop streaming and cleanup
                streaming_active[peer_id] = false;
                if (streaming_threads.find(peer_id) != streaming_threads.end() && streaming_threads[peer_id].joinable()) {
                    streaming_threads[peer_id].join();
                }
                peer_connections.erase(peer_id);
                video_tracks.erase(peer_id);
            }
        });
        
        // Handle gathering state
        pc->onGatheringStateChange([this, peer_id, pc](PeerConnection::GatheringState state) {
            cout << "[WebRTC] Gathering state for " << peer_id << ": " << static_cast<int>(state) << endl;
            
            if (state == PeerConnection::GatheringState::Complete) {
                auto desc = pc->localDescription();
                if (desc) {
                    // Send answer as raw SDP string (not JSON)
                    string sdp_string = string(*desc);
                    string topic = thing_name + "/robot-control/" + peer_id + "/answer";
                    mqtt_publish(topic, sdp_string);
                }
            }
        });
        
        // Handle local candidates - store them for later use
        pc->onLocalCandidate([this, peer_id](Candidate candidate) {
            cout << "[WebRTC] Generated local candidate for " << peer_id << endl;
            local_candidates[peer_id].push_back(string(candidate));
        });
        
        // Set remote description and create answer
        pc->setRemoteDescription(Description(offer_sdp, "offer"));
        pc->setLocalDescription();
    }
    
    void on_connect(int result) {
        if (result == 0) {
            cout << "[MQTT] Connected to broker: " << mqtt_broker << ":" << mqtt_port << endl;
            
            // Subscribe to topics
            string offer_topic = thing_name + "/robot-control/+/offer";
            string candidate_topic = thing_name + "/robot-control/+/candidate/robot";
            
            mosquitto_subscribe(mosq, nullptr, offer_topic.c_str(), 0);
            mosquitto_subscribe(mosq, nullptr, candidate_topic.c_str(), 0);
            
            cout << "[MQTT] Subscribed to: " << offer_topic << endl;
            cout << "[MQTT] Subscribed to: " << candidate_topic << endl;
        } else {
            cerr << "[MQTT] Connection failed with code: " << result << endl;
        }
    }
    
    void on_message(const struct mosquitto_message* message) {
        string topic = message->topic;
        string payload(static_cast<char*>(message->payload), message->payloadlen);
        
        cout << "[MQTT] Message on topic: " << topic << endl;
        
        string peer_id = extract_peer_id(topic);
        if (peer_id.empty()) {
            cerr << "[MQTT] Could not extract peer ID from topic" << endl;
            return;
        }
        
        // Handle offer
        if (topic.find("/offer") != string::npos) {
            cout << "[MQTT] Processing offer from peer: " << peer_id << endl;
            
            string offer_sdp;
            if (payload[0] == '{') {
                // JSON format
                Json::Value root;
                Json::Reader reader;
                if (reader.parse(payload, root) && root.isMember("sdp")) {
                    offer_sdp = root["sdp"].asString();
                }
            } else {
                // Raw SDP
                offer_sdp = payload;
            }
            
            if (!offer_sdp.empty()) {
                create_peer_connection(peer_id, offer_sdp);
            }
        }
        // Handle ICE candidates
        else if (topic.find("/candidate/robot") != string::npos) {
            cout << "[MQTT] Processing ICE candidates for peer: " << peer_id << endl;
            
            auto pc_it = peer_connections.find(peer_id);
            if (pc_it != peer_connections.end()) {
                Json::Value root;
                Json::Reader reader;
                if (reader.parse(payload, root) && root.isArray()) {
                    for (const auto& ice : root) {
                        if (ice.isMember("candidate")) {
                            string candidate_str = ice["candidate"].asString();
                            pc_it->second->addRemoteCandidate(Candidate(candidate_str));
                            cout << "[WebRTC] Added remote ICE candidate for " << peer_id << endl;
                        }
                    }
                    
                    // Send our stored local candidates in response
                    auto local_cand_it = local_candidates.find(peer_id);
                    if (local_cand_it != local_candidates.end() && !local_cand_it->second.empty()) {
                        Json::Value response_ice_array(Json::arrayValue);
                        for (const auto& local_cand : local_cand_it->second) {
                            Json::Value response_ice_obj;
                            response_ice_obj["candidate"] = local_cand;
                            response_ice_obj["sdpMid"] = "0";
                            response_ice_obj["sdpMLineIndex"] = 0;
                            response_ice_array.append(response_ice_obj);
                        }
                        
                        Json::StreamWriterBuilder builder;
                        string response_message = Json::writeString(builder, response_ice_array);
                        
                        string response_topic = thing_name + "/robot-control/" + peer_id + "/candidate/rmcs";
                        mqtt_publish(response_topic, response_message);
                        
                        // Clear sent candidates
                        local_candidates[peer_id].clear();
                    }
                }
            }
        }
    }
    
public:
    MQTTWebRTCClient() {
        instance = this;
    }
    
    // Public accessor for mosquitto client
    struct mosquitto* getMosq() { return mosq; }
    
    ~MQTTWebRTCClient() {
        // Stop all streaming
        for (auto& [peer_id, active] : streaming_active) {
            active = false;
        }
        
        // Wait for threads to finish
        for (auto& [peer_id, thread] : streaming_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (mosq) {
            mosquitto_destroy(mosq);
        }
        mosquitto_lib_cleanup();
        instance = nullptr;
    }
    
    bool initialize() {
        cout << "[Init] MQTT WebRTC Streaming Client" << endl;
        cout << "[Init] H264 directory: " << h264_base_dir << endl;
        
        // Initialize H264 parser
        initialize_h264_parser();
        
        // Initialize libdatachannel
        rtcInitLogger(RTC_LOG_WARNING, nullptr);
        
        // Initialize mosquitto
        mosquitto_lib_init();
        mosq = mosquitto_new("m2m-robot-001", true, nullptr);
        
        if (!mosq) {
            cerr << "[Init] Failed to create mosquitto client" << endl;
            return false;
        }
        
        // Set callbacks
        mosquitto_connect_callback_set(mosq, on_connect_callback);
        mosquitto_message_callback_set(mosq, on_message_callback);
        
        // Set credentials
        mosquitto_username_pw_set(mosq, mqtt_username.c_str(), mqtt_password.c_str());
        
        // Connect to broker
        cout << "[MQTT] Connecting to broker..." << endl;
        int ret = mosquitto_connect(mosq, mqtt_broker.c_str(), mqtt_port, 60);
        if (ret != MOSQ_ERR_SUCCESS) {
            cerr << "[MQTT] Failed to connect: " << mosquitto_strerror(ret) << endl;
            return false;
        }
        
        return true;
    }
    
};

// Static instance pointer
MQTTWebRTCClient* MQTTWebRTCClient::instance = nullptr;

// Global signal handler
volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    cout << "\n[Signal] Received signal " << sig << ", shutting down..." << endl;
    running = 0;
}

int main() {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    MQTTWebRTCClient client;
    
    if (!client.initialize()) {
        cerr << "Failed to initialize client" << endl;
        return 1;
    }
    
    cout << "[Main] Starting main loop..." << endl;
    while (running) {
        mosquitto_loop(client.getMosq(), 100, 1);
    }
    
    cout << "[Main] Shutting down gracefully..." << endl;
    return 0;
}