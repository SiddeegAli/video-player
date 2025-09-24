#include <stdio.h>

extern "C" {
#include<libavcodec/avcodec.h>
#include<libavformat/avformat.h>
}

void print_ffmpeg_err(int err_code) {
	char err_buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(err_code, err_buf, sizeof(err_buf));
	fprintf(stderr, "FFmpeg Error: %s\n", err_buf);
}

int decode_video_frame(const char* file_name, AVFrame** out_video_frame) {
	//Opening the file and creating a format context
	AVFormatContext* fmt_ctx = nullptr;
	int ret = avformat_open_input(&fmt_ctx, file_name, nullptr, nullptr);
	if (ret < 0) {
		print_ffmpeg_err(ret);
		return ret;
	}

	//Finding the stream info
	ret = avformat_find_stream_info(fmt_ctx, nullptr);
	if (ret < 0) {
		print_ffmpeg_err(ret);
		avformat_close_input(&fmt_ctx);
		return ret;
	}

	//Finding the video stream and its decoder
	const AVCodec* codec;
	int video_stream_index = -1;
	video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);


	if (video_stream_index == -1) {
		fprintf(stderr, "No video stream found\n");
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	if (!codec) {
		fprintf(stderr, "Unsupported codec\n");
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	AVCodecParameters* codec_par = fmt_ctx->streams[video_stream_index]->codecpar;

	//Creating a codec context for the decoder
	AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		fprintf(stderr, "Could not allocate codec context\n");
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	//Copying codec parameters to codec context
	ret = avcodec_parameters_to_context(codec_ctx, codec_par);
	if (ret < 0) {
		print_ffmpeg_err(ret);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		return ret;
	}

	//Opening the codec
	ret = avcodec_open2(codec_ctx, codec, nullptr);
	if (ret < 0) {
		print_ffmpeg_err(ret);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		return ret;
	}

	//Createing packet and frame to hold the data
	AVPacket* packet = av_packet_alloc();
	if (!packet) {
		fprintf(stderr, "Could not allocate packet\n");
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	AVFrame* frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		av_packet_free(&packet);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	//Reading frames from the file
	bool frame_decoded = false;	
	while (av_read_frame(fmt_ctx, packet) >= 0) {
		if (packet->stream_index == video_stream_index) {
			//Sending the packet to the decoder
			ret = avcodec_send_packet(codec_ctx, packet);
			if (ret < 0) {
				print_ffmpeg_err(ret);
				break;
			}

			//Receiving the decoded frame
			ret = avcodec_receive_frame(codec_ctx, frame);
			if (ret == AVERROR_EOF) {
				// End of file reached
				av_packet_unref(packet);
				break; // Break only on EOF
			}
			else if (ret == AVERROR(EAGAIN)) {
				// Need more packets
				av_packet_unref(packet);
				continue; // Continue on EAGAIN
			}

			else if (ret < 0) {
				print_ffmpeg_err(ret);
				break;
			}

			//Successfully decoded a frame
			frame_decoded = true;
			break;
		}
	}

	//Checking if a frame was decoded
	if (!frame_decoded) {
		fprintf(stderr, "Could not decode a frame\n");
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		return -1;
	}

	//Setting the output frame
	*out_video_frame = frame;

	//Cleaning up
	av_packet_free(&packet);
	avcodec_free_context(&codec_ctx);
	avformat_close_input(&fmt_ctx);
	return 0;
}