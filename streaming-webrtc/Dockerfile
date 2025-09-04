FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    wget \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Install libraries
RUN apt-get update && apt-get install -y \
    libssl-dev \
    libmosquitto-dev \
    libjsoncpp-dev \
    libopencv-dev \
    libnice-dev \
    libsrtp2-dev \
    && rm -rf /var/lib/apt/lists/*

# Build and install libdatachannel (latest version)
RUN git clone --depth 1 https://github.com/paullouisageneau/libdatachannel.git /tmp/libdatachannel && \
    cd /tmp/libdatachannel && \
    git submodule update --init --recursive --depth 1 && \
    cmake -B build -DUSE_GNUTLS=0 -DUSE_NICE=0 -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j2 && \
    cmake --install build && \
    ldconfig && \
    rm -rf /tmp/libdatachannel

WORKDIR /app

# Copy source files
COPY mqtt_client.cpp ./
COPY CMakeLists.txt ./
COPY dispatchqueue.hpp ./
COPY dispatchqueue.cpp ./
COPY helpers.hpp ./
COPY helpers.cpp ./
COPY fileparser.hpp ./
COPY fileparser.cpp ./
COPY h264fileparser.hpp ./
COPY h264fileparser.cpp ./
COPY stream.hpp ./

# Copy H264 sample files
COPY h264_samples/ ./h264_samples/

# Build application
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

WORKDIR /app/build

CMD ["./mqtt_client"]