#include <stdio.h>

extern "C" {
#include<libavcodec/avcodec.h>
#include<libavformat/avformat.h>
}

AVFormatContext* fmt_ctx;
AVCodecContext* codec_ctx = nullptr;
AVPacket* packet = nullptr;
int video_stream_index = -1;
// Global variable to hold the stream's time base (Crucial for PTS conversion)
AVRational video_stream_time_base;

void print_ffmpeerr(int err_code) {
    char err_buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err_code, err_buf, sizeof(err_buf));
    fprintf(stderr, "FFmpeg Error: %s\n", err_buf);
}

// Initialization: Opens the file and sets up the decoding context.
// frame_delay_out is now unused but kept in signature for compatibility.
int init_ffmpeg(const char* file_name, int* w, int* h, double* frame_delay_out, AVFrame** frame) {
    int ret;

    // 1. Open the file
    ret = avformat_open_input(&fmt_ctx, file_name, nullptr, nullptr);
    if (ret < 0) {
        fprintf(stderr, "Could not open input file: %s\n", file_name);
        print_ffmpeerr(ret);
        return ret;
    }

    // 2. Find stream info
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        print_ffmpeerr(ret);
        return ret;
    }

    // 3. Find video stream and its decoder
    const AVCodec* codec;
    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);

    if (video_stream_index == -1) {
        fprintf(stderr, "No video stream found\n");
        return -1;
    }

    if (!codec) {
        fprintf(stderr, "Unsupported codec\n");
        return -1;
    }

    AVCodecParameters* codec_par = fmt_ctx->streams[video_stream_index]->codecpar;

    // 4. Calculate and save the stream's time base and frame rate info (for logging)
    video_stream_time_base = fmt_ctx->streams[video_stream_index]->time_base;
    AVRational frame_rate = av_guess_frame_rate(fmt_ctx, fmt_ctx->streams[video_stream_index], nullptr);

    double frame_delay = 0.0;
    if (frame_rate.num > 0 && frame_rate.den > 0) {
        frame_delay = 1.0 / av_q2d(frame_rate);
    }
    else {
        // Fallback for logging if rate estimation fails
        frame_delay = 1.0 / 30.0;
    }

    // 5. Create codec context and copy parameters
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate codec context\n");
        return -1;
    }

    ret = avcodec_parameters_to_context(codec_ctx, codec_par);
    if (ret < 0) {
        print_ffmpeerr(ret);
        return ret;
    }

    // 6. Open the codec
    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) {
        print_ffmpeerr(ret);
        return ret;
    }

    // 7. Allocate packet and frame
    packet = av_packet_alloc();
    *frame = av_frame_alloc();
    if (!packet || !frame) {
        fprintf(stderr, "Could not allocate packet or frame\n");
        return -1;
    }

    // output vars
    *w = codec_ctx->width;
    *h = codec_ctx->height;
    *frame_delay_out = frame_delay; // frame_delay_out is now just for logging

    fprintf(stdout, "Video Resolution: %dx%d\n", *w, *h);
    fprintf(stdout, "Target Frame Delay (Estimated): %.4f seconds (%.2f FPS)\n", frame_delay, 1.0 / frame_delay);
    fprintf(stdout, "Using PTS Time Base: %d/%d\n", video_stream_time_base.num, video_stream_time_base.den);


    return 0;
}

// Frame Retrieval: Reads, decodes, and returns 0 if successful, or <0 on error/EOF.
// The frame's presentation timestamp (PTS) in seconds is returned via pts_out.
int get_next_frame(AVFrame** frame, double* pts_out) {
    int ret;

    // Loop until a video frame is decoded
    while (ret = av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {

            // Send packet to decoder
            ret = avcodec_send_packet(codec_ctx, packet);
            av_packet_unref(packet); // Always unref the packet after sending

            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                print_ffmpeerr(ret);
                return ret; // Decoding error
            }

            // Receive decoded frame (can yield multiple frames, typically one)
            ret = avcodec_receive_frame(codec_ctx, *frame);

            if (ret == AVERROR(EAGAIN)) {
                // Decoder buffer empty, need to read more packets
                continue;
            }
            else if (ret == AVERROR_EOF) {
                // Stream finished
                return AVERROR_EOF;
            }
            else if (ret < 0) {
                print_ffmpeerr(ret);
                return ret; // Decoding error
            }

            // Frame successfully decoded: calculate and return its timestamp in seconds.
            // NOTE: AVFrame->pts contains the presentation timestamp in time_base units.
            if ((*frame)->pts != AV_NOPTS_VALUE) {
                *pts_out = av_q2d(video_stream_time_base) * (*frame)->pts;
            }
            else {
                // If PTS is missing, we must use the old approach or rely on the container. 
                // For simplicity here, we'll signal an error if PTS is absolutely necessary.
                fprintf(stderr, "Warning: Decoded frame has no valid PTS. Using 0.0\n");
                *pts_out = 0.0;
            }

            return 0;
        }
        av_packet_unref(packet); // Unref packet if it's not the video stream
    }

    // End of file reached (or error during av_read_frame)
    // print_ffmpeerr(ret); // Suppress error print on EOF from av_read_frame

    return ret;
}

// Cleanup: Frees all allocated resources.
void cleanup_ffmpeg() {
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
}