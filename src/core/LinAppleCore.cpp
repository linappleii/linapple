#include "LinAppleCore.h"
#include <cstdio>
#include <vector>

static LinappleVideoCallback g_videoCB = nullptr;
static LinappleAudioCallback g_audioCB = nullptr;
static LinappleTitleCallback g_titleCB = nullptr;

void Linapple_SetVideoCallback(LinappleVideoCallback cb) {
    g_videoCB = cb;
}

void Linapple_SetAudioCallback(LinappleAudioCallback cb) {
    g_audioCB = cb;
}

void Linapple_SetTitleCallback(LinappleTitleCallback cb) {
    g_titleCB = cb;
}

// Internal bridge functions
void Linapple_UpdateTitle(const char* title) {
    if (g_titleCB) {
        g_titleCB(title);
    }
}

// For now, these are just stubs that will be implemented in later phases
// or bridge to existing functions.
void Linapple_Init() {
    // SessionInit and others are currently in Applewin.cpp
}

void Linapple_Shutdown() {
    // SessionShutdown is currently in Applewin.cpp
}

uint32_t Linapple_RunFrame(uint32_t cycles) {
    // ContinueExecution is currently in Applewin.cpp
      (void)cycles;
    return 0; 
}
