#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <gst/sdp/sdp.h>
#include <mosquitto.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

using json = nlohmann::json;

class GStreamerWebRTCSender {
private:
    GstElement *pipeline;
    GstElement *webrtcbin;
    struct mosquitto *mqtt_client;
    std::string mqtt_broker;
    int mqtt_port;
    std::string video_file;
    std::string stun_server;
    std::atomic<bool> running;
    std::mutex webrtc_mutex;
    std::string current_peer_id;
    std::vector<std::pair<std::string, guint>> local_candidates;
    
    // Thing name (static for now, can be made configurable)
    const std::string THING_NAME = "vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af";
    
    // MQTT topics (matching your existing setup)
    std::string getOfferTopic() const {
        return THING_NAME + "/robot-control/+/offer";
    }
    
    std::string getAnswerTopic(const std::string& peer_id) const {
        return THING_NAME + "/robot-control/" + peer_id + "/answer";
    }
    
    std::string getCandidateRobotTopic() const {
        return THING_NAME + "/robot-control/+/candidate/robot";
    }
    
    std::string getCandidateRmcsTopic(const std::string& peer_id) const {
        return THING_NAME + "/robot-control/" + peer_id + "/candidate/rmcs";
    }

public:
    GStreamerWebRTCSender() : pipeline(nullptr), webrtcbin(nullptr), mqtt_client(nullptr), running(true) {
        // Get configuration from environment
        const char* broker = getenv("MQTT_BROKER");
        const char* port = getenv("MQTT_PORT");
        const char* video = getenv("VIDEO_FILE");
        const char* stun = getenv("STUN_SERVER");
        
        mqtt_broker = broker ? broker : "localhost";
        mqtt_port = port ? std::stoi(port) : 1883;
        video_file = video ? video : "/app/videos/flir_id8_image_resized_30fps.mp4";
        stun_server = stun ? stun : "stun://stun.l.google.com:19302";
    }
    
    ~GStreamerWebRTCSender() {
        cleanup();
    }
    
    // Extract peerId from topic pattern: <thingname>/robot-control/<peerId>/offer
    std::string extractPeerId(const std::string& topic) {
        size_t start = topic.find("/robot-control/");
        if (start == std::string::npos) return "";
        
        start += 15; // Length of "/robot-control/"
        size_t end = topic.find("/", start);
        if (end == std::string::npos) return "";
        
        return topic.substr(start, end - start);
    }
    
