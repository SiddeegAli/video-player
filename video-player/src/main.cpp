#include <stdio.h>

extern "C" {
#include<libavformat/avformat.h>
}

int decode_video_frame(const char* file_name, AVFrame** out_video_frame);

int main() {
	fprintf(stdout , "FFmpeg C++ Video Player");
	
	AVFrame* video_frame;

	int ret = decode_video_frame("toubun no Hanayome Movie.mp4", &video_frame);
	
	if (ret < 0) {
		fprintf(stderr, "Failed to decode video frame\n");
		return ret;
	}

	fprintf(stdout, "Video frame decoded successfully\n");

	//print the videos resolution
	fprintf(stdout, "Video Resolution: %dx%d\n", video_frame->width, video_frame->height);

	// Free the allocated frame
	av_frame_free(&video_frame);

	return 0;
}