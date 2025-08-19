#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <iomanip>

// ROS includes
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <sensor_msgs/Image.h>
#include <cv_bridge/cv_bridge.h>

// OpenCV includes
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

class BagProcessor {
private:
    std::string bag_path_;
    std::string output_dir_;
    
    struct TopicInfo {
        std::string topic_name;
        std::string msg_type;
        int msg_count;
    };
    
    std::vector<TopicInfo> image_topics_;
    std::map<std::string, std::string> topic_directories_;
    std::map<std::string, int> extraction_counts_;

public:
    BagProcessor(const std::string& bag_path, const std::string& output_dir = "cpp_extracted_images") 
        : bag_path_(bag_path), output_dir_(output_dir) {}

    bool analyzeBag() {
        std::cout << "=== ANALYZING BAG FILE ===" << std::endl;
        std::cout << "Bag file: " << bag_path_ << std::endl;
        std::cout << "==============================" << std::endl;

        try {
            rosbag::Bag bag;
            bag.open(bag_path_, rosbag::bagmode::Read);

            // Get bag info
            rosbag::View view(bag);
            
            // Count total messages and get duration
            int total_messages = 0;
            ros::Time start_time = ros::TIME_MAX;
            ros::Time end_time = ros::TIME_MIN;
            
            std::map<std::string, int> topic_counts;
            std::map<std::string, std::string> topic_types;

            // First pass: collect metadata
            for (const rosbag::MessageInstance& msg : view) {
                total_messages++;
                
                if (msg.getTime() < start_time) start_time = msg.getTime();
                if (msg.getTime() > end_time) end_time = msg.getTime();
                
                std::string topic = msg.getTopic();
                topic_counts[topic]++;
                topic_types[topic] = msg.getDataType();
            }

            double duration = (end_time - start_time).toSec();
            
            std::cout << "Duration: " << std::fixed << std::setprecision(2) << duration << " seconds" << std::endl;
            std::cout << "Message count: " << total_messages << std::endl;
            std::cout << "Topics: " << topic_counts.size() << std::endl << std::endl;

            // Display topic information
            std::cout << "Topics Information:" << std::endl;
            std::cout << "----------------------------------------" << std::endl;
            
            for (const auto& topic_pair : topic_counts) {
                const std::string& topic_name = topic_pair.first;
                int count = topic_pair.second;
                const std::string& msg_type = topic_types[topic_name];
                
                std::cout << "Topic: " << topic_name << std::endl;
                std::cout << "  Type: " << msg_type << std::endl;
                std::cout << "  Count: " << count << std::endl << std::endl;

                // Check if this is an image topic
                if (msg_type.find("Image") != std::string::npos || 
                    topic_name.find("image") != std::string::npos) {
                    
                    TopicInfo info;
                    info.topic_name = topic_name;
                    info.msg_type = msg_type;
                    info.msg_count = count;
                    image_topics_.push_back(info);
                }
            }

            // Display found image topics
            if (!image_topics_.empty()) {
                std::cout << "Found " << image_topics_.size() << " image topics:" << std::endl;
                for (const auto& topic : image_topics_) {
                    std::cout << "  - " << topic.topic_name << ": " << topic.msg_count << " images" << std::endl;
                }
            } else {
                std::cout << "No image topics found!" << std::endl;
                bag.close();
                return false;
            }

            bag.close();
            std::cout << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error analyzing bag file: " << e.what() << std::endl;
            return false;
        }
    }

    bool createOutputDirectories() {
        std::cout << "=== CREATING OUTPUT DIRECTORIES ===" << std::endl;
        
        try {
            // Create main output directory
            std::filesystem::create_directories(output_dir_);
            
            // Create directories for each image topic
            for (const auto& topic : image_topics_) {
                // Clean topic name for directory (replace / with _)
                std::string dir_name = topic.topic_name;
                std::replace(dir_name.begin(), dir_name.end(), '/', '_');
                std::replace(dir_name.begin(), dir_name.end(), ':', '_');
                
                // Remove leading/trailing underscores
                if (!dir_name.empty() && dir_name[0] == '_') {
                    dir_name = dir_name.substr(1);
                }
                
                std::string topic_dir = output_dir_ + "/" + dir_name;
                std::filesystem::create_directories(topic_dir);
                
                topic_directories_[topic.topic_name] = topic_dir;
                extraction_counts_[topic.topic_name] = 0;
                
                std::cout << "Created directory: " << topic_dir << std::endl;
            }
            
            std::cout << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating directories: " << e.what() << std::endl;
            return false;
        }
    }

