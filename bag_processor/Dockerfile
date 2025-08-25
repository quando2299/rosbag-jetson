# Multi-stage Dockerfile for ROS bag processor
# Works on both Mac (x86_64) and Jetson (aarch64)

FROM ros:melodic-ros-base-bionic

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=melodic

# Install system dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    wget \
    curl \
    libc6-dev \
    libpthread-stubs0-dev \
    && rm -rf /var/lib/apt/lists/*

# Install ROS packages
RUN apt-get update && apt-get install -y \
    ros-melodic-rosbag \
    ros-melodic-sensor-msgs \
    ros-melodic-cv-bridge \
    ros-melodic-roscpp \
    ros-melodic-cpp-common \
    && rm -rf /var/lib/apt/lists/*

# Install OpenCV
RUN apt-get update && apt-get install -y \
    libopencv-dev \
    libopencv-contrib-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Boost and FFmpeg
RUN apt-get update && apt-get install -y \
    libboost-all-dev \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Copy source code
COPY . /workspace/

# Source ROS environment
RUN echo "source /opt/ros/melodic/setup.bash" >> ~/.bashrc
RUN /bin/bash -c "source /opt/ros/melodic/setup.bash"

# Create build directory and build
RUN mkdir -p build && cd build && \
    /bin/bash -c "source /opt/ros/melodic/setup.bash && \
    cp ../CMakeLists.txt . && \
    cp ../rosbag_analyzed.cpp . && \
    cmake . \
        -DCMAKE_CXX_STANDARD=14 \
        -DCMAKE_CXX_FLAGS='-pthread -std=c++14' \
        -DCMAKE_EXE_LINKER_FLAGS='-pthread' \
        -DBoost_USE_STATIC_LIBS=OFF \
        -DBoost_USE_MULTITHREADED=ON && \
    make VERBOSE=1"

# Set entrypoint
WORKDIR /workspace/build
COPY docker-entrypoint.sh /workspace/
RUN chmod +x /workspace/docker-entrypoint.sh

ENTRYPOINT ["/workspace/docker-entrypoint.sh"]
CMD ["./rosbag_analyzed"]