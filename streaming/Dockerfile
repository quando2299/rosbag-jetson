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

# Install MQTT dependencies
RUN apt-get update && apt-get install -y \
    libmosquitto-dev \
    mosquitto-clients \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Copy source code
COPY mqtt_client.cpp CMakeLists.txt ./

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