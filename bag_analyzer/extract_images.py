#!/usr/bin/env python3
"""
Extract images from ROS bag file and save them in organized folders
"""

import os
import sys
import cv2
import numpy as np
from rosbags.rosbag1 import Reader as ROS1Reader
from rosbags.serde import deserialize_cdr

def create_output_directories(base_dir, topics):
    """Create output directories for each camera topic"""
    os.makedirs(base_dir, exist_ok=True)
    
    topic_dirs = {}
    for topic in topics:
        # Clean topic name for directory (remove slashes and special chars)
        dir_name = topic.replace('/', '_').replace(':', '').strip('_')
        topic_dir = os.path.join(base_dir, dir_name)
        os.makedirs(topic_dir, exist_ok=True)
        topic_dirs[topic] = topic_dir
        print(f"Created directory: {topic_dir}")
    
    return topic_dirs

def decode_ros_image(raw_msg):
    """
    Decode ROS Image message from raw bytes
    ROS Image message structure (simplified):
    - header (variable length)
    - height (4 bytes)
    - width (4 bytes) 
    - encoding (variable length string)
    - is_bigendian (1 byte)
    - step (4 bytes)
    - data (variable length)
    """
    try:
        # Skip to find the image dimensions and data
        # This is a simplified parser - in production you'd want proper deserialization
        
        # Try to find width and height in the byte stream
        # Look for common image dimensions patterns
        data = bytearray(raw_msg)
        
        # Common resolutions to look for (little endian format)
        width_candidates = []
        height_candidates = []
        
        for i in range(len(data) - 4):
            val = int.from_bytes(data[i:i+4], 'little')
            if 100 <= val <= 2000:  # Reasonable image dimension range
                width_candidates.append((i, val))
        
        # Try different encoding assumptions
        # Most common: BGR8, RGB8, MONO8
        
        # Look for image data pattern - try to find the actual pixel data
        # Image data usually starts after header information
        
        # Simple heuristic: look for large continuous data blocks
        max_continuous = 0
        data_start = 0
        current_start = 0
        current_len = 0
        
        for i in range(len(data)):
            if data[i] != 0:  # Non-zero data (likely pixels)
                if current_len == 0:
                    current_start = i
                current_len += 1
            else:
                if current_len > max_continuous:
                    max_continuous = current_len
                    data_start = current_start
                current_len = 0
        
        # Check final block
        if current_len > max_continuous:
            data_start = current_start
            max_continuous = current_len
        
        # Try common image sizes based on the bag info we found
        # From analysis: likely 480x320 or similar (resized images)
        common_sizes = [
            (480, 320, 3),   # BGR8 
            (320, 480, 3),   # BGR8 rotated
            (480, 320, 1),   # MONO8
            (320, 240, 3),   # Small BGR8
            (640, 480, 3),   # VGA BGR8
            (1280, 720, 3),  # HD BGR8
        ]
        
        for width, height, channels in common_sizes:
            expected_size = width * height * channels
            if data_start + expected_size <= len(data):
                # Extract image data
                img_data = data[data_start:data_start + expected_size]
                
                try:
                    # Reshape to image
                    if channels == 3:
                        img_array = np.frombuffer(img_data, dtype=np.uint8).reshape(height, width, 3)
                        # Convert BGR to RGB for saving
                        img_array = cv2.cvtColor(img_array, cv2.COLOR_BGR2RGB)
                    else:
                        img_array = np.frombuffer(img_data, dtype=np.uint8).reshape(height, width)
                    
                    return img_array, width, height
                except:
                    continue
        
        return None, 0, 0
        
    except Exception as e:
        print(f"Error decoding image: {e}")
        return None, 0, 0

def extract_images_from_bag(bag_path, output_dir="extracted_images", max_images_per_topic=50):
    """Extract images from bag file and save to directories"""
    
    print(f"Extracting images from: {bag_path}")
    print(f"Output directory: {output_dir}")
    print(f"Max images per topic: {max_images_per_topic}")
    print("=" * 60)
    
    with ROS1Reader(bag_path) as reader:
        # Get image topics
        image_topics = []
        for topic_name, topic_info in reader.topics.items():
            if 'Image' in topic_info.msgtype:
                image_topics.append(topic_name)
        
        if not image_topics:
            print("No image topics found!")
            return
        
        print(f"Found {len(image_topics)} image topics:")
        for topic in image_topics:
            print(f"  - {topic}")
        print()
        
        # Create output directories
        topic_dirs = create_output_directories(output_dir, image_topics)
        
        # Track extraction progress
        extraction_count = {topic: 0 for topic in image_topics}
        
        # Extract images
        for connection, timestamp, rawdata in reader.messages():
            topic_name = connection.topic
            
            if topic_name in image_topics:
                if extraction_count[topic_name] >= max_images_per_topic:
                    continue
                
                # Decode image
                img_array, width, height = decode_ros_image(rawdata)
                
                if img_array is not None:
                    # Generate filename with timestamp
                    timestamp_sec = timestamp / 1e9  # Convert to seconds
                    filename = f"image_{extraction_count[topic_name]:04d}_{timestamp_sec:.3f}.jpg"
                    filepath = os.path.join(topic_dirs[topic_name], filename)
                    
                    # Save image
                    try:
                        cv2.imwrite(filepath, cv2.cvtColor(img_array, cv2.COLOR_RGB2BGR))
                        extraction_count[topic_name] += 1
                        
                        if extraction_count[topic_name] % 10 == 0:
                            print(f"  {topic_name}: extracted {extraction_count[topic_name]} images")
                    
                    except Exception as e:
                        print(f"Error saving image from {topic_name}: {e}")
                else:
                    print(f"Failed to decode image from {topic_name}")
        
        # Print final results
        print("\nExtraction completed:")
        print("-" * 40)
        total_extracted = 0
        for topic in image_topics:
            count = extraction_count[topic]
            total_extracted += count
            print(f"{topic}: {count} images")
        
        print(f"\nTotal images extracted: {total_extracted}")
        return total_extracted

if __name__ == "__main__":
    bag_file = "../camera_data_2025-07-08-16-29-06_0.bag"
    
    if not os.path.exists(bag_file):
        print(f"Error: Bag file not found: {bag_file}")
        sys.exit(1)
    
    try:
        extracted_count = extract_images_from_bag(bag_file, "extracted_images", max_images_per_topic=20)
        if extracted_count > 0:
            print(f"\nSuccess! Extracted {extracted_count} images to ./extracted_images/")
        else:
            print("No images were extracted.")
    except Exception as e:
        print(f"Error extracting images: {e}")
        sys.exit(1)