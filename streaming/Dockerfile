# Multi-stage Dockerfile for MQTT streaming client
# Works on both Mac (x86_64) and Jetson (aarch64)

FROM ubuntu:20.04

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    wget \
    curl \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# Install MQTT and JSON dependencies
RUN apt-get update && apt-get install -y \
    libmosquitto-dev \
    mosquitto-clients \
    libjsoncpp-dev \
    && rm -rf /var/lib/apt/lists/*

# Install WebRTC and OpenCV dependencies
RUN apt-get update && apt-get install -y \
    libssl-dev \
    libnice-dev \
    libsrtp2-dev \
    libopencv-dev \
    libavcodec-dev \
    libavformat-dev \
    libswscale-dev \
    && rm -rf /var/lib/apt/lists/*

# Install GStreamer and plugins for video capture and encoding
RUN apt-get update && apt-get install -y \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    v4l-utils \
    && rm -rf /var/lib/apt/lists/*

# Build and install libdatachannel from source (lightweight WebRTC library)
RUN git clone --depth 1 --branch v0.20.0 https://github.com/paullouisageneau/libdatachannel.git /tmp/libdatachannel && \
    cd /tmp/libdatachannel && \
    git submodule update --init --recursive && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DNO_EXAMPLES=1 -DNO_TESTS=1 && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    rm -rf /tmp/libdatachannel

# Set working directory
WORKDIR /workspace

# Copy source code
COPY mqtt_client.cpp CMakeLists.txt webrtc_manager.hpp webrtc_manager.cpp ./

# Copy video files directory (prepare-videos.sh should be run first)
# COPY videos/ /workspace/videos/

# Create build directory and build
RUN mkdir -p build && cd build && \
    cmake .. && \
    make

# Copy entrypoint script
COPY docker-entrypoint.sh ./
RUN chmod +x docker-entrypoint.sh

# Set working directory to build for execution
WORKDIR /workspace/build

ENTRYPOINT ["/workspace/docker-entrypoint.sh"]
CMD ["./mqtt_client"]