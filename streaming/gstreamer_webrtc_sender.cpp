#define GST_USE_UNSTABLE_API
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
#include <chrono>
#include <signal.h>

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
        
        // Use the same default broker as your existing code
        mqtt_broker = broker ? broker : "test.rmcs.d6-vnext.com";
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
        // Check if this is ICE candidate from robot (Flutter app)
        else if (topic.find("/candidate/robot") != std::string::npos) {
            std::string peer_id = extractPeerId(topic);
            if (!peer_id.empty() && peer_id == current_peer_id) {
                std::cout << "Received remote ICE candidates from Flutter for peer: " << peer_id << std::endl;
                
                try {
                    json candidates = json::parse(payload);
                    if (candidates.is_array()) {
                        handleRemoteIceCandidates(peer_id, candidates);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing remote ICE candidates: " << e.what() << std::endl;
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
        
        // Check if we're already handling this peer with an active pipeline
        if (pipeline && current_peer_id == peer_id) {
            std::cout << "Already handling peer: " << peer_id << " - ignoring duplicate offer" << std::endl;
            return;
        }
        
        // If handling a different peer, stop the old pipeline
        if (pipeline) {
            std::cout << "Stopping old pipeline for peer: " << current_peer_id << std::endl;
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            pipeline = nullptr;
            webrtcbin = nullptr;
            local_candidates.clear();
        }
        
        current_peer_id = peer_id;
        std::cout << "Creating new pipeline for peer: " << peer_id << std::endl;
        
        // Create simplified GStreamer pipeline - let WebRTC handle codec negotiation automatically
        std::string pipeline_str = 
            "webrtcbin name=sendonly bundle-policy=max-bundle stun-server=stun://stun.l.google.com:19302 "
            "filesrc location=" + video_file + " ! "
            "qtdemux name=demux "
            "demux.video_0 ! queue max-size-buffers=20 ! h264parse config-interval=1 ! "
            "rtph264pay config-interval=1 name=pay0 ! sendonly. "
            "audiotestsrc is-live=true wave=silence ! "
            "audioconvert ! audioresample ! "
            "opusenc bitrate=64000 ! rtpopuspay name=pay1 ! sendonly.";
        
        GError *error = nullptr;
        pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
        
        if (error) {
            std::cerr << "❌ Failed to create pipeline: " << error->message << std::endl;
            g_error_free(error);
            return;
        }
        
        // Get webrtcbin element
        webrtcbin = gst_bin_get_by_name(GST_BIN(pipeline), "sendonly");
        if (!webrtcbin) {
            std::cerr << "❌ Failed to get webrtcbin element" << std::endl;
            gst_object_unref(pipeline);
            pipeline = nullptr;
            return;
        }
        
        // Set up WebRTC callbacks BEFORE starting pipeline
        g_signal_connect(webrtcbin, "on-ice-candidate",
            G_CALLBACK(onIceCandidate), this);
        
        g_signal_connect(webrtcbin, "notify::ice-gathering-state",
            G_CALLBACK(onIceGatheringStateNotify), this);
        
        g_signal_connect(webrtcbin, "notify::connection-state",
            G_CALLBACK(onConnectionStateNotify), this);
        
        std::cout << "🔧 WebRTC callbacks configured - letting WebRTC handle codec negotiation" << std::endl;
        
        // Start pipeline in PAUSED state first
        GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "❌ Failed to set pipeline to PAUSED" << std::endl;
            gst_object_unref(pipeline);
            pipeline = nullptr;
            webrtcbin = nullptr;
            return;
        }
        
        // Wait for pipeline to reach PAUSED state
        GstState state;
        ret = gst_element_get_state(pipeline, &state, nullptr, 5 * GST_SECOND);
        if (ret == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PAUSED) {
            std::cerr << "❌ Pipeline failed to reach PAUSED state" << std::endl;
            gst_object_unref(pipeline);
            pipeline = nullptr;
            webrtcbin = nullptr;
            return;
        }
        
        std::cout << "✅ Pipeline created and paused for peer: " << peer_id << std::endl;
        
        // Set pipeline to PLAYING first so transceivers can be created automatically
        GstStateChangeReturn ret_play = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (ret_play == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "❌ Failed to set pipeline to PLAYING" << std::endl;
            return;
        }
        
        std::cout << "▶️  Pipeline set to PLAYING state" << std::endl;
        
        // Wait a moment for pipeline to stabilize
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Now set remote description (offer)
        setRemoteDescription(sdp_offer);
    }
    
    void setRemoteDescription(const std::string& sdp_offer) {
        if (!webrtcbin) {
            std::cerr << "❌ No webrtcbin available for remote description" << std::endl;
            return;
        }
        
        GstSDPMessage *sdp_msg;
        GstWebRTCSessionDescription *offer;
        
        // Parse SDP with better error handling
        if (gst_sdp_message_new(&sdp_msg) != GST_SDP_OK) {
            std::cerr << "❌ Failed to create SDP message" << std::endl;
            return;
        }
        
        if (gst_sdp_message_parse_buffer((guint8*)sdp_offer.c_str(), sdp_offer.length(), sdp_msg) != GST_SDP_OK) {
            std::cerr << "❌ Failed to parse SDP buffer" << std::endl;
            gst_sdp_message_free(sdp_msg);
            return;
        }
        
        offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_msg);
        if (!offer) {
            std::cerr << "❌ Failed to create WebRTC session description" << std::endl;
            return;
        }
        
        std::cout << "📥 Setting remote description..." << std::endl;
        
        GstPromise *promise = gst_promise_new_with_change_func(
            [](GstPromise *promise, gpointer user_data) {
                GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
                const GstStructure *reply = gst_promise_get_reply(promise);
                
                if (reply && gst_structure_has_field(reply, "error")) {
                    const GValue *error_val = gst_structure_get_value(reply, "error");
                    GError *error = (GError*)g_value_get_boxed(error_val);
                    std::cerr << "❌ Failed to set remote description: " << error->message << std::endl;
                } else {
                    std::cout << "✅ Remote description set successfully" << std::endl;
                    
                    // Create answer
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    self->createAnswer();
                }
                gst_promise_unref(promise);
            }, this, nullptr);
        
        g_signal_emit_by_name(webrtcbin, "set-remote-description", offer, promise);
        gst_webrtc_session_description_free(offer);
    }
    

    void createAnswer() {
        if (!webrtcbin) {
            std::cerr << "❌ No webrtcbin available for creating answer" << std::endl;
            return;
        }
        
        std::cout << "📝 Creating WebRTC answer..." << std::endl;
        
        GstPromise *promise = gst_promise_new_with_change_func(
            [](GstPromise *promise, gpointer user_data) {
                GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
                self->onAnswerCreated(promise);
            }, this, nullptr);
        
        g_signal_emit_by_name(webrtcbin, "create-answer", nullptr, promise);
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
    
    void onAnswerCreated(GstPromise *promise) {
        const GstStructure *reply = gst_promise_get_reply(promise);
        
        if (reply && gst_structure_has_field(reply, "error")) {
            const GValue *error_val = gst_structure_get_value(reply, "error");
            GError *error = (GError*)g_value_get_boxed(error_val);
            std::cerr << "❌ Failed to create answer: " << error->message << std::endl;
            gst_promise_unref(promise);
            return;
        }
        
        GstWebRTCSessionDescription *answer = nullptr;
        if (!gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr) || !answer) {
            std::cerr << "❌ No answer in reply structure" << std::endl;
            gst_promise_unref(promise);
            return;
        }
        
        std::cout << "✅ Answer created successfully" << std::endl;
        
        // Set local description
        GstPromise *local_promise = gst_promise_new_with_change_func(
            [](GstPromise *promise, gpointer user_data) {
                const GstStructure *reply = gst_promise_get_reply(promise);
                if (reply && gst_structure_has_field(reply, "error")) {
                    const GValue *error_val = gst_structure_get_value(reply, "error");
                    GError *error = (GError*)g_value_get_boxed(error_val);
                    std::cerr << "❌ Failed to set local description: " << error->message << std::endl;
                } else {
                    std::cout << "✅ Local description set successfully" << std::endl;
                }
                gst_promise_unref(promise);
            }, this, nullptr);
        
        g_signal_emit_by_name(webrtcbin, "set-local-description", answer, local_promise);
        
        // Send answer via MQTT to <thingname>/robot-control/<peerId>/answer
        gchar *sdp_string = gst_sdp_message_as_text(answer->sdp);
        
        if (sdp_string) {
            // Send raw SDP (like your existing code)
            std::string answer_topic = getAnswerTopic(current_peer_id);
            int ret = mosquitto_publish(mqtt_client, nullptr, answer_topic.c_str(), 
                             strlen(sdp_string), sdp_string, 0, false);
            
            if (ret == MOSQ_ERR_SUCCESS) {
                std::cout << "📤 Answer sent to topic: " << answer_topic << std::endl;
            } else {
                std::cerr << "❌ Failed to publish answer: " << mosquitto_strerror(ret) << std::endl;
            }
            
            g_free(sdp_string);
        } else {
            std::cerr << "❌ Failed to convert SDP to string" << std::endl;
        }
        
        gst_webrtc_session_description_free(answer);
        gst_promise_unref(promise);
    }
    
    static void onIceCandidate(GstElement *element, guint mlineindex, gchar *candidate, gpointer user_data) {
        GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
        
        // Store candidates in a list to send them all at once when gathering is complete
        self->local_candidates.push_back({candidate, mlineindex});
        
        std::cout << "ICE candidate collected: " << candidate << std::endl;
    }
    
    void handleRemoteIceCandidates(const std::string& peer_id, const json& candidates) {
        if (!candidates.is_array()) {
            std::cerr << "Remote ICE candidates not an array" << std::endl;
            return;
        }
        
        std::lock_guard<std::mutex> lock(webrtc_mutex);
        if (!webrtcbin) {
            std::cout << "No webrtcbin available for remote ICE candidates" << std::endl;
            return;
        }
        
        std::cout << "Processing " << candidates.size() << " remote ICE candidates from Flutter" << std::endl;
        
        for (const auto& candidate : candidates) {
            if (candidate.contains("candidate") && candidate.contains("sdpMLineIndex")) {
                std::string cand_str = candidate["candidate"];
                guint mlineindex = candidate["sdpMLineIndex"];
                
                g_signal_emit_by_name(webrtcbin, "add-ice-candidate", mlineindex, cand_str.c_str());
                std::cout << "✅ Added remote ICE candidate from Flutter" << std::endl;
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
        
        std::cout << "🧊 ICE gathering state: " << state_name << std::endl;
    }
    
    static void onConnectionStateNotify(GstElement *element, GParamSpec *pspec, gpointer user_data) {
        GStreamerWebRTCSender *self = static_cast<GStreamerWebRTCSender*>(user_data);
        GstWebRTCPeerConnectionState conn_state;
        g_object_get(element, "connection-state", &conn_state, nullptr);
        
        const char *state_name = "unknown";
        switch (conn_state) {
            case GST_WEBRTC_PEER_CONNECTION_STATE_NEW:
                state_name = "new";
                break;
            case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING:
                state_name = "connecting";
                break;
            case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED:
                state_name = "connected";
                std::cout << "🎉 WebRTC connection established!" << std::endl;
                break;
            case GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED:
                state_name = "disconnected";
                break;
            case GST_WEBRTC_PEER_CONNECTION_STATE_FAILED:
                state_name = "failed";
                std::cout << "❌ WebRTC connection failed!" << std::endl;
                break;
            case GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED:
                state_name = "closed";
                break;
        }
        
        std::cout << "🔗 WebRTC connection state: " << state_name << std::endl;
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