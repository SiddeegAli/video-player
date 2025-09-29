#include <stdio.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <thread>
extern "C" {
#include<libavformat/avformat.h>
#include <chrono>

}

// Declaring the functions that are in decode_video
// NOTE: Modified signature to include 'double* pts_out'
int init_ffmpeg(const char* file_name, int* w, int* h, double* frame_delay_out, AVFrame** frame);
int get_next_frame(AVFrame** frame, double* pts_out);
void cleanup_ffmpeg();

// Global vars used
int v_frame_width;
int v_frame_height;

unsigned int Y_txt;
unsigned int U_txt;
unsigned int V_txt;

unsigned int shader_program;
unsigned int VAO;

// ... (vertexShaderSource and fragmentShaderSource remain unchanged) ...

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
    float R = Y + 1.5748 * V;
    float G = Y - 0.1873 * U - 0.4681 * V;
    float B = Y + 1.8556 * U;

    // Clamp the final colors and output
    FragColor = vec4(R, G, B, 1.0);
}
)";


// ... (compileShader, createShaderProgram, setupYUVTextures, setupQuad, and updateYUVTexturesFromAVFrame remain unchanged) ...

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

// Function to update the texture data using the FFmpeg AVFrame structure
// This is the CRITICAL integration point, handling FFmpeg's linesize.
void updateYUVTexturesFromAVFrame(AVFrame* frame) {
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) {
        fprintf(stderr, "Error: Invalid or non-YUV420P frame provided.");
        return;
    }

    // Set GL_UNPACK_ALIGNMENT to 1 byte, as FFmpeg data is usually tightly packed by row
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // The key is using glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[i])
    // This tells OpenGL that each row of source data is padded (has a stride) equal to linesize.

    // 1. Update Y Plane
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Y_txt);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]); // Padded source row length
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);

    // 2. Update U Plane
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, U_txt);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width / 2, frame->height / 2, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);

    // 3. Update V Plane
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, V_txt);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width / 2, frame->height / 2, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);

    // Reset UNPACK_ROW_LENGTH to zero for standard behavior
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void render() {
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_program);

    // Bind all three texture units
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Y_txt);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, U_txt);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, V_txt);

    // Draw the quad
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main() {
    fprintf(stdout, "FFmpeg C++ Video Player\n");

    //Createing a video frame placeholder
    AVFrame* video_frame = nullptr;
    // frame_delay is now only for logging the estimated FPS, not for timing
    double estimated_frame_delay = 0.0;

    // Init FFmpeg and get frame resolution
    int ret = init_ffmpeg("C:\\Users\\meyzat11\\source\\repos\\video-player\\x64\\Debug\\test.mp4", &v_frame_width, &v_frame_height, &estimated_frame_delay, &video_frame);

    //Error handling
    if (ret < 0) {
        fprintf(stderr, "Failed to Init ffmpeg\n");
        return ret;
    }

    fprintf(stdout, "FFmpeg inited successfully\n");

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

    //This function allows opengl to be aware if the windows size was changed by the user
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) { glViewport(0, 0, width, height); });

    //Createing a shader to render the frame using
    shader_program = createShaderProgram(vertexShaderSource, fragmentShaderSource);

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

    // Master Clock: Stores the time when the video started playing relative to its first frame's PTS
    double video_start_time = -1.0;

    // A reasonable threshold to decide if a frame is too old and should be dropped (e.g., 2 frames worth of delay)
    const double MAX_CATCHUP_DELAY = 0.08;

    while (!glfwWindowShouldClose(window)) {
        double frame_pts_seconds = 0.0;
        int ret;

        // --- 1. Frame Retrieval Loop & Frame Dropping ---
        // Continuously decode frames until we get one that isn't too stale.
        while (true) {
            ret = get_next_frame(&video_frame, &frame_pts_seconds);

            if (ret < 0) {
                if (ret != AVERROR_EOF) {
                    fprintf(stderr, "Critical error during decoding. Stopping playback.\n");
                }
                goto cleanup_and_exit;
            }

            double master_clock = glfwGetTime();

            // Initialize the video start time on the very first frame
            if (video_start_time < 0.0) {
                // video_start_time = SystemTime (master_clock) - FramePTS
                // This establishes a sync point: when the first frame (at its PTS) *should* be shown.
                video_start_time = master_clock - frame_pts_seconds;
            }

            // Calculate the intended time this frame *should* appear on screen.
            double intended_render_time = frame_pts_seconds + video_start_time;

            // Calculate how much time is left until the intended render time.
            double time_to_render = intended_render_time - master_clock;

            // If the frame is too late (stale), drop it and continue the loop to get the next one.
            if (time_to_render < -MAX_CATCHUP_DELAY) {
                // The -MAX_CATCHUP_DELAY ensures we drop frames only if we're behind by more than 2 frames worth of time (0.08s).
                fprintf(stderr, "Dropping stale frame (PTS: %.3fs, Clock: %.3fs). Need to catch up.\n", frame_pts_seconds, master_clock);
                continue; // Loop back and decode the next frame immediately.
            }

            // We have a frame that is either early or only slightly late/on time.
            break; // Exit the retrieval loop to process and render this frame.
        }

        // The current frame (video_frame) is the one we will present.

        // --- 2. Synchronization Wait (Blocking Sleep) ---
        // Recalculate time_to_render, as decoding/dropping took time.
        double current_master_clock = glfwGetTime();
        double intended_render_time = frame_pts_seconds + video_start_time;
        double time_to_wait = intended_render_time - current_master_clock;

        if (time_to_wait > 0.0) {
            // Frame is early: sleep for the remainder.
            auto remaining_sleep = std::chrono::duration<double>(time_to_wait);
            std::this_thread::sleep_for(remaining_sleep);
        }

        // --- 3. Render ---
        updateYUVTexturesFromAVFrame(video_frame);
        render();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

cleanup_and_exit:
    // Cleanup
    av_frame_free(&video_frame);
    cleanup_ffmpeg();
    glfwTerminate();
    return 0;
}