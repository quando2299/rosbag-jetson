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
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/nalunit.hpp>
#include "h264fileparser.hpp"
#include "helpers.hpp"
#include <signal.h>

using namespace std;
using namespace rtc;
using namespace std::chrono;

template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }

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
    
    // WebRTC connections - using Client structure like the working example
    map<string, shared_ptr<Client>> clients;
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
        
        auto client_it = clients.find(peer_id);
        if (client_it == clients.end() || !client_it->second->video.has_value()) {
            cerr << "[Stream] No video track found for peer: " << peer_id << endl;
            return;
        }
        
        auto trackData = client_it->second->video.value();
        auto track = trackData->track;
        
        if (!h264_parser) {
            cerr << "[Stream] H264 parser not initialized" << endl;
            return;
        }
        
        streaming_active[peer_id] = true;
        uint32_t frame_count = 0;
        
        while (streaming_active[peer_id] && clients.find(peer_id) != clients.end()) {
            try {
                // Get next H264 sample
                auto sample = h264_parser->getSample();
                uint64_t sampleTime = h264_parser->getSampleTime_us();
                
                if (frame_count % 30 == 0) {  // Log every second at 30fps
                    cout << "[Stream] Frame " << frame_count << " sample size: " << sample.size() << " bytes" << endl;
                }
                
                if (!sample.empty()) {
                    // Send frame with proper timestamp like the working example
                    track->sendFrame(sample, chrono::duration<double, micro>(sampleTime));
                    
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
    
    shared_ptr<ClientTrackData> addVideo(shared_ptr<PeerConnection> pc, uint8_t payloadType, uint32_t ssrc, 
                                const string& cname, const string& msid, 
                                function<void()> onOpen) {
        auto video = Description::Video(cname);
        video.addH264Codec(payloadType);
        video.addSSRC(ssrc, cname, msid, cname);
        auto track = pc->addTrack(video);
        cout << "[DEBUG] Adding video track with PT=" << (int)payloadType << " SSRC=" << ssrc << endl;
        
        // create RTP configuration
        auto rtpConfig = make_shared<RtpPacketizationConfig>(ssrc, cname, payloadType, H264RtpPacketizer::ClockRate);
        // create packetizer
        auto packetizer = make_shared<H264RtpPacketizer>(NalUnit::Separator::Length, rtpConfig);
        // add RTCP SR handler
        auto srReporter = make_shared<RtcpSrReporter>(rtpConfig);
        packetizer->addToChain(srReporter);
        // add RTCP NACK handler
        auto nackResponder = make_shared<RtcpNackResponder>();
        packetizer->addToChain(nackResponder);
        // set handler
        track->setMediaHandler(packetizer);
        track->onOpen([onOpen]() {
            cout << "[DEBUG] Video track onOpen callback triggered!" << endl;
            onOpen();
        });
        auto trackData = make_shared<ClientTrackData>(track, srReporter);
        
        return trackData;
    }
    
    shared_ptr<ClientTrackData> addAudio(shared_ptr<PeerConnection> pc, uint8_t payloadType, uint32_t ssrc,
                               const string& cname, const string& msid,
                               function<void()> onOpen) {
        auto audio = Description::Audio(cname);
        audio.addOpusCodec(payloadType);
        audio.addSSRC(ssrc, cname, msid, cname);
        audio.setDirection(Description::Direction::SendOnly);  // We only send audio
        auto track = pc->addTrack(audio);
        cout << "[DEBUG] Adding audio track with PT=" << (int)payloadType << " SSRC=" << ssrc << endl;
        
        // create RTP configuration
        auto rtpConfig = make_shared<RtpPacketizationConfig>(ssrc, cname, payloadType, OpusRtpPacketizer::DefaultClockRate);
        // create packetizer
        auto packetizer = make_shared<OpusRtpPacketizer>(rtpConfig);
        // add RTCP SR handler
        auto srReporter = make_shared<RtcpSrReporter>(rtpConfig);
        packetizer->addToChain(srReporter);
        // add RTCP NACK handler
        auto nackResponder = make_shared<RtcpNackResponder>();
        packetizer->addToChain(nackResponder);
        // set handler
        track->setMediaHandler(packetizer);
        track->onOpen([onOpen]() {
            cout << "[DEBUG] Audio track onOpen callback triggered!" << endl;
            onOpen();
        });
        auto trackData = make_shared<ClientTrackData>(track, srReporter);
        
        return trackData;
    }
    
    void sendInitialNalus(shared_ptr<ClientTrackData> video) {
        if (!h264_parser || !video) return;
        
        auto initialNalus = h264_parser->initialNALUS();
        if (!initialNalus.empty()) {
            // Send initial NAL units with proper timestamp handling like the working example
            const double frameDuration_s = double(h264_parser->getSampleDuration_us()) / (1000 * 1000);
            const uint32_t frameTimestampDuration = video->sender->rtpConfig->secondsToTimestamp(frameDuration_s);
            video->sender->rtpConfig->timestamp = video->sender->rtpConfig->startTimestamp - frameTimestampDuration * 2;
            video->track->send(initialNalus);
            video->sender->rtpConfig->timestamp += frameTimestampDuration;
            // Send initial NAL units again to start stream in firefox browser
            video->track->send(initialNalus);
            cout << "[Stream] Sent initial NALUs (" << initialNalus.size() << " bytes)" << endl;
        }
    }
    
    // Add client to stream - similar to the working example
    void addToStream(shared_ptr<Client> client, bool isAddingVideo, const string& peer_id) {
        if (client->getState() == Client::State::Waiting) {
            client->setState(isAddingVideo ? Client::State::WaitingForAudio : Client::State::WaitingForVideo);
        } else if ((client->getState() == Client::State::WaitingForAudio && !isAddingVideo)
                   || (client->getState() == Client::State::WaitingForVideo && isAddingVideo)) {

            // Audio and video tracks are collected now
            assert(client->video.has_value() && client->audio.has_value());
            auto video = client->video.value();

            // Send initial NALUs when both tracks are ready
            sendInitialNalus(video);

            client->setState(Client::State::Ready);
        }
        if (client->getState() == Client::State::Ready) {
            // Start streaming now that client is ready
            streaming_active[peer_id] = true;
            if (streaming_threads.find(peer_id) != streaming_threads.end() && streaming_threads[peer_id].joinable()) {
                streaming_threads[peer_id].join();
            }
            streaming_threads[peer_id] = thread(&MQTTWebRTCClient::stream_h264_to_peer, this, peer_id);
            cout << "[WebRTC] Started streaming for client " << peer_id << " in Ready state" << endl;
        }
    }
    
    void create_peer_connection(const string& peer_id, const string& offer_sdp) {
        cout << "[WebRTC] Creating peer connection for: " << peer_id << endl;
        
        // Configure WebRTC exactly like the working example
        Configuration config;
        string stunServer = "stun:stun.l.google.com:19302";
        cout << "STUN server is " << stunServer << endl;
        config.iceServers.emplace_back(stunServer);
        config.disableAutoNegotiation = true;
        
        auto pc = make_shared<PeerConnection>(config);
        auto client = make_shared<Client>(pc);
        clients[peer_id] = client;

        // Follow EXACT pattern from libdatachannel example
        pc->onStateChange([this, peer_id](PeerConnection::State state) {
            cout << "State: " << state << endl;
            if (state == PeerConnection::State::Connected) {
                cout << "CONNECTED! Checking track state..." << endl;
                auto client_it = clients.find(peer_id);
                if (client_it != clients.end() && client_it->second->video.has_value()) {
                    auto track = client_it->second->video.value()->track;
                    cout << "Video track isOpen: " << (track->isOpen() ? "YES" : "NO") << endl;
                    
                    if (track->isOpen()) {
                        cout << "Track already open, forcing stream start!" << endl;
                        streaming_active[peer_id] = true;
                        if (streaming_threads.find(peer_id) != streaming_threads.end() && streaming_threads[peer_id].joinable()) {
                            streaming_threads[peer_id].join();
                        }
                        streaming_threads[peer_id] = thread(&MQTTWebRTCClient::stream_h264_to_peer, this, peer_id);
                    }
                }
            } else if (state == PeerConnection::State::Disconnected ||
                state == PeerConnection::State::Failed ||
                state == PeerConnection::State::Closed) {
                // remove disconnected client
                clients.erase(peer_id);
            }
        });

        pc->onGatheringStateChange([this, wpc = make_weak_ptr(pc), peer_id](PeerConnection::GatheringState state) {
            cout << "Gathering State: " << state << endl;
            if (state == PeerConnection::GatheringState::Complete) {
                if(auto pc = wpc.lock()) {
                    auto description = pc->localDescription();
                    string topic = thing_name + "/robot-control/" + peer_id + "/answer";
                    mqtt_publish(topic, string(description.value()));
                }
            }
        });

        // Handle local candidates - store them for later use
        pc->onLocalCandidate([this, peer_id](Candidate candidate) {
            cout << "Generated local candidate for " << peer_id << endl;
            local_candidates[peer_id].push_back(string(candidate));
        });

        // ONLY ADD VIDEO TRACK - no audio as requested  
        client->video = addVideo(pc, 96, 1, "video-stream", "stream1", [this, peer_id, wc = make_weak_ptr(client)]() {
            cout << "Video from " << peer_id << " opened" << endl;
            
            // Send initial NALUs immediately like libdatachannel example
            if (auto c = wc.lock() && c->video.has_value()) {
                sendInitialNalus(c->video.value());
            }
            
            // Start streaming immediately when track opens
            streaming_active[peer_id] = true;
            if (streaming_threads.find(peer_id) != streaming_threads.end() && streaming_threads[peer_id].joinable()) {
                streaming_threads[peer_id].join();
            }
            streaming_threads[peer_id] = thread(&MQTTWebRTCClient::stream_h264_to_peer, this, peer_id);
        });

        auto dc = pc->createDataChannel("ping-pong");
        dc->onOpen([peer_id]() {
            cout << "DataChannel opened for " << peer_id << endl;
        });
        client->dataChannel = dc;

        // Set remote description from the offer FIRST
        if (!offer_sdp.empty()) {
            Description offer(offer_sdp, "offer");
            pc->setRemoteDescription(offer);
            
            // Check what media types are in the offer and add matching tracks
            bool hasAudio = false, hasVideo = false;
            for (int i = 0; i < offer.mediaCount(); i++) {
                auto media = offer.media(i);
                if (holds_alternative<Description::Media*>(media)) {
                    auto m = get<Description::Media*>(media);
                    if (m) {
                        if (m->type() == "audio") hasAudio = true;
                        if (m->type() == "video") hasVideo = true;
                    }
                }
            }
            
            // Add dummy audio track if browser expects it
            if (hasAudio) {
                auto audioTrack = addAudio(pc, 111, 2, "audio-stream", "stream1", [peer_id]() {
                    cout << "Audio from " << peer_id << " opened (dummy)" << endl;
                });
                client->audio = audioTrack;
            }
        }

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
            
            // Extract SDP from payload
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
                // Create peer connection with the offer
                create_peer_connection(peer_id, offer_sdp);
            }
        }
        // Handle ICE candidates
        else if (topic.find("/candidate/robot") != string::npos) {
            cout << "[MQTT] Processing ICE candidates for peer: " << peer_id << endl;
            
            auto client_it = clients.find(peer_id);
            if (client_it != clients.end()) {
                Json::Value root;
                Json::Reader reader;
                if (reader.parse(payload, root) && root.isArray()) {
                    for (const auto& ice : root) {
                        if (ice.isMember("candidate")) {
                            string candidate_str = ice["candidate"].asString();
                            client_it->second->peerConnection->addRemoteCandidate(Candidate(candidate_str));
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