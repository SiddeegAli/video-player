#include <stdio.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

extern "C" {
#include<libavformat/avformat.h>
}

int decode_video_frame(const char* file_name, AVFrame** out_video_frame);

int v_frame_width;
int v_frame_height;

int main() {
	fprintf(stdout , "FFmpeg C++ Video Player\n");

	AVFrame* video_frame;

	int ret = decode_video_frame("toubun no Hanayome Movie.mp4", &video_frame);
	
	if (ret < 0) {
		fprintf(stderr, "Failed to decode video frame\n");
		av_frame_free(&video_frame);
		return ret;
	}

	fprintf(stdout, "Video frame decoded successfully\n");

	//print the videos resolution
	fprintf(stdout, "Video Resolution: %dx%d\n", video_frame->width, video_frame->height);

	GLFWwindow* window;

	if (!glfwInit())
		return -2;

	window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
	glfwMakeContextCurrent(window);

	if (glewInit() != GLEW_OK)
		return -1;

	v_frame_width = video_frame->width;
	v_frame_height = video_frame->height;

	while (!glfwWindowShouldClose(window)) {

		glClear(GL_COLOR_BUFFER_BIT);

		glClearColor(1.0f, 1.0f, 0.0f, 1.0f);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	// Free the allocated frame
	av_frame_free(&video_frame);
	glfwTerminate();
	return 0;
}