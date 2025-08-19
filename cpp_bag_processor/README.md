# C++ Bag Processor

Combined C++ implementation of bag file analyzer and image extractor.

## Features

1. **Analyze Bag File**: 
   - Shows duration, message count, topics
   - Identifies image topics automatically
   - Displays topic information and message counts

2. **Extract Images**:
   - Extracts ALL images from all image topics
   - Saves to organized directories by topic
   - Supports multiple encodings (bgr8, rgb8, mono8, mono16)
   - Progress tracking and success rate reporting

## Dependencies

- ROS (rosbag, sensor_msgs, cv_bridge, roscpp)
- OpenCV
- Boost (system, filesystem)
- C++17 compiler

## Build Instructions

### For ROS Catkin Workspace:
```bash
# Copy to your catkin workspace
cp -r cpp_bag_processor ~/catkin_ws/src/

# Build
cd ~/catkin_ws
catkin_make

# Run
./devel/lib/bag_processor/bag_processor
```

### For Standalone Build:
```bash
cd cpp_bag_processor
mkdir build && cd build
cmake ..
make
./bag_processor
```

## Usage

The program automatically:
1. Analyzes `../../camera_data_2025-07-08-16-29-06_0.bag` (in m2m folder)
2. Creates `cpp_extracted_images/` directory
3. Extracts all images to organized subdirectories

## Output Structure

```
cpp_extracted_images/
├── flir_id8_image_resized/
├── leopard_id1_image_resized/
├── leopard_id3_image_resized/
├── leopard_id4_image_resized/
├── leopard_id5_image_resized/
├── leopard_id6_image_resized/
└── leopard_id7_image_resized/
```

Each image is named: `image_XXXX_timestamp.jpg`

## Performance Benefits

- **Faster**: Native C++ performance
- **Lower Memory**: No Python overhead
- **Better Integration**: Direct ROS message handling
- **Jetson Ready**: Optimized for embedded deployment