#include "h264_encoder.hpp"
#include <iostream>

H264Encoder::H264Encoder() 
    : codec_(nullptr), codec_context_(nullptr), frame_(nullptr), 
      packet_(nullptr), sws_context_(nullptr), width_(0), height_(0) {
}

H264Encoder::~H264Encoder() {
    cleanup();
}

bool H264Encoder::initialize(int width, int height, int fps, int bitrate) {
    width_ = width;
    height_ = height;
    
    std::cout << "🎬 Initializing H.264 encoder (like robot_simulator automatic encoding)" << std::endl;
    std::cout << "   Resolution: " << width << "x" << height << std::endl;
    std::cout << "   FPS: " << fps << ", Bitrate: " << bitrate << " bps" << std::endl;
    
    // Find H.264 encoder
    codec_ = const_cast<AVCodec*>(avcodec_find_encoder(AV_CODEC_ID_H264));
    if (!codec_) {
        std::cerr << "❌ H.264 encoder not found" << std::endl;
        return false;
    }
    
    // Create codec context
    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        std::cerr << "❌ Could not allocate codec context" << std::endl;
        return false;
    }
    
    // Set encoding parameters (similar to robot_simulator's video constraints)
    codec_context_->bit_rate = bitrate;
    codec_context_->width = width;
    codec_context_->height = height;
    codec_context_->time_base = (AVRational){1, fps};
    codec_context_->framerate = (AVRational){fps, 1};
    codec_context_->gop_size = 30; // Keyframe every 30 frames
    codec_context_->max_b_frames = 0; // No B-frames for low latency
    codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // Set H.264 specific options for WebRTC compatibility
    av_opt_set(codec_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(codec_context_->priv_data, "profile", "baseline", 0);
    av_opt_set(codec_context_->priv_data, "level", "3.1", 0);
    
    // CRITICAL: Disable SEI timestamps to prevent RMCS decoder confusion
    av_opt_set(codec_context_->priv_data, "x264opts", "no-scenecut:no-b-adapt:nal-hrd=none", 0);
    
    // Important for WebRTC: use annexb format (start codes)
    codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    
    // Open codec
    if (avcodec_open2(codec_context_, codec_, nullptr) < 0) {
        std::cerr << "❌ Could not open H.264 codec" << std::endl;
        return false;
    }
    
    // Allocate frame
    frame_ = av_frame_alloc();
    if (!frame_) {
        std::cerr << "❌ Could not allocate frame" << std::endl;
        return false;
    }
    
    frame_->format = codec_context_->pix_fmt;
    frame_->width = codec_context_->width;
    frame_->height = codec_context_->height;
    
    if (av_frame_get_buffer(frame_, 32) < 0) {
        std::cerr << "❌ Could not allocate frame buffer" << std::endl;
        return false;
    }
    
    // Allocate packet
    packet_ = av_packet_alloc();
    if (!packet_) {
        std::cerr << "❌ Could not allocate packet" << std::endl;
        return false;
    }
    
    // Initialize SWS context for BGR to YUV420P conversion
    sws_context_ = sws_getContext(width, height, AV_PIX_FMT_BGR24,
                                  width, height, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_context_) {
        std::cerr << "❌ Could not create SWS context" << std::endl;
        return false;
    }
    
    // Extract SPS/PPS for WebRTC
    extractSPSPPS();
    
    std::cout << "✅ H.264 encoder initialized successfully" << std::endl;
    return true;
}

std::vector<uint8_t> H264Encoder::encode(const cv::Mat& frame) {
    std::vector<uint8_t> encoded_data;
    
    if (!codec_context_ || frame.empty()) {
        return encoded_data;
    }
    
    try {
        // Make sure frame is writable
        if (av_frame_make_writable(frame_) < 0) {
            std::cerr << "❌ Frame not writable" << std::endl;
            return encoded_data;
        }
        
        // Convert OpenCV Mat (BGR) to YUV420P
        const uint8_t* src_data[1] = { frame.data };
        int src_linesize[1] = { static_cast<int>(frame.step[0]) };
        
        sws_scale(sws_context_, src_data, src_linesize, 0, height_,
                  frame_->data, frame_->linesize);
        
        // Set frame PTS
        static int64_t pts = 0;
        frame_->pts = pts++;
        
        // Encode frame
        int ret = avcodec_send_frame(codec_context_, frame_);
        if (ret < 0) {
            std::cerr << "❌ Error sending frame to encoder" << std::endl;
            return encoded_data;
        }
        
        // Receive encoded packet
        ret = avcodec_receive_packet(codec_context_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No packet available yet
            return encoded_data;
        } else if (ret < 0) {
            std::cerr << "❌ Error receiving packet from encoder" << std::endl;
            return encoded_data;
        }
        
        // Copy encoded data (with start codes for WebRTC)
        encoded_data.resize(packet_->size);
        std::memcpy(encoded_data.data(), packet_->data, packet_->size);
        
        // Filter out SEI NAL units to prevent RMCS decoder confusion
        encoded_data = filterNALUnits(encoded_data);
        
        // Unref packet for next use
        av_packet_unref(packet_);
        
        // Debug output
        static int frame_count = 0;
        if (frame_count % 60 == 0) { // Every 2 seconds at 30fps
            std::cout << "📺 H.264 frame encoded: " << encoded_data.size() << " bytes" 
                      << " (frame " << frame_count << ")" << std::endl;
        }
        frame_count++;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in H.264 encoding: " << e.what() << std::endl;
    }
    
    return encoded_data;
}

