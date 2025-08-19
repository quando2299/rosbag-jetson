#!/usr/bin/env python3
"""
Extract ALL images from ROS bag file using proper ROS message deserialization
"""

import os
import sys
import cv2
import numpy as np
from rosbags.rosbag1 import Reader as ROS1Reader
from rosbags.serde import deserialize_cdr
import struct

def create_output_directories(base_dir, topics):
    """Create output directories for each camera topic"""
    os.makedirs(base_dir, exist_ok=True)
    
    topic_dirs = {}
    for topic in topics:
        # Clean topic name for directory
        dir_name = topic.replace('/', '_').replace(':', '').strip('_')
        topic_dir = os.path.join(base_dir, dir_name)
        os.makedirs(topic_dir, exist_ok=True)
        topic_dirs[topic] = topic_dir
        print(f"Created directory: {topic_dir}")
    
    return topic_dirs

def parse_ros1_image_message(raw_data):
    """
    Parse ROS1 sensor_msgs/Image message from raw bytes
    ROS1 message format:
    - header (seq:4, timestamp:8, frame_id:variable)
    - height:4, width:4, encoding:variable, is_bigendian:1, step:4, data:variable
    """
    try:
        offset = 0
        
        # Skip header - find frame_id end
        # Look for null terminator after frame_id string
        frame_id_start = 12  # After seq(4) + timestamp(8)
        frame_id_len_bytes = raw_data[frame_id_start:frame_id_start+4]
        frame_id_len = struct.unpack('<I', frame_id_len_bytes)[0]
        offset = frame_id_start + 4 + frame_id_len
        
        # Read height and width
        height = struct.unpack('<I', raw_data[offset:offset+4])[0]
        offset += 4
        width = struct.unpack('<I', raw_data[offset:offset+4])[0]
        offset += 4
        
        # Read encoding string
        encoding_len = struct.unpack('<I', raw_data[offset:offset+4])[0]
        offset += 4
        encoding = raw_data[offset:offset+encoding_len].decode('utf-8').rstrip('\x00')
        offset += encoding_len
        
        # Read is_bigendian and step
        is_bigendian = struct.unpack('<B', raw_data[offset:offset+1])[0]
        offset += 1
        step = struct.unpack('<I', raw_data[offset:offset+4])[0]
        offset += 4
        
        # Read image data length
        data_len = struct.unpack('<I', raw_data[offset:offset+4])[0]
        offset += 4
        
        # Read image data
        image_data = raw_data[offset:offset+data_len]
        
        # Convert to numpy array based on encoding
        if encoding in ['bgr8', 'rgb8']:
            channels = 3
            dtype = np.uint8
            img_array = np.frombuffer(image_data, dtype=dtype).reshape(height, width, channels)
            
            # Convert BGR to RGB if needed
            if encoding == 'bgr8':
                img_array = cv2.cvtColor(img_array, cv2.COLOR_BGR2RGB)
                
        elif encoding in ['mono8', 'gray8']:
            channels = 1
            dtype = np.uint8
            img_array = np.frombuffer(image_data, dtype=dtype).reshape(height, width)
            
        elif encoding == 'mono16':
            channels = 1
            dtype = np.uint16
            img_array = np.frombuffer(image_data, dtype=dtype).reshape(height, width)
            # Convert to 8-bit for saving
            img_array = (img_array / 256).astype(np.uint8)
            
        else:
            print(f"Unsupported encoding: {encoding}")
            return None, 0, 0, ""
        
        return img_array, width, height, encoding
        
    except Exception as e:
        print(f"Error parsing ROS message: {e}")
        return None, 0, 0, ""

def extract_all_images_from_bag(bag_path, output_dir="all_extracted_images"):
    """Extract ALL images from bag file"""
    
    print(f"Extracting ALL images from: {bag_path}")
    print(f"Output directory: {output_dir}")
    print("=" * 60)
    
    with ROS1Reader(bag_path) as reader:
        # Get image topics
        image_topics = []
        topic_counts = {}
        for topic_name, topic_info in reader.topics.items():
            if 'Image' in topic_info.msgtype:
                image_topics.append(topic_name)
                topic_counts[topic_name] = topic_info.msgcount
        
        if not image_topics:
            print("No image topics found!")
            return
        
        print(f"Found {len(image_topics)} image topics:")
        for topic in image_topics:
            print(f"  - {topic}: {topic_counts[topic]} images")
        print()
        
        # Create output directories
        topic_dirs = create_output_directories(output_dir, image_topics)
        
        # Track extraction progress
        extraction_count = {topic: 0 for topic in image_topics}
        success_count = {topic: 0 for topic in image_topics}
        
        # Extract ALL images
        print("Extracting images...")
        for connection, timestamp, rawdata in reader.messages():
            topic_name = connection.topic
            
            if topic_name in image_topics:
                extraction_count[topic_name] += 1
                
                # Parse ROS1 image message
                img_array, width, height, encoding = parse_ros1_image_message(rawdata)
                
                if img_array is not None:
                    # Generate filename with timestamp
                    timestamp_sec = timestamp / 1e9  # Convert to seconds
                    filename = f"image_{success_count[topic_name]:04d}_{timestamp_sec:.3f}.jpg"
                    filepath = os.path.join(topic_dirs[topic_name], filename)
                    
                    # Save image
                    try:
                        if len(img_array.shape) == 3:
                            # Color image - convert RGB to BGR for OpenCV
                            cv2.imwrite(filepath, cv2.cvtColor(img_array, cv2.COLOR_RGB2BGR))
                        else:
                            # Grayscale image
                            cv2.imwrite(filepath, img_array)
                        
                        success_count[topic_name] += 1
                        
                        # Progress update
                        if success_count[topic_name] % 50 == 0:
                            print(f"  {topic_name}: saved {success_count[topic_name]} images")
                    
                    except Exception as e:
                        print(f"Error saving image from {topic_name}: {e}")
                else:
                    if extraction_count[topic_name] <= 5:  # Only show first few failures
                        print(f"Failed to decode image {extraction_count[topic_name]} from {topic_name}")
        
        # Print final results
        print("\nExtraction completed:")
        print("-" * 50)
        total_attempted = 0
        total_extracted = 0
        
        for topic in image_topics:
            attempted = extraction_count[topic]
            extracted = success_count[topic]
            success_rate = (extracted / attempted * 100) if attempted > 0 else 0
            
            total_attempted += attempted
            total_extracted += extracted
            
            print(f"{topic}:")
            print(f"  Attempted: {attempted}")
            print(f"  Successful: {extracted}")
            print(f"  Success rate: {success_rate:.1f}%")
        
        overall_success = (total_extracted / total_attempted * 100) if total_attempted > 0 else 0
        print(f"\nOverall Results:")
        print(f"  Total attempted: {total_attempted}")
        print(f"  Total extracted: {total_extracted}")
        print(f"  Overall success rate: {overall_success:.1f}%")
        
        return total_extracted

if __name__ == "__main__":
    bag_file = "../camera_data_2025-07-08-16-29-06_0.bag"
    
    if not os.path.exists(bag_file):
        print(f"Error: Bag file not found: {bag_file}")
        sys.exit(1)
    
    try:
        extracted_count = extract_all_images_from_bag(bag_file, "all_extracted_images")
        if extracted_count > 0:
            print(f"\nSuccess! Extracted {extracted_count} images to ./all_extracted_images/")
        else:
            print("No images were extracted.")
    except Exception as e:
        print(f"Error extracting images: {e}")
        sys.exit(1)