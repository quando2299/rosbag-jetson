#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <mosquitto.h>

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
    std::string topic;
    
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
    
    void on_connect(int result) {
        if (result == 0) {
            std::cout << "Connected to MQTT broker at " << host << ":" << port << std::endl;
            std::cout << "Attempting to subscribe to topic: " << topic << std::endl;
            int ret = mosquitto_subscribe(mosq, nullptr, topic.c_str(), 0);
            if (ret == MOSQ_ERR_SUCCESS) {
                std::cout << "Subscribed to topic: " << topic << std::endl;
            } else {
                std::cerr << "Failed to subscribe to topic. Error: " << ret << " (" << mosquitto_strerror(ret) << ")" << std::endl;
            }
        } else {
            std::cerr << "Failed to connect to MQTT broker. Return code: " << result << std::endl;
        }
        std::cout.flush();
    }
    
    void on_message(const struct mosquitto_message *message) {
        std::cout.flush();
        std::cerr << "DEBUG: Message received callback triggered!" << std::endl;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::cout << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                  << "] Received message on '" << message->topic << "':" << std::endl;
        
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
    MQTTClient(const std::string& host = "rmcs.d6-vnext.com", int port = 1883) 
        : host(host), port(port), topic("vnext-test_b6239876-943a-4d6f-a7ef-f1440d5c58af/connection") {
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
        int auth_ret = mosquitto_username_pw_set(mosq, "0f5c126f-e339-4f99-a8a0-4b4fe29944f0", "!1l!YC1#");
        if (auth_ret != MOSQ_ERR_SUCCESS) {
            throw std::runtime_error("Failed to set MQTT credentials");
        }
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