bool H264Encoder::extractSPSPPS() {
    if (!codec_context_ || !codec_context_->extradata) {
        std::cout << "⚠️  No extradata available for SPS/PPS extraction" << std::endl;
        return false;
    }
    
    // Parse extradata to extract SPS and PPS
    uint8_t* data = codec_context_->extradata;
    int size = codec_context_->extradata_size;
    
    // Simple SPS/PPS extraction (for more robust parsing, use proper H.264 parser)
    for (int i = 0; i < size - 4; i++) {
        if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01) {
            uint8_t nal_type = data[i+4] & 0x1F;
            
            if (nal_type == 7) { // SPS
                int sps_start = i;
                int sps_end = size;
                
                // Find next start code
                for (int j = i + 4; j < size - 4; j++) {
                    if (data[j] == 0x00 && data[j+1] == 0x00 && data[j+2] == 0x00 && data[j+3] == 0x01) {
                        sps_end = j;
                        break;
                    }
                }
                
                sps_.assign(data + sps_start, data + sps_end);
                std::cout << "✅ SPS extracted: " << sps_.size() << " bytes" << std::endl;
                
            } else if (nal_type == 8) { // PPS
                int pps_start = i;
                int pps_end = size;
                
                // Find next start code
                for (int j = i + 4; j < size - 4; j++) {
                    if (data[j] == 0x00 && data[j+1] == 0x00 && data[j+2] == 0x00 && data[j+3] == 0x01) {
                        pps_end = j;
                        break;
                    }
                }
                
                pps_.assign(data + pps_start, data + pps_end);
                std::cout << "✅ PPS extracted: " << pps_.size() << " bytes" << std::endl;
            }
        }
    }
    
    return !sps_.empty() && !pps_.empty();
}

void H264Encoder::cleanup() {
    if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }
    
    if (packet_) {
        av_packet_free(&packet_);
    }
    
    if (frame_) {
        av_frame_free(&frame_);
    }
    
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
}

std::vector<uint8_t> H264Encoder::filterNALUnits(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> filtered_data;
    
    if (data.size() < 4) {
        return data; // Too small to contain NAL units
    }
    
    const uint8_t* ptr = data.data();
    size_t remaining = data.size();
    
    std::cout << "🔍 Filtering NAL units from " << data.size() << " bytes" << std::endl;
    
    while (remaining >= 4) {
        // Look for start codes (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
        bool found_start = false;
        size_t start_code_len = 0;
        
        if (ptr[0] == 0x00 && ptr[1] == 0x00 && ptr[2] == 0x00 && ptr[3] == 0x01) {
            found_start = true;
            start_code_len = 4;
        } else if (ptr[0] == 0x00 && ptr[1] == 0x00 && ptr[2] == 0x01) {
            found_start = true;
            start_code_len = 3;
        }
        
        if (found_start && remaining > start_code_len) {
            uint8_t nal_type = ptr[start_code_len] & 0x1F;
            
            // Find the end of this NAL unit (next start code)
            size_t nal_end = remaining;
            for (size_t i = start_code_len + 1; i < remaining - 3; i++) {
                if ((ptr[i] == 0x00 && ptr[i+1] == 0x00 && ptr[i+2] == 0x00 && ptr[i+3] == 0x01) ||
                    (ptr[i] == 0x00 && ptr[i+1] == 0x00 && ptr[i+2] == 0x01)) {
                    nal_end = i;
                    break;
                }
            }
            
            // Filter logic: exclude SEI NAL units (type 6)
            if (nal_type == 6) { // SEI NAL unit - SKIP IT!
                std::cout << "🚫 Skipping SEI NAL unit (type 6) - " << (nal_end) << " bytes" << std::endl;
                ptr += nal_end;
                remaining -= nal_end;
                continue;
            } else {
                // Keep this NAL unit (SPS=7, PPS=8, IDR=5, P=1, etc.)
                std::cout << "✅ Keeping NAL unit type " << (int)nal_type << " - " << nal_end << " bytes" << std::endl;
                filtered_data.insert(filtered_data.end(), ptr, ptr + nal_end);
                ptr += nal_end;
                remaining -= nal_end;
            }
        } else {
            // No start code found, advance by 1 byte
            filtered_data.push_back(*ptr);
            ptr++;
            remaining--;
        }
    }
    
    // Add any remaining bytes
    if (remaining > 0) {
        filtered_data.insert(filtered_data.end(), ptr, ptr + remaining);
    }
    
    std::cout << "📏 Filtered data: " << data.size() << " -> " << filtered_data.size() << " bytes" << std::endl;
    
    return filtered_data;
}