#include "core/LinAppleCore.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include <cstdio>
#include <iostream>
#include <getopt.h>
#include <cstring>

void VideoCallback(const uint32_t* pixels, int width, int height, int pitch) {
    (void)pixels; (void)width; (void)height; (void)pitch;
}

void AudioCallback(const int16_t* samples, size_t num_samples) {
    (void)samples; (void)num_samples;
}

void TitleCallback(const char* title) {
    (void)title;
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"test-cpu", required_argument, 0, 't'},
        {"test-6502", no_argument, 0, '1'},
        {"test-65c02", no_argument, 0, '2'},
        {"boot", no_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;
    while ((c = getopt_long(argc, argv, "t:b", long_options, &option_index)) != -1) {
        switch (c) {
            case 't':
                Linapple_CpuTest(optarg);
                return 0;
            case '1':
                g_Apple2Type = A2TYPE_APPLE2PLUS;
                break;
            case '2':
                g_Apple2Type = A2TYPE_APPLE2EENHANCED;
                break;
            case 'b':
                break;
        }
    }

    std::cout << "Starting LinApple Headless Frontend..." << std::endl;

    Linapple_Init();
    Linapple_SetVideoCallback(VideoCallback);
    Linapple_SetAudioCallback(AudioCallback);
    Linapple_SetTitleCallback(TitleCallback);

    g_state.mode = MODE_RUNNING;

    // Simulate 60 frames (1 second of emulation)
    for (int i = 0; i < 60; ++i) {
        Linapple_RunFrame(17030);

        if (i == 10) {
            Linapple_SetKeyState('H', true);
            Linapple_RunFrame(100);
            Linapple_SetKeyState('H', false);
        }
    }

    Linapple_Shutdown();
    std::cout << "Headless execution complete." << std::endl;
    return 0;
}