    bool initialize() {
        // Initialize GStreamer
        gst_init(nullptr, nullptr);
        
        // Initialize MQTT
        mosquitto_lib_init();
        mqtt_client = mosquitto_new("m2m-robot-gstreamer", true, this);
        
        if (!mqtt_client) {
            std::cerr << "Failed to create MQTT client" << std::endl;
            return false;
        }
        
        // Set MQTT callbacks
        mosquitto_message_callback_set(mqtt_client, 
            [](struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
                static_cast<GStreamerWebRTCSender*>(obj)->onMqttMessage(msg);
            });
        
        mosquitto_connect_callback_set(mqtt_client,
            [](struct mosquitto *mosq, void *obj, int rc) {
                if (rc == 0) {
                    std::cout << "Connected to MQTT broker" << std::endl;
                    static_cast<GStreamerWebRTCSender*>(obj)->subscribeTopics();
                }
            });
        
        // Set authentication (using same credentials as your existing code)
        mosquitto_username_pw_set(mqtt_client, THING_NAME.c_str(), "7#TlDprf");
        
        // Connect to MQTT broker
        if (mosquitto_connect(mqtt_client, mqtt_broker.c_str(), mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
            std::cerr << "Failed to connect to MQTT broker" << std::endl;
            return false;
        }
        
        // Start MQTT loop
        mosquitto_loop_start(mqtt_client);
        
        return true;
    }
    
    void subscribeTopics() {
        std::string offer_topic = getOfferTopic();
        std::string candidate_topic = getCandidateRobotTopic();
        
        mosquitto_subscribe(mqtt_client, nullptr, offer_topic.c_str(), 0);
        mosquitto_subscribe(mqtt_client, nullptr, candidate_topic.c_str(), 0);
        
        std::cout << "Subscribed to topics:" << std::endl;
        std::cout << "  - " << offer_topic << std::endl;
        std::cout << "  - " << candidate_topic << std::endl;
    }
    
    void onMqttMessage(const struct mosquitto_message *msg) {
        std::string topic(msg->topic);
        std::string payload((char*)msg->payload, msg->payloadlen);
        
        // Check if this is an offer topic
        if (topic.find("/robot-control/") != std::string::npos && topic.find("/offer") != std::string::npos) {
            std::string peer_id = extractPeerId(topic);
            if (!peer_id.empty()) {
                current_peer_id = peer_id;
                std::cout << "Received offer from peer: " << peer_id << std::endl;
                
                // Handle the offer (can be JSON or raw SDP)
                handleOffer(peer_id, payload);
            }
        }
        // Check if this is ICE candidate from robot
        else if (topic.find("/candidate/robot") != std::string::npos) {
            std::string peer_id = extractPeerId(topic);
            if (!peer_id.empty()) {
                std::cout << "Received ICE candidates for peer: " << peer_id << std::endl;
                
                try {
                    json candidates = json::parse(payload);
                    if (candidates.is_array()) {
                        handleIceCandidates(peer_id, candidates);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing ICE candidates: " << e.what() << std::endl;
                }
            }
        }
    }
    
    void handleOffer(const std::string& peer_id, const std::string& offer_payload) {
        try {
            std::string sdp_offer;
            
            // Check if payload is JSON or raw SDP
            if (offer_payload[0] == '{') {
                json data = json::parse(offer_payload);
                if (data.contains("sdp")) {
                    sdp_offer = data["sdp"];
                } else {
                    std::cerr << "No SDP in offer" << std::endl;
                    return;
                }
            } else {
                // Raw SDP
                sdp_offer = offer_payload;
            }
            
            // Start pipeline and create answer
            startPipeline(peer_id, sdp_offer);
            
        } catch (const std::exception& e) {
            std::cerr << "Error handling offer: " << e.what() << std::endl;
        }
    }
    
    void startPipeline(const std::string& peer_id, const std::string& sdp_offer) {
        std::lock_guard<std::mutex> lock(webrtc_mutex);
        
        current_peer_id = peer_id;
        
        if (pipeline) {
            std::cout << "Pipeline already running" << std::endl;
            return;
        }
        
        // Create GStreamer pipeline for H.264 video file streaming
        std::string pipeline_str = 
            "filesrc location=" + video_file + " ! "
            "qtdemux ! h264parse ! "
            "rtph264pay config-interval=-1 pt=96 ! "
            "application/x-rtp,media=video,encoding-name=H264,payload=96 ! "
            "webrtcbin name=sendonly bundle-policy=max-bundle stun-server=" + stun_server;
        
        GError *error = nullptr;
        pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
        
        if (error) {
            std::cerr << "Failed to create pipeline: " << error->message << std::endl;
            g_error_free(error);
            return;
        }
        
        // Get webrtcbin element
        webrtcbin = gst_bin_get_by_name(GST_BIN(pipeline), "sendonly");
        
        // Connect to signals
        g_signal_connect(webrtcbin, "on-negotiation-needed",
            G_CALLBACK(onNegotiationNeeded), this);
        
        g_signal_connect(webrtcbin, "on-ice-candidate",
            G_CALLBACK(onIceCandidate), this);
        
        g_signal_connect(webrtcbin, "notify::ice-gathering-state",
            G_CALLBACK(onIceGatheringStateNotify), this);
        
        // Set remote description (offer)
        GstSDPMessage *sdp_msg;
        GstWebRTCSessionDescription *offer;
        
        gst_sdp_message_new(&sdp_msg);
        gst_sdp_message_parse_buffer((guint8*)sdp_offer.c_str(), sdp_offer.length(), sdp_msg);
        offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_msg);
        
        GstPromise *promise = gst_promise_new();
        g_signal_emit_by_name(webrtcbin, "set-remote-description", offer, promise);
        gst_promise_interrupt(promise);
        gst_promise_unref(promise);
        
        // Start pipeline
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        std::cout << "Pipeline started for peer: " << peer_id << std::endl;
    }
    
    void stopPipeline() {
        std::lock_guard<std::mutex> lock(webrtc_mutex);
        
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            pipeline = nullptr;
            webrtcbin = nullptr;
            std::cout << "Pipeline stopped" << std::endl;
        }
    }
    
    static void onNegotiationNeeded(GstElement *element, gpointer user_data) {
        GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
        GstPromise *promise;
        
        std::cout << "Creating answer..." << std::endl;
        
        promise = gst_promise_new_with_change_func(
            [](GstPromise *promise, gpointer user_data) {
                GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
                self->onAnswerCreated(promise);
            }, user_data, nullptr);
        
        g_signal_emit_by_name(element, "create-answer", nullptr, promise);
    }
    
    void onAnswerCreated(GstPromise *promise) {
        GstWebRTCSessionDescription *answer = nullptr;
        const GstStructure *reply;
        
        reply = gst_promise_get_reply(promise);
        gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr);
        gst_promise_unref(promise);
        
