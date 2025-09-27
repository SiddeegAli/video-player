#include <stdio.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

extern "C" {
#include<libavformat/avformat.h>
}

int decode_video_frame(const char* file_name, AVFrame** out_video_frame);

int v_frame_width;
int v_frame_height;

unsigned int Y_txt;
unsigned int U_txt;
unsigned int V_txt;

unsigned int VAO;

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main()
{
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

// Uniforms for the three texture planes
uniform sampler2D Y_tex;
uniform sampler2D U_tex;
uniform sampler2D V_tex;

void main()
{
    // 1. Sample Y, U, V values from their respective textures.
    float Y = texture(Y_tex, TexCoord).r;
    float U = texture(U_tex, TexCoord).r;
    float V = texture(V_tex, TexCoord).r;

    // The chrominance components (U and V) are centered around 0.5.
    U = U - 0.5;
    V = V - 0.5;

    // YUV to RGB Conversion Matrix (BT.709 approximation, full range)
    // R = Y + 1.5748 * V
    // G = Y - 0.1873 * U - 0.4681 * V
    // B = Y + 1.8556 * U

    float R = Y + 1.5748 * V;
    float G = Y - 0.1873 * U - 0.4681 * V;
    float B = Y + 1.8556 * U;

    // Clamp the final colors and output
    FragColor = vec4(R, G, B, 1.0);
}
)";

unsigned int compileShader(int type, const char* source) {
    //Createing and compileing the shader
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    //Error checking
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        fprintf(stderr, "ERROR::SHADER::COMPILATION_FAILED\n%s\n", infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int createShaderProgram(const char* vs, const char* fs) {
    //Createing a vertexShader and a fragmentShader to attach to our program
    unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vs);
    unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fs);

    //Error handling
    if (vertexShader == 0 || fragmentShader == 0) return 0;

    //Creating and linking our program
    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    //Error checking
    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        fprintf(stderr, "ERROR::SHADER::PROGRAM::LINKING_FAILED\n%s\n", infoLog);
        glDeleteProgram(program);
        return 0;
    }

    //Cleanup
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void setupYUVTextures() {
    glGenTextures(1, &Y_txt);
    glGenTextures(1, &U_txt);
    glGenTextures(1, &V_txt);

    // --- Y Plane (Full Resolution) ---
    glBindTexture(GL_TEXTURE_2D, Y_txt);
    // Texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Allocate memory for Y plane (GL_R8 for monochromatic data)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, v_frame_width, v_frame_height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);

    // --- U Plane (Half Resolution) ---
    glBindTexture(GL_TEXTURE_2D, U_txt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Allocate memory for U plane
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, v_frame_width / 2, v_frame_height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);

    // --- V Plane (Half Resolution) ---
    glBindTexture(GL_TEXTURE_2D, V_txt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Allocate memory for V plane
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, v_frame_width / 2, v_frame_height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
}

// Quad setup (normalized device coordinates from -1 to 1) - UNCHANGED
void setupQuad() {
    // Vertices for a full-screen quad and corresponding texture coordinates
    float vertices[] = {
        // positions        // texture coords (inverted Y-axis) #Opengl expects the texture to be upside down so thats why we flip the txt cords
        -1.0f,  1.0f,       0.0f, 0.0f, // Top Left
        -1.0f, -1.0f,       0.0f, 1.0f, // Bottom Left
         1.0f, -1.0f,       1.0f, 1.0f, // Bottom Right

        -1.0f,  1.0f,       0.0f, 0.0f, // Top Left
         1.0f, -1.0f,       1.0f, 1.0f, // Bottom Right
         1.0f,  1.0f,       1.0f, 0.0f  // Top Right
    };

    GLuint VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0); // Unbind VAO
}

int main() {
	fprintf(stdout , "FFmpeg C++ Video Player\n");

    //Createing a video frame and giving vlaue from the decode_video_frame function see decode_video_frame.cpp for more info
	AVFrame* video_frame;
	int ret = decode_video_frame("C:\\Users\\meyzat11\\source\\repos\\video-player\\x64\\Debug\\toubun no Hanayome Movie.mp4", &video_frame);
	
    //Error handling
	if (ret < 0) {
		fprintf(stderr, "Failed to decode video frame\n");
		av_frame_free(&video_frame);
		return ret;
	}

	fprintf(stdout, "Video frame decoded successfully\n");

	//print the videos resolution
	fprintf(stdout, "Video Resolution: %dx%d\n", video_frame->width, video_frame->height);

    //Crateing a window and initing GLFW 
	GLFWwindow* window;
	if (!glfwInit())
		return -2;

	window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	glViewport(0, 0, 800, 600);

    //Ininting glew
	if (glewInit() != GLEW_OK)
		return -1;

    //Setting are global vars to give them value
	v_frame_width = video_frame->width;
	v_frame_height = video_frame->height;

    //This function allows opengl to be aware if the windows size was changed by the user
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) { glViewport(0, 0, width, height); });

    //Createing a shader to render the frame using
	unsigned int shader_program = createShaderProgram(vertexShaderSource, fragmentShaderSource);

    if (shader_program == 0) {
        fprintf(stderr, "Couldent create a program");
        return -1;
    }

    setupYUVTextures();
    setupQuad();

    // Set texture uniform locations once
    glUseProgram(shader_program);
    glUniform1i(glGetUniformLocation(shader_program, "Y_tex"), 0); // Texture unit 0
    glUniform1i(glGetUniformLocation(shader_program, "U_tex"), 1); // Texture unit 1
    glUniform1i(glGetUniformLocation(shader_program, "V_tex"), 2); // Texture unit 2


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