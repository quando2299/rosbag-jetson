#pragma once

#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// H.264 encoder that mimics robot_simulator's automatic video encoding
// This replaces manual H.264 frame creation with proper FFmpeg encoding
class H264Encoder {
public:
    H264Encoder();
    ~H264Encoder();
    
    // Initialize encoder (like getUserMedia video settings)
    bool initialize(int width, int height, int fps = 30, int bitrate = 1000000);
    
    // Encode OpenCV Mat to H.264 (equivalent to what robot_simulator does internally)
    std::vector<uint8_t> encode(const cv::Mat& frame);
    
    // Get SPS/PPS for WebRTC initialization
    std::vector<uint8_t> getSPS() const { return sps_; }
    std::vector<uint8_t> getPPS() const { return pps_; }
    
    // Cleanup
    void cleanup();
    
private:
    AVCodec* codec_;
    AVCodecContext* codec_context_;
    AVFrame* frame_;
    AVPacket* packet_;
    SwsContext* sws_context_;
    
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    
    int width_;
    int height_;
    
    bool extractSPSPPS();
};