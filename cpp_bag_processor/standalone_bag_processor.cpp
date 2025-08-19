#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdint>

// OpenCV includes (if available)
#ifdef HAVE_OPENCV
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#endif

struct BagHeader {
    std::string version;
    uint64_t conn_count;
    uint64_t chunk_count;
    uint64_t index_pos;
    uint32_t chunk_threshold;
    uint64_t creation_date;
};

struct TopicInfo {
    std::string topic_name;
    std::string msg_type;
    int msg_count;
};

class StandaloneBagProcessor {
private:
    std::string bag_path_;
    std::string output_dir_;
    std::vector<TopicInfo> image_topics_;

public:
    StandaloneBagProcessor(const std::string& bag_path, const std::string& output_dir = "cpp_extracted_images") 
        : bag_path_(bag_path), output_dir_(output_dir) {}

    bool analyzeBag() {
        std::cout << "=== ANALYZING BAG FILE (Standalone Mode) ===" << std::endl;
        std::cout << "Bag file: " << bag_path_ << std::endl;
        std::cout << "Note: This is a basic analysis without full ROS support" << std::endl;
        std::cout << "=============================================" << std::endl;

        std::ifstream bag_file(bag_path_, std::ios::binary);
        if (!bag_file.is_open()) {
            std::cerr << "Error: Cannot open bag file: " << bag_path_ << std::endl;
            return false;
        }

        // Read basic file info
        bag_file.seekg(0, std::ios::end);
        size_t file_size = bag_file.tellg();
        bag_file.seekg(0, std::ios::beg);

        std::cout << "File size: " << file_size << " bytes (" 
                  << std::fixed << std::setprecision(2) << (file_size / 1024.0 / 1024.0) 
                  << " MB)" << std::endl;

        // Try to read bag header
        std::string line;
        if (std::getline(bag_file, line)) {
            if (line.find("#ROSBAG") != std::string::npos) {
                std::cout << "Valid ROS bag file detected" << std::endl;
                std::cout << "Header: " << line << std::endl;
            } else {
                std::cout << "Warning: May not be a valid ROS bag file" << std::endl;
            }
        }

        bag_file.close();

        // Since we can't parse the full bag without ROS libraries,
        // we'll create some mock image topics based on what we know
        std::cout << "\nBased on previous analysis, expecting these image topics:" << std::endl;
        
        TopicInfo topics[] = {
            {"/flir/id8/image_resized", "sensor_msgs/Image", 438},
            {"/leopard/id1/image_resized", "sensor_msgs/Image", 438},
            {"/leopard/id3/image_resized", "sensor_msgs/Image", 439},
            {"/leopard/id4/image_resized", "sensor_msgs/Image", 438},
            {"/leopard/id5/image_resized", "sensor_msgs/Image", 438},
            {"/leopard/id6/image_resized", "sensor_msgs/Image", 439},
            {"/leopard/id7/image_resized", "sensor_msgs/Image", 439}
        };

        for (int i = 0; i < 7; i++) {
            image_topics_.push_back(topics[i]);
            std::cout << "  - " << topics[i].topic_name << ": " << topics[i].msg_count << " images" << std::endl;
        }

        std::cout << "\nTotal expected images: 3069" << std::endl;
        std::cout << "Estimated duration: ~43.83 seconds" << std::endl;

        return true;
    }

    bool createOutputDirectories() {
        std::cout << "\n=== CREATING OUTPUT DIRECTORIES ===" << std::endl;
        
        try {
            // Create main output directory
            std::filesystem::create_directories(output_dir_);
            
            // Create directories for each image topic
            for (const auto& topic : image_topics_) {
                // Clean topic name for directory
                std::string dir_name = topic.topic_name;
                std::replace(dir_name.begin(), dir_name.end(), '/', '_');
                std::replace(dir_name.begin(), dir_name.end(), ':', '_');
                
                // Remove leading underscore
                if (!dir_name.empty() && dir_name[0] == '_') {
                    dir_name = dir_name.substr(1);
                }
                
                std::string topic_dir = output_dir_ + "/" + dir_name;
                std::filesystem::create_directories(topic_dir);
                
                std::cout << "Created directory: " << topic_dir << std::endl;
            }
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating directories: " << e.what() << std::endl;
            return false;
        }
    }

    bool showInstructions() {
        std::cout << "\n=== NEXT STEPS ===" << std::endl;
        std::cout << "This standalone version shows the structure but cannot extract images" << std::endl;
        std::cout << "without ROS libraries." << std::endl << std::endl;
        
        std::cout << "To extract images, you have two options:" << std::endl;
        std::cout << "1. Use the Python version (already working):" << std::endl;
        std::cout << "   cd ../bag_analyzer" << std::endl;
        std::cout << "   python extract_all_images.py" << std::endl << std::endl;
        
        std::cout << "2. On Jetson with ROS installed:" << std::endl;
        std::cout << "   # Install dependencies" << std::endl;
        std::cout << "   sudo apt update" << std::endl;
        std::cout << "   sudo apt install ros-melodic-rosbag ros-melodic-sensor-msgs ros-melodic-cv-bridge" << std::endl;
        std::cout << "   sudo apt install libopencv-dev" << std::endl << std::endl;
        
        std::cout << "   # Build and run" << std::endl;
        std::cout << "   mkdir build && cd build" << std::endl;
        std::cout << "   cmake .." << std::endl;
        std::cout << "   make" << std::endl;
        std::cout << "   ./bag_processor" << std::endl << std::endl;

        std::cout << "The C++ version will be much faster on Jetson!" << std::endl;
        return true;
    }

    bool process() {
        std::cout << "Starting standalone bag file analysis..." << std::endl;
        std::cout << "Bag file: " << bag_path_ << std::endl;
        std::cout << "Output directory: " << output_dir_ << std::endl << std::endl;

        // Step 1: Analyze bag file
        if (!analyzeBag()) {
            std::cerr << "Failed to analyze bag file" << std::endl;
            return false;
        }

        // Step 2: Create output directories
        if (!createOutputDirectories()) {
            std::cerr << "Failed to create output directories" << std::endl;
            return false;
        }

        // Step 3: Show instructions
        showInstructions();

        return true;
    }
};

int main(int argc, char** argv) {
    std::string bag_file = "../../camera_data_2025-07-08-16-29-06_0.bag";
    std::string output_dir = "cpp_extracted_images";

    // Check if bag file exists
    if (!std::filesystem::exists(bag_file)) {
        std::cerr << "Error: Bag file not found: " << bag_file << std::endl;
        std::cerr << "Current directory: " << std::filesystem::current_path() << std::endl;
        return 1;
    }

    // Create and run standalone bag processor
    StandaloneBagProcessor processor(bag_file, output_dir);
    
    if (!processor.process()) {
        std::cerr << "Bag processing failed!" << std::endl;
        return 1;
    }

    return 0;
}