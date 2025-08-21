#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <memory>
#include <mosquitto.h>

#ifdef JSON_ENABLED
#include <json/json.h>
#endif

#include "webrtc_manager.hpp"

// Global variables for signal handling
static volatile bool keep_running = true;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    keep_running = false;
}

class MQTTClient {
private:
    struct mosquitto *mosq;
    std::string host;
    int port;
    std::string connection_topic;
    std::string robot_control_topic;
    std::string thing_name;
    
#ifdef WEBRTC_ENABLED
    std::unique_ptr<WebRTCManager> webrtc_manager;
#else
    std::unique_ptr<MockWebRTCManager> webrtc_manager;
#endif
    
    static void on_connect_callback(struct mosquitto *mosq, void *userdata, int result) {
        MQTTClient *client = static_cast<MQTTClient*>(userdata);
        client->on_connect(result);
    }
    
    static void on_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message) {
        MQTTClient *client = static_cast<MQTTClient*>(userdata);
        client->on_message(message);
    }
    
    static void on_disconnect_callback(struct mosquitto *mosq, void *userdata, int result) {
        MQTTClient *client = static_cast<MQTTClient*>(userdata);
        client->on_disconnect(result);
    }
    
    static void on_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos) {
        MQTTClient *client = static_cast<MQTTClient*>(userdata);
        client->on_subscribe(mid, qos_count, granted_qos);
    }
    
    // Extract peerId from robot-control topic pattern: <thingname>/robot-control/+/offer
    std::string extract_peer_id(const std::string& topic) {
        // Find "robot-control/" in the topic
        size_t start = topic.find("/robot-control/");
        if (start == std::string::npos) {
            return "";
        }
        
        start += 15; // Length of "/robot-control/"
        
        // Find the next "/" after robot-control/
        size_t end = topic.find("/", start);
        if (end == std::string::npos) {
            return "";
        }
        
        // Extract peerId between robot-control/ and /offer
        return topic.substr(start, end - start);
    }
    
    // Generic method to publish MQTT messages
    void publish_message(const std::string& topic, const std::string& message) {
        std::cout << "📡 Publishing to topic: " << topic << std::endl;
        
        int ret = mosquitto_publish(mosq, nullptr, topic.c_str(), 
                                  message.length(), message.c_str(), 
                                  0, false);
        
        if (ret == MOSQ_ERR_SUCCESS) {
            std::cout << "✅ Message published successfully" << std::endl;
        } else {
            std::cerr << "❌ Failed to publish message. Error: " << ret << " (" << mosquitto_strerror(ret) << ")" << std::endl;
        }
    }
    
    // Publish answer message to <thingname>/<peerId>/answer topic (legacy method)
    void publish_answer(const std::string& peer_id) {
        std::string answer_topic = thing_name + "/" + peer_id + "/answer";
        std::string answer_message = "{\"connected\": true}";
        publish_message(answer_topic, answer_message);
    }
    
    void on_connect(int result) {
        if (result == 0) {
            std::cout << "Connected to MQTT broker at " << host << ":" << port << std::endl;
            
            // Subscribe to connection topic
            std::cout << "Attempting to subscribe to topic: " << connection_topic << std::endl;
            int ret1 = mosquitto_subscribe(mosq, nullptr, connection_topic.c_str(), 0);
            if (ret1 == MOSQ_ERR_SUCCESS) {
                std::cout << "Subscribed to connection topic: " << connection_topic << std::endl;
            } else {
                std::cerr << "Failed to subscribe to connection topic. Error: " << ret1 << " (" << mosquitto_strerror(ret1) << ")" << std::endl;
            }
            
            // Subscribe to robot-control topic
            std::cout << "Attempting to subscribe to topic: " << robot_control_topic << std::endl;
            int ret2 = mosquitto_subscribe(mosq, nullptr, robot_control_topic.c_str(), 0);
            if (ret2 == MOSQ_ERR_SUCCESS) {
                std::cout << "Subscribed to robot-control topic: " << robot_control_topic << std::endl;
            } else {
                std::cerr << "Failed to subscribe to robot-control topic. Error: " << ret2 << " (" << mosquitto_strerror(ret2) << ")" << std::endl;
            }
        } else {
            std::cerr << "Failed to connect to MQTT broker. Return code: " << result << std::endl;
        }
        std::cout.flush();
    }
    
    void on_message(const struct mosquitto_message *message) {
        std::cout.flush();
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::string topic_str = message->topic;
        
        std::cout << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                  << "] Received message on '" << topic_str << "':" << std::endl;
        
        // Check if this is a robot-control offer topic and extract peerId
        if (topic_str.find("/robot-control/") != std::string::npos && topic_str.find("/offer") != std::string::npos) {
            std::string peer_id = extract_peer_id(topic_str);
            if (!peer_id.empty()) {
                std::cout << "🤖 ROBOT-CONTROL OFFER - Extracted peerId: " << peer_id << std::endl;
                
                // Parse the offer payload (supports both JSON and raw SDP)
                if (message->payload && message->payloadlen > 0) {
                    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
                    std::string offer_sdp;
                    
                    try {
                        // Check if payload is JSON or raw SDP
                        if (payload[0] == '{') {
#ifdef JSON_ENABLED
                            // Parse JSON to extract SDP offer
                            Json::Value root;
                            Json::Reader reader;
                            
                            if (reader.parse(payload, root)) {
                                if (root.isMember("sdp")) {
                                    offer_sdp = root["sdp"].asString();
                                    std::cout << "📥 Received JSON SDP offer for peer " << peer_id << std::endl;
                                } else {
                                    std::cout << "⚠️  No SDP found in JSON payload" << std::endl;
                                    publish_answer(peer_id);
                                    return;
                                }
                            } else {
                                std::cout << "⚠️  Invalid JSON in offer payload" << std::endl;
                                publish_answer(peer_id);
                                return;
                            }
#else
                            std::cout << "⚠️  JSON parsing disabled - treating as raw SDP" << std::endl;
                            offer_sdp = payload;
#endif
                        } else {
                            // Treat as raw SDP (like in response.md)
                            offer_sdp = payload;
                            std::cout << "📥 Received raw SDP offer for peer " << peer_id << std::endl;
                        }
                        
                        // Use WebRTC manager to handle the offer
                        if (webrtc_manager && webrtc_manager->handleOffer(peer_id, offer_sdp)) {
                            std::cout << "✅ WebRTC offer handled successfully for " << peer_id << std::endl;
                        } else {
                            std::cout << "⚠️  WebRTC offer handling failed for " << peer_id << std::endl;
                            // Fallback to simple answer
                            publish_answer(peer_id);
                        }
                        
                    } catch (const std::exception& e) {
                        std::cerr << "❌ Error parsing offer: " << e.what() << std::endl;
                        // Fallback to simple answer
                        publish_answer(peer_id);
                    }
                } else {
                    std::cout << "⚠️  Empty offer payload" << std::endl;
                    // Fallback to simple answer
                    publish_answer(peer_id);
                }
            } else {
                std::cout << "⚠️  Could not extract peerId from topic" << std::endl;
            }
        }
        
        if (message->payload && message->payloadlen > 0) {
            std::string payload(static_cast<char*>(message->payload), message->payloadlen);
            std::cout << "Payload: " << payload << std::endl;
        } else {
            std::cout << "No payload or empty payload" << std::endl;
        }
        
        std::cout << std::string(50, '-') << std::endl;
        std::cout.flush();
    }
    
    void on_subscribe(int mid, int qos_count, const int *granted_qos) {
        std::cout << "Subscription confirmed! Message ID: " << mid << ", QoS count: " << qos_count << std::endl;
        for (int i = 0; i < qos_count; i++) {
            std::cout << "Granted QoS[" << i << "]: " << granted_qos[i] << std::endl;
        }
        std::cout.flush();
    }
    
    void on_disconnect(int result) {
        std::cout << "Disconnected from MQTT broker. Return code: " << result << std::endl;
    }
    