        // Set local description
        GstPromise *local_promise = gst_promise_new();
        g_signal_emit_by_name(webrtcbin, "set-local-description", answer, local_promise);
        gst_promise_interrupt(local_promise);
        gst_promise_unref(local_promise);
        
        // Send answer via MQTT to <thingname>/robot-control/<peerId>/answer
        gchar *sdp_string = gst_sdp_message_as_text(answer->sdp);
        
        // Send raw SDP (like your existing code)
        std::string answer_topic = getAnswerTopic(current_peer_id);
        mosquitto_publish(mqtt_client, nullptr, answer_topic.c_str(), 
                         strlen(sdp_string), sdp_string, 0, false);
        
        std::cout << "Answer sent to topic: " << answer_topic << std::endl;
        
        g_free(sdp_string);
        gst_webrtc_session_description_free(answer);
    }
    
    static void onIceCandidate(GstElement *element, guint mlineindex, gchar *candidate, gpointer user_data) {
        GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
        
        // Store candidates in a list to send them all at once when gathering is complete
        self->local_candidates.push_back({candidate, mlineindex});
        
        std::cout << "ICE candidate collected: " << candidate << std::endl;
    }
    
    void handleIceCandidates(const std::string& peer_id, const json& candidates) {
        if (!candidates.is_array()) {
            std::cerr << "ICE candidates not an array" << std::endl;
            return;
        }
        
        std::lock_guard<std::mutex> lock(webrtc_mutex);
        if (!webrtcbin) return;
        
        for (const auto& candidate : candidates) {
            if (candidate.contains("candidate") && candidate.contains("sdpMLineIndex")) {
                std::string cand_str = candidate["candidate"];
                guint mlineindex = candidate["sdpMLineIndex"];
                
                g_signal_emit_by_name(webrtcbin, "add-ice-candidate", mlineindex, cand_str.c_str());
                std::cout << "Added remote ICE candidate" << std::endl;
            }
        }
    }
    
    static void onIceGatheringStateNotify(GstElement *element, GParamSpec *pspec, gpointer user_data) {
        GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
        GstWebRTCICEGatheringState ice_state;
        g_object_get(element, "ice-gathering-state", &ice_state, nullptr);
        
        const char *state_name = "unknown";
        switch (ice_state) {
            case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
                state_name = "new";
                break;
            case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
                state_name = "gathering";
                break;
            case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
                state_name = "complete";
                // Send all collected ICE candidates when gathering is complete
                self->sendCollectedIceCandidates();
                break;
        }
        
        std::cout << "ICE gathering state: " << state_name << std::endl;
    }
    
    void sendCollectedIceCandidates() {
        if (local_candidates.empty()) return;
        
        json candidates_array = json::array();
        for (const auto& [candidate, mlineindex] : local_candidates) {
            json cand_obj;
            cand_obj["candidate"] = candidate;
            cand_obj["sdpMLineIndex"] = mlineindex;
            cand_obj["sdpMid"] = "0";
            candidates_array.push_back(cand_obj);
        }
        
        // Publish to <thingname>/robot-control/<peerId>/candidate/rmcs
        std::string rmcs_topic = getCandidateRmcsTopic(current_peer_id);
        std::string msg_str = candidates_array.dump();
        
        mosquitto_publish(mqtt_client, nullptr, rmcs_topic.c_str(),
                         msg_str.length(), msg_str.c_str(), 0, false);
        
        std::cout << "Published " << local_candidates.size() << " ICE candidates to: " << rmcs_topic << std::endl;
        
        // Clear candidates after sending
        local_candidates.clear();
    }
    
    void cleanup() {
        running = false;
        
        stopPipeline();
        
        if (mqtt_client) {
            mosquitto_loop_stop(mqtt_client, true);
            mosquitto_disconnect(mqtt_client);
            mosquitto_destroy(mqtt_client);
        }
        
        mosquitto_lib_cleanup();
    }
    
    void run() {
        std::cout << "GStreamer WebRTC Sender started" << std::endl;
        std::cout << "Thing name: " << THING_NAME << std::endl;
        std::cout << "Video file: " << video_file << std::endl;
        std::cout << "MQTT broker: " << mqtt_broker << ":" << mqtt_port << std::endl;
        std::cout << "STUN server: " << stun_server << std::endl;
        std::cout << "Waiting for WebRTC offers..." << std::endl;
        
        // Keep running
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

int main(int argc, char *argv[]) {
    GStreamerWebRTCSender sender;
    
    if (!sender.initialize()) {
        std::cerr << "Failed to initialize" << std::endl;
        return 1;
    }
    
    // Handle signals
    signal(SIGINT, [](int) {
        std::cout << "\nShutting down..." << std::endl;
        exit(0);
    });
    
    sender.run();
    
    return 0;
}