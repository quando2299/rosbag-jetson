#!/usr/bin/env python3
"""
Bag file analyzer to extract image data and understand topics/message types
Supports both ROS1 (.bag) and ROS2 (.db3) formats
"""

import sys
import os

# Try ROS1 bag format first
try:
    from rosbags.rosbag1 import Reader as ROS1Reader
    ros1_available = True
except ImportError:
    ros1_available = False

# Try ROS2 bag format  
try:
    from rosbags.rosbag2 import Reader as ROS2Reader
    from rosbags.serde import deserialize_cdr
    ros2_available = True
except ImportError:
    ros2_available = False

def analyze_ros1_bag(bag_path):
    """Analyze ROS1 bag file"""
    print(f"Analyzing ROS1 bag file: {bag_path}")
    print("=" * 60)
    
    with ROS1Reader(bag_path) as reader:
        print(f"Duration: {reader.duration / 1e9:.2f} seconds")
        print(f"Message count: {reader.message_count}")
        print(f"Topics: {len(reader.topics)}")
        print()
        
        # Analyze topics
        print("Topics Information:")
        print("-" * 40)
        for topic_name, topic_info in reader.topics.items():
            print(f"Topic: {topic_name}")
            print(f"  Type: {topic_info.msgtype}")
            print(f"  Count: {topic_info.msgcount}")
            print()
        
        # Sample some messages
        print("Sample Messages:")
        print("-" * 40)
        
        message_samples = {}
        sample_limit = 3
        
        for connection, timestamp, rawdata in reader.messages():
            topic_name = connection.topic
            
            if topic_name not in message_samples:
                message_samples[topic_name] = []
            
            if len(message_samples[topic_name]) < sample_limit:
                try:
                    # For ROS1, rawdata is already deserialized
                    msg = rawdata
                    message_samples[topic_name].append({
                        'timestamp': timestamp,
                        'data': msg
                    })
                    
                    if len(message_samples[topic_name]) == 1:
                        print(f"\nTopic: {topic_name}")
                        print(f"Sample message structure:")
                        print_message_structure(msg, indent=2)
                        
                except Exception as e:
                    print(f"Error processing message from {topic_name}: {e}")
        
        # Look for image topics
        image_topics = []
        for topic_name, topic_info in reader.topics.items():
            if 'Image' in topic_info.msgtype or 'image' in topic_name.lower():
                image_topics.append(topic_name)
        
        if image_topics:
            print(f"\nFound {len(image_topics)} potential image topics:")
            for topic in image_topics:
                print(f"  - {topic}")
        else:
            print("\nNo obvious image topics found.")

def analyze_ros2_bag(bag_path):
    """Analyze ROS2 bag file"""
    print(f"Analyzing ROS2 bag file: {bag_path}")
    print("=" * 60)
    
    with ROS2Reader(bag_path) as reader:
        # Get bag info
        print(f"Duration: {reader.duration / 1e9:.2f} seconds")
        print(f"Message count: {reader.message_count}")
        print(f"Topics: {len(reader.topics)}")
        print()
        
        # Analyze topics
        print("Topics Information:")
        print("-" * 40)
        for topic_name, topic_info in reader.topics.items():
            print(f"Topic: {topic_name}")
            print(f"  Type: {topic_info.msgtype}")
            print(f"  Count: {topic_info.msgcount}")
            print(f"  Serialization: {topic_info.serialization_format}")
            print()
        
        # Sample some messages to understand structure
        print("Sample Messages:")
        print("-" * 40)
        
        message_samples = {}
        sample_limit = 3  # Limit samples per topic
        
        for connection, timestamp, rawdata in reader.messages():
            topic_name = connection.topic
            
            if topic_name not in message_samples:
                message_samples[topic_name] = []
            
            if len(message_samples[topic_name]) < sample_limit:
                try:
                    msg = deserialize_cdr(rawdata, connection.msgtype)
                    message_samples[topic_name].append({
                        'timestamp': timestamp,
                        'data': msg
                    })
                    
                    if len(message_samples[topic_name]) == 1:  # Print first sample structure
                        print(f"\nTopic: {topic_name}")
                        print(f"Sample message structure:")
                        print_message_structure(msg, indent=2)
                        
                except Exception as e:
                    print(f"Error deserializing message from {topic_name}: {e}")
        
        # Look for image topics specifically
        image_topics = []
        for topic_name, topic_info in reader.topics.items():
            if 'Image' in topic_info.msgtype or 'image' in topic_name.lower():
                image_topics.append(topic_name)
        
        if image_topics:
            print(f"\nFound {len(image_topics)} potential image topics:")
            for topic in image_topics:
                print(f"  - {topic}")
        else:
            print("\nNo obvious image topics found. Check message structures above.")

def print_message_structure(msg, indent=0):
    """Print the structure of a ROS message"""
    prefix = " " * indent
    
    if hasattr(msg, '__dict__'):
        for attr, value in msg.__dict__.items():
            if hasattr(value, '__dict__'):
                print(f"{prefix}{attr}: (nested message)")
                print_message_structure(value, indent + 2)
            elif isinstance(value, (list, tuple)) and len(value) > 0:
                print(f"{prefix}{attr}: list/array of {type(value[0]).__name__} (length: {len(value)})")
                if hasattr(value[0], '__dict__'):
                    print(f"{prefix}  Sample element:")
                    print_message_structure(value[0], indent + 4)
            else:
                print(f"{prefix}{attr}: {type(value).__name__} = {str(value)[:50]}{'...' if len(str(value)) > 50 else ''}")
    else:
        print(f"{prefix}Value: {type(msg).__name__} = {str(msg)[:100]}{'...' if len(str(msg)) > 100 else ''}")

def analyze_bag_file(bag_path):
    """Main function to detect and analyze bag file format"""
    
    if not os.path.exists(bag_path):
        print(f"Error: Bag file not found: {bag_path}")
        return False
    
    # Try ROS1 format first
    if ros1_available:
        try:
            analyze_ros1_bag(bag_path)
            return True
        except Exception as e:
            print(f"Not a valid ROS1 bag file: {e}")
    
    # Try ROS2 format
    if ros2_available:
        try:
            analyze_ros2_bag(bag_path)
            return True
        except Exception as e:
            print(f"Not a valid ROS2 bag file: {e}")
    
    print("Could not read bag file in either ROS1 or ROS2 format")
    return False

if __name__ == "__main__":
    bag_file = "../camera_data_2025-07-08-16-29-06_0.bag"
    
    print(f"ROS1 support available: {ros1_available}")
    print(f"ROS2 support available: {ros2_available}")
    print()
    
    if not analyze_bag_file(bag_file):
        sys.exit(1)