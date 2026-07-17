#pragma once
// ============================================================================
// test_helpers.h — Bridges for testing internal functions
//
// Includes the shared header and declares internal functions that need testing.
// Source files are compiled separately, so we use extern declarations.
// ============================================================================

#include "binkw32_proxy.h"

// --- Functions under test (from binkw32_proxy.cpp) ---
extern int BppFromFlags(int flags);
extern void TrackVideo(void* h, const char* bikPath, const char* mixName);
extern void UntrackVideo(void* h);
extern VideoInfo* FindVideo(void* h);
extern void ExtractFileName(void* a, DWORD flags, char* out, int outSize);
extern BOOL ExtractNameFromCCFileClass(void* ccFile, char* out, int outSize);

extern VideoInfo g_vids[];
extern int g_vidCount;

// --- Proxy export stubs (callable from tests) ---
extern "C" {
    void __stdcall sBinkSetSoundTrack8(void* a, void* b);
    void __stdcall sBinkSetSoundTrack4(void* a);
    void __stdcall sBinkClose(void* a);
    intptr_t __stdcall sBinkCopyToBuffer(void* a, void* b, void* c, void* d, void* e, void* f, void* g);
    intptr_t __stdcall sBinkOpen(void* a, void* b);
    intptr_t __stdcall sBinkPause(void* a, void* b);
    void __stdcall sBinkSetWillLoop(void* a, void* b);
    intptr_t __stdcall sBinkWait(void* a);
    void __stdcall sBinkGoto(void* a, void* b, void* c);
    void __stdcall sBinkSetVolume2(void* a, void* b);
    void __stdcall sBinkSetSoundOnOff(void* a, void* b);
}

// --- Mock BinkGetSummary callback type ---
typedef void (__stdcall *MockSummaryFn)(void* handle, void* summary);

// --- Mockable Bink function pointers (via BINK_TEST_BUILD) ---
extern void* pBinkGetSummary;
extern void* pBinkOpen;
extern void* pBinkDoFrame;
extern void* pBinkClose;
extern void* pBinkCopyToBuffer;
extern void* pBinkSetVolume;
extern void* pBinkSetPan;
extern void* pBinkGoto;
extern void* pBinkWait;
extern void* pBinkPause;
extern void* pBinkSetSoundOnOff;

// --- Functions under test (from config.cpp) ---
// MixCrc32 and ReadU32/ReadU16 are already accessible via the header.

// --- Helper to create temp files for testing ---
#include <cstdio>
#include <cstring>

struct TempFile {
    char path[MAX_PATH];
    FILE* f;
    TempFile(const char* name) {
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", TEST_DATA_DIR, name);
        f = NULL;
    }
    bool write(const void* data, size_t size) {
        fopen_s(&f, path, "wb");
        if (!f) return false;
        fwrite(data, 1, size, f);
        fclose(f);
        f = NULL;
        return true;
    }
    bool writeText(const char* text) {
        fopen_s(&f, path, "w");
        if (!f) return false;
        fwrite(text, 1, strlen(text), f);
        fclose(f);
        f = NULL;
        return true;
    }
    ~TempFile() {
        if (f) fclose(f);
    }
};
