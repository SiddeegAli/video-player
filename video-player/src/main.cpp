#include <stdio.h>

extern "C" {
#include<libavcodec/avcodec.h>
#include<libavformat/avformat.h>
}

int main() {
	fprintf(stdout , "FFmpeg C++ Video Player");
	return 0;
}