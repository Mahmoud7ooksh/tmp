extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/channel_layout.h>
}

#include <portaudio.h>
#include <GLFW/glfw3.h>
#include <iostream>

void initializeGLFW(int width, int height) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        exit(EXIT_FAILURE);
    }
    GLFWwindow* window = glfwCreateWindow(width, height, "Lethal Sharp", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
}

void renderFrame(GLFWwindow* window, AVFrame* frame, int width, int height) {
    glClear(GL_COLOR_BUFFER_BIT);
    glRasterPos2i(-1, 1);
    glPixelZoom(1.0f, -1.0f);
    glDrawPixels(width, height, GL_RGB, GL_UNSIGNED_BYTE, frame->data[0]);
    glfwSwapBuffers(window);
    glfwPollEvents();
}

typedef struct {
    float* buffer;
    unsigned long remainingSamples;
} AudioData;

static int patestCallback(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {


    AudioData* audioData = (AudioData*)userData;
    float* out = (float*)outputBuffer;

    if (audioData->remainingSamples > 0) {
        unsigned long samplesToCopy = framesPerBuffer;

        if (audioData->remainingSamples < framesPerBuffer) {
            samplesToCopy = audioData->remainingSamples;
        }

        memcpy(out, audioData->buffer, samplesToCopy * sizeof(float));

        out += samplesToCopy;
        audioData->buffer += samplesToCopy;
        audioData->remainingSamples -= samplesToCopy;

        if (audioData->remainingSamples == 0) {
            return paComplete;
        }

        return paContinue;
    }
    else {
        return paComplete;
    }
}

int main() {

    const char* filePath = "F:\\FFOutput\\Malky - Dork.mp4";


    AVFormatContext* formatCtx = avformat_alloc_context();
    if (avformat_open_input(&formatCtx, filePath, NULL, NULL) != 0) {
        std::cerr << "Failed to open video file\n";
        return -1;
    }
    if (avformat_find_stream_info(formatCtx, NULL) < 0) {
        std::cerr << "Failed to retrieve stream info\n";
        return -1;
    }

    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
        } 
        else if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
        }
    }
    if (videoStreamIndex == -1) {
        std::cerr << "No video stream found\n";
        return -1;
    }
    if (audioStreamIndex == -1) {
        std::cerr << "No audio stream found\n";
        return -1;
    }

    AVCodecParameters* videoCodecParams = formatCtx->streams[videoStreamIndex]->codecpar;
    const AVCodec* videoCodec = avcodec_find_decoder(videoCodecParams->codec_id);
    if (!videoCodec) {
        std::cerr << "Unsupported video codec\n";
        return -1;
    }

    AVCodecParameters* audioCodecParams = formatCtx->streams[audioStreamIndex]->codecpar;
    const AVCodec* audioCodec = avcodec_find_decoder(audioCodecParams->codec_id);
    if (!audioCodec) {
        std::cerr << "Unsupported audio codec\n";
        return -1;
    }

    AVCodecContext* videoCodecCtx = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecCtx, videoCodecParams);
    if (avcodec_open2(videoCodecCtx, videoCodec, NULL) < 0) {
        std::cerr << "Failed to open video codec\n";
        return -1;
    }

    AVCodecContext* audioCodecCtx = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecCtx, audioCodecParams);
    if (avcodec_open2(audioCodecCtx, audioCodec, NULL) < 0) {
        std::cerr << "Failed to open audio codec\n";
        return -1;
    }


    int width = videoCodecParams->width;
    int height = videoCodecParams->height;
    initializeGLFW(width/2, height/2);
    GLFWwindow* window = glfwGetCurrentContext();

    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();


    SwsContext* swsCtx = sws_getContext(
        width, height, videoCodecCtx->pix_fmt,
        width/2, height/2, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL
    );


    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* swr_ctx = swr_alloc();
    swr_alloc_set_opts2(&swr_ctx,
        &stereo,
        AV_SAMPLE_FMT_FLT,
        48000,
        &audioCodecParams->ch_layout,
        (AVSampleFormat)audioCodecParams->format,
        audioCodecParams->sample_rate,
        0, NULL);
    swr_init(swr_ctx);

    int rgbFrameSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
    uint8_t* rgbBuffer = (uint8_t*)av_malloc(rgbFrameSize * sizeof(uint8_t));
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer, AV_PIX_FMT_RGB24, width, height, 1);

    PaError paErr;


    paErr = Pa_Initialize();
    if (paErr != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(paErr));
        return -1;
    }

    PaStream* audioStream;

    AudioData audioData = { 0 };

    paErr = Pa_OpenDefaultStream(&audioStream,
        0,                             
        2,                             
        paFloat32,                      
        48000,  
        512,                            
        patestCallback,                 
        &audioData);

    if (paErr != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(paErr));
        Pa_Terminate();
        return -1;
    }

    paErr = Pa_StartStream(audioStream);
    if (paErr != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(paErr));
        Pa_Terminate();
        return -1;
    }

    uint8_t* outputBuffer = NULL;
    int outputLinesize = 0;

    
    int i = 0, j = 0;
    while (av_read_frame(formatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(videoCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(videoCodecCtx, frame) == 0) {
                    sws_scale(swsCtx, frame->data, frame->linesize, 0, height, rgbFrame->data, rgbFrame->linesize);
                    renderFrame(window, rgbFrame, width, height);

                    if (glfwWindowShouldClose(window))
                        break;
                }
            }
        }
        else if (packet->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(audioCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(audioCodecCtx, frame) == 0) {
                    int outputSamples = (int)av_rescale_rnd(
                        frame->nb_samples, 480000, audioCodecParams->sample_rate, AV_ROUND_UP);

                    av_samples_alloc(&outputBuffer, &outputLinesize, 2, outputSamples,
                        AV_SAMPLE_FMT_FLT, 0);

                    int converted_samples = swr_convert(
                        swr_ctx,
                        &outputBuffer,
                        outputSamples,
                        (const uint8_t**)frame->extended_data,
                        frame->nb_samples);

                    audioData.buffer = (float*)outputBuffer;
                    audioData.remainingSamples = converted_samples * 2;

                    if (glfwWindowShouldClose(window))
                        break;
                }
            }
        }

        av_packet_unref(packet);
        if (glfwWindowShouldClose(window))
            break;
    }

    Pa_StopStream(audioStream);
    Pa_CloseStream(audioStream);
    Pa_Terminate();
    av_free(rgbBuffer);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    sws_freeContext(swsCtx);
    avcodec_free_context(&videoCodecCtx);
    avformat_close_input(&formatCtx);
    glfwTerminate();

    return 0;
}