    bool extractImages() {
        std::cout << "=== EXTRACTING IMAGES ===" << std::endl;
        std::cout << "Extracting ALL images from bag file..." << std::endl;
        
        try {
            rosbag::Bag bag;
            bag.open(bag_path_, rosbag::bagmode::Read);

            // Create view for image topics only
            std::vector<std::string> image_topic_names;
            for (const auto& topic : image_topics_) {
                image_topic_names.push_back(topic.topic_name);
            }
            
            rosbag::View view(bag, rosbag::TopicQuery(image_topic_names));
            
            int processed_messages = 0;
            std::map<std::string, int> success_counts;
            std::map<std::string, int> attempt_counts;
            
            // Initialize counters
            for (const auto& topic : image_topics_) {
                success_counts[topic.topic_name] = 0;
                attempt_counts[topic.topic_name] = 0;
            }

            for (const rosbag::MessageInstance& msg : view) {
                std::string topic_name = msg.getTopic();
                attempt_counts[topic_name]++;
                processed_messages++;

                try {
                    // Convert ROS message to sensor_msgs::Image
                    sensor_msgs::ImageConstPtr image_msg = msg.instantiate<sensor_msgs::Image>();
                    
                    if (image_msg) {
                        // Convert to OpenCV image using cv_bridge
                        cv_bridge::CvImagePtr cv_ptr;
                        
                        try {
                            // Try to convert the image
                            if (image_msg->encoding == "bgr8" || image_msg->encoding == "rgb8") {
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "bgr8");
                            } else if (image_msg->encoding == "mono8") {
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "mono8");
                            } else if (image_msg->encoding == "mono16") {
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "mono16");
                                // Convert 16-bit to 8-bit
                                cv_ptr->image.convertTo(cv_ptr->image, CV_8UC1, 1.0/256.0);
                            } else {
                                // Try default conversion
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "bgr8");
                            }
                        } catch (cv_bridge::Exception& e) {
                            // If conversion fails, try with original encoding
                            cv_ptr = cv_bridge::toCvCopy(image_msg);
                        }

                        if (cv_ptr && !cv_ptr->image.empty()) {
                            // Generate filename with timestamp
                            double timestamp = msg.getTime().toSec();
                            
                            std::ostringstream filename_stream;
                            filename_stream << "image_" 
                                          << std::setfill('0') << std::setw(4) << success_counts[topic_name]
                                          << "_" << std::fixed << std::setprecision(3) << timestamp
                                          << ".jpg";
                            
                            std::string filepath = topic_directories_[topic_name] + "/" + filename_stream.str();
                            
                            // Save image
                            if (cv::imwrite(filepath, cv_ptr->image)) {
                                success_counts[topic_name]++;
                                
                                // Progress update every 50 images
                                if (success_counts[topic_name] % 50 == 0) {
                                    std::cout << "  " << topic_name << ": saved " 
                                             << success_counts[topic_name] << " images" << std::endl;
                                }
                            } else {
                                std::cerr << "Failed to save image: " << filepath << std::endl;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (attempt_counts[topic_name] <= 5) {  // Only show first few errors
                        std::cerr << "Error processing image " << attempt_counts[topic_name] 
                                 << " from " << topic_name << ": " << e.what() << std::endl;
                    }
                }
            }

            bag.close();

            // Print final results
            std::cout << std::endl << "Extraction completed:" << std::endl;
            std::cout << "--------------------------------------------------" << std::endl;
            
            int total_attempted = 0;
            int total_extracted = 0;
            
            for (const auto& topic : image_topics_) {
                int attempted = attempt_counts[topic.topic_name];
                int extracted = success_counts[topic.topic_name];
                double success_rate = attempted > 0 ? (double(extracted) / attempted * 100.0) : 0.0;
                
                total_attempted += attempted;
                total_extracted += extracted;
                
                std::cout << topic.topic_name << ":" << std::endl;
                std::cout << "  Attempted: " << attempted << std::endl;
                std::cout << "  Successful: " << extracted << std::endl;
                std::cout << "  Success rate: " << std::fixed << std::setprecision(1) 
                         << success_rate << "%" << std::endl;
            }
            
            double overall_success = total_attempted > 0 ? (double(total_extracted) / total_attempted * 100.0) : 0.0;
            std::cout << std::endl << "Overall Results:" << std::endl;
            std::cout << "  Total attempted: " << total_attempted << std::endl;
            std::cout << "  Total extracted: " << total_extracted << std::endl;
            std::cout << "  Overall success rate: " << std::fixed << std::setprecision(1) 
                     << overall_success << "%" << std::endl;

            return total_extracted > 0;

        } catch (const std::exception& e) {
            std::cerr << "Error extracting images: " << e.what() << std::endl;
            return false;
        }
    }

    bool process() {
        std::cout << "Starting bag file processing..." << std::endl;
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

        // Step 3: Extract images
        if (!extractImages()) {
            std::cerr << "Failed to extract images" << std::endl;
            return false;
        }

        std::cout << std::endl << "✅ Bag processing completed successfully!" << std::endl;
        std::cout << "Images extracted to: " << output_dir_ << std::endl;
        
        return true;
    }
};

int main(int argc, char** argv) {
    // Initialize ROS (required for rosbag)
    ros::init(argc, argv, "bag_processor");

    std::string bag_file = "../../camera_data_2025-07-08-16-29-06_0.bag";
    std::string output_dir = "cpp_extracted_images";

    // Check if bag file exists
    if (!std::filesystem::exists(bag_file)) {
        std::cerr << "Error: Bag file not found: " << bag_file << std::endl;
        return 1;
    }

    // Create and run bag processor
    BagProcessor processor(bag_file, output_dir);
    
    if (!processor.process()) {
        std::cerr << "Bag processing failed!" << std::endl;
        return 1;
    }

    return 0;
}