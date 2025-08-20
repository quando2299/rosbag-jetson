# Docker ROS Bag Processor

Cross-platform Docker solution for analyzing and extracting images from ROS bag files.

## Quick Start

### 1. Build the Docker Image
```bash
cd bag_processor
./docker-build.sh
```

### 2. Run the Container
```bash
./docker-run.sh
```

## What It Does

1. ✅ **Analyzes the bag file**: Shows topics, message counts, duration
2. ✅ **Extracts ALL images**: 3,069 images from 7 camera topics
3. ✅ **Organizes by camera**: Separate folders for each camera
4. ✅ **Cross-platform**: Works on Mac (x86_64) and Jetson (aarch64)

## File Structure Expected

```
m2m/
├── camera_data_2025-07-08-16-29-06_0.bag  ← Your bag file
└── jetson/
    └── bag_processor/
        ├── docker-build.sh  ← Build script
        ├── docker-run.sh    ← Run script
        └── Dockerfile       ← Container definition
```

## Output Structure

```
bag_processor/
└── cpp_extracted_images/
    ├── flir_id8_image_resized/     (438 images)
    ├── leopard_id1_image_resized/  (438 images)
    ├── leopard_id3_image_resized/  (439 images)
    ├── leopard_id4_image_resized/  (438 images)
    ├── leopard_id5_image_resized/  (438 images)
    ├── leopard_id6_image_resized/  (439 images)
    └── leopard_id7_image_resized/  (439 images)
```

## Expected Output

```
=== ANALYZING BAG FILE ===
Bag file: /workspace/m2m/camera_data_2025-07-08-16-29-06_0.bag
Duration: 43.83 seconds
Message count: 3069
Topics: 7

=== EXTRACTING IMAGES ===
  /leopard/id1/image_resized: saved 50 images
  [... progress updates ...]

Extraction completed:
Overall Results:
  Total extracted: 3069
  Overall success rate: 100.0%

✅ Bag processing completed successfully!
```

## Platform Support

- ✅ **Mac (Intel/Apple Silicon)**: Uses `linux/amd64` platform
- ✅ **Jetson (ARM64)**: Uses `linux/arm64` platform
- ✅ **Auto-detection**: Automatically selects correct platform

## Troubleshooting

### Issue: "Docker image not found"
```bash
# Rebuild the image
./docker-build.sh
```

### Issue: "Bag file not found"
Check the file structure - bag file should be in `m2m/` directory:
```bash
ls -la ../../camera_data_2025-07-08-16-29-06_0.bag
```

### Issue: "Permission denied"
```bash
# Make scripts executable
chmod +x docker-*.sh
```

### Issue: Docker build fails on Jetson
```bash
# Try with specific platform
docker build --platform linux/arm64 -t bag-processor:latest .
```

## Performance

- **Mac**: ~2-3 minutes for full extraction
- **Jetson**: ~1-2 minutes (native ARM performance)
- **Memory Usage**: ~500MB RAM during processing
- **Output Size**: ~150MB for all extracted images

## Manual Docker Commands

If the scripts don't work, use these manual commands:

### Build
```bash
docker build -t bag-processor:latest .
```

### Run
```bash
docker run --rm -it \
  -v "$(pwd)/../..:/workspace/m2m" \
  -v "$(pwd)/cpp_extracted_images:/workspace/build/cpp_extracted_images" \
  bag-processor:latest
```

## Next Steps

After successful extraction:
1. ✅ Images are ready for H.264 encoding pipeline
2. ✅ Can be integrated with MQTT/WebRTC streaming
3. ✅ Ready for Jetson deployment with GPU acceleration

The Docker approach eliminates dependency issues and ensures consistent results across platforms!