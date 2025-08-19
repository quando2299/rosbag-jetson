# Jetson Deployment Guide

## Quick Start on Jetson

1. **Clone/Pull your repository**
2. **Navigate to the project**:
   ```bash
   cd jetson/cpp_bag_processor
   ```

3. **Run the build script**:
   ```bash
   ./build_jetson.sh
   ```

4. **Run the processor**:
   ```bash
   cd build
   ./bag_processor
   ```

## What the build script does:

1. ✅ **Detects ROS** (Melodic/Noetic/Kinetic)
2. ✅ **Installs dependencies** automatically:
   - `ros-$ROS_DISTRO-rosbag`
   - `ros-$ROS_DISTRO-sensor-msgs` 
   - `ros-$ROS_DISTRO-cv-bridge`
   - `libopencv-dev`
   - `libboost-all-dev`
3. ✅ **Builds with CMake** 
4. ✅ **Creates executable**: `./bag_processor`

## Expected Output:

```
=== ANALYZING BAG FILE ===
Bag file: ../../camera_data_2025-07-08-16-29-06_0.bag
==============================
Duration: 43.83 seconds
Message count: 3069
Topics: 7

Topics Information:
----------------------------------------
Topic: /flir/id8/image_resized
  Type: sensor_msgs/Image
  Count: 438

[... all 7 topics listed ...]

=== CREATING OUTPUT DIRECTORIES ===
Created directory: cpp_extracted_images/flir_id8_image_resized
[... all 7 directories created ...]

=== EXTRACTING IMAGES ===
Extracting ALL images from bag file...
  /leopard/id1/image_resized: saved 50 images
  [... progress updates ...]

Extraction completed:
--------------------------------------------------
Overall Results:
  Total attempted: 3069
  Total extracted: 3069
  Overall success rate: 100.0%

✅ Bag processing completed successfully!
```

## File Structure After Build:

```
cpp_bag_processor/
├── build/
│   └── bag_processor          # Main executable
├── cpp_extracted_images/      # Output images
│   ├── flir_id8_image_resized/
│   ├── leopard_id1_image_resized/
│   └── [... 5 more camera dirs]
├── bag_processor.cpp          # Full ROS version
├── build_jetson.sh           # Build script
└── README_JETSON.md          # This file
```

## Troubleshooting:

**If ROS not found:**
```bash
# Install ROS Melodic (for Ubuntu 18.04)
sudo apt update
sudo apt install ros-melodic-desktop-full

# Install ROS Noetic (for Ubuntu 20.04)  
sudo apt install ros-noetic-desktop-full
```

**If build fails:**
```bash
# Clean and rebuild
rm -rf build
./build_jetson.sh
```

**If bag file not found:**
- Make sure `camera_data_2025-07-08-16-29-06_0.bag` is in the `m2m/` folder
- Check path: `m2m/camera_data_2025-07-08-16-29-06_0.bag`

## Performance on Jetson:

- **Much faster** than Python version
- **GPU acceleration** available with OpenCV
- **Native ROS** message handling
- **Low memory usage**

The C++ version should extract all 3,069 images in seconds instead of minutes!