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

// --- Functions under test (from logging.cpp) ---
extern void RotateLogFile(const char* logPath);
extern void LogLockForTest();
extern void LogUnlockForTest();

// --- Testable globals (from binkw32_proxy.cpp) ---
extern LONG g_initState;

// --- Proxy export stubs (callable from tests) ---
extern "C" {
    void __stdcall sBinkSetSoundTrack8(void* a, void* b);
    void __stdcall sBinkSetSoundTrack4(void* a);
    void __stdcall sYUV_blit_16bpp(void* a, void* b, void* c, void* d, void* e, void* f,
                                   void* g, void* h, void* i, void* j, void* k, void* l);
    void __stdcall sBinkClose(void* a);
    intptr_t __stdcall sBinkCopyToBuffer(void* a, void* b, void* c, void* d, void* e, void* f, void* g);
    intptr_t __stdcall sBinkOpen(void* a, void* b);
    intptr_t __stdcall sBinkOpenWithOptions(void* a, void* b, void* c);
    void __stdcall sBinkDoFrame(void* a);
    intptr_t __stdcall sBinkDoFramePlane(void* a, void* b);
    void __stdcall sBinkNextFrame(void* a);
    intptr_t __stdcall sBinkPause(void* a, void* b);
    void __stdcall sBinkSetWillLoop(void* a, void* b);
    intptr_t __stdcall sBinkWait(void* a);
    intptr_t __stdcall sBinkGoto(void* a, void* b, void* c);
    void __stdcall sBinkSetVolume2(void* a, void* b);
    void __stdcall sBinkSetVolume3(void* a, void* b, void* c);
    void __stdcall sBinkSetSoundOnOff(void* a, void* b);
    void __stdcall sBinkSetPan(void* a, void* b, void* c);
    void __stdcall sBinkSetPan2(void* a, void* b);
    void __stdcall sBinkSetMixBins(void* a, void* b, void* c, void* d);
    void __stdcall sBinkSetMixBins2(void* a, void* b);
}

// --- Mock BinkGetSummary callback type ---
typedef void (__stdcall *MockSummaryFn)(void* handle, void* summary);

// --- Mockable Bink function pointers (via BINK_TEST_BUILD) ---
extern void* pBinkGetSummary;
extern void* pBinkOpen;
extern void* pBinkOpenWithOptions;
extern void* pBinkDoFrame;
extern void* pBinkDoFramePlane;
extern void* pBinkClose;
extern void* pBinkCopyToBuffer;
extern void* pBinkSetVolume;
extern void* pBinkSetPan;
extern void* pBinkSetMixBins;
extern void* pBinkGoto;
extern void* pBinkWait;
extern void* pBinkPause;
extern void* pBinkSetSoundOnOff;
extern void* pBinkSetWillLoop;
extern void* pBinkSetSoundTrack;
extern void* pYUV_blit_16bpp;

// Probed arity of the real exports (bytes): stdcall decoration `_Name@N`.
// Both are written by LoadDll() and read by the forwarding stubs; tests drive
// them directly, since the loader itself is not exercised here.
extern int g_soundTrackArity;
extern int g_yuvArity[YUV_ARITY_COUNT];

// --- Functions under test (from config.cpp) ---
// MixCrc32 and ReadU32/ReadU16 are already accessible via the header.

// --- Helper to create temp files for testing ---
#include <cstdio>
#include <cstring>

// --- Project-relative path resolution ---
// Repo root is resolved at runtime (exe location walk-up), so test binaries
// survive repository relocation — no absolute paths baked at configure time.

inline const char* ProjectRootDir() {
    static char root[MAX_PATH] = {0};
    if (root[0]) return root;

    // 1) Optional env override
    DWORD len = GetEnvironmentVariableA("PROXY_BINK_PROJECT_ROOT", root, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        while (root[0] && root[strlen(root) - 1] == '\\') root[strlen(root) - 1] = '\0';
        if (root[0]) return root;
        root[0] = '\0';
    }

    auto isRoot = [](const char* dir) -> bool {
        char marker[MAX_PATH];
        _snprintf_s(marker, sizeof(marker), _TRUNCATE, "%s\\CMakeLists.txt", dir);
        DWORD a = GetFileAttributesA(marker);
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) return false;
        _snprintf_s(marker, sizeof(marker), _TRUNCATE, "%s\\tests", dir);
        a = GetFileAttributesA(marker);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return false;
        _snprintf_s(marker, sizeof(marker), _TRUNCATE, "%s\\src", dir);
        a = GetFileAttributesA(marker);
        return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
    };

    auto walkUp = [&](char* start) -> bool {
        // strip trailing backslash(es)
        size_t sl = strlen(start);
        while (sl > 0 && start[sl - 1] == '\\') start[--sl] = '\0';
        for (int i = 0; i < 8; i++) {
            if (isRoot(start)) {
                strncpy_s(root, sizeof(root), start, _TRUNCATE);
                return true;
            }
            char* slash = strrchr(start, '\\');
            if (!slash || slash == start + 2) return false; // drive root "C:\"
            *slash = '\0';
        }
        return false;
    };

    // 2) Walk up from exe location
    char base[MAX_PATH];
    len = GetModuleFileNameA(NULL, base, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char* file = strrchr(base, '\\');
        if (file) *file = '\0';
        if (walkUp(base)) return root;
    }

    // 3) Fallback: walk up from CWD
    len = GetCurrentDirectoryA(MAX_PATH, base);
    if (len > 0 && len < MAX_PATH && walkUp(base)) return root;

    // Last resort: CWD itself
    strncpy_s(root, sizeof(root), base, _TRUNCATE);
    return root;
}

inline const char* TestDataDir() {
    static char dataDir[MAX_PATH] = {0};
    if (dataDir[0]) return dataDir;
    _snprintf_s(dataDir, sizeof(dataDir), _TRUNCATE, "%s\\tests", ProjectRootDir());
    CreateDirectoryA(dataDir, NULL);
    size_t n = strlen(dataDir);
    _snprintf_s(dataDir + n, sizeof(dataDir) - n, _TRUNCATE, "\\data");
    CreateDirectoryA(dataDir, NULL);
    return dataDir;
}

#define TEST_DATA_DIR TestDataDir()

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