public:
    MQTTClient(const std::string& host = "test.rmcs.d6-vnext.com", int port = 1883) 
        : host(host), port(port), 
          connection_topic("vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/connection"),
          robot_control_topic("vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/robot-control/+/offer"),
          thing_name("vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af") {
        mosquitto_lib_init();
        mosq = mosquitto_new("m2m-robot-001", true, this);
        
        if (!mosq) {
            throw std::runtime_error("Failed to create mosquitto client");
        }
        
        mosquitto_connect_callback_set(mosq, on_connect_callback);
        mosquitto_message_callback_set(mosq, on_message_callback);
        mosquitto_disconnect_callback_set(mosq, on_disconnect_callback);
        mosquitto_subscribe_callback_set(mosq, on_subscribe_callback);
        
        // Set authentication
        int auth_ret = mosquitto_username_pw_set(mosq, "vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af", "7#TlDprf");
        if (auth_ret != MOSQ_ERR_SUCCESS) {
            throw std::runtime_error("Failed to set MQTT credentials");
        }
        
        // Initialize WebRTC manager with publish callback
        auto publish_cb = [this](const std::string& topic, const std::string& message) {
            this->publish_message(topic, message);
        };
        
#ifdef WEBRTC_ENABLED
        webrtc_manager = std::make_unique<WebRTCManager>(thing_name, publish_cb);
#else
        webrtc_manager = std::make_unique<MockWebRTCManager>(thing_name, publish_cb);
#endif
    }
    
    ~MQTTClient() {
        if (mosq) {
            mosquitto_destroy(mosq);
        }
        mosquitto_lib_cleanup();
    }
    
    void start() {
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        std::cout << "Connecting to MQTT broker at " << host << ":" << port << "..." << std::endl;
        
        int ret = mosquitto_connect(mosq, host.c_str(), port, 60);
        if (ret != MOSQ_ERR_SUCCESS) {
            std::cerr << "Failed to connect: " << mosquitto_strerror(ret) << std::endl;
            return;
        }
        
        while (keep_running) {
            ret = mosquitto_loop(mosq, 100, 1);
            if (ret != MOSQ_ERR_SUCCESS) {
                std::cerr << "Loop error: " << mosquitto_strerror(ret) << std::endl;
                break;
            }
        }
        
        mosquitto_disconnect(mosq);
        std::cout << "MQTT client stopped." << std::endl;
    }
    
    void stop() {
        mosquitto_disconnect(mosq);
    }
};

int main() {
    try {
        MQTTClient client;
        client.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}