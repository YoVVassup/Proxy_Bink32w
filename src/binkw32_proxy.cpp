#include "binkw32_proxy.h"
#include <stdlib.h>

// ============================================================================
// binkw32_proxy.cpp — DLL loader, video tracking, and proxy exports
//
// This is the main module that implements the binkw32.dll proxy. It:
// 1. Loads the real Bink DLL by ordinal at DllMain time
// 2. Exports all 107 Bink API functions as passthrough stubs
// 3. Intercepts BinkOpen/BinkClose for video tracking and audio replacement
// 4. Intercepts BinkCopyToBuffer for aspect-ratio fit scaling
// 5. Intercepts BinkSetVolume/Pan for audio muting during replacement
// 6. Provides call stack logging via CaptureStackBackTrace
//
// Calling convention adapters:
//   BinkSetVolume: game imports @8 (2 args), real DLL may have @8 or @12
//   BinkSetPan: game imports @12 (3 args), real DLL may have @8 or @12
//   These are handled via BINK_HAS_VOLUME_12 / BINK_HAS_PAN_12 macros.
// ============================================================================

// ============================================================================
// Proxy_Bink32w v2.0.0 — Bink Video API Proxy DLL
//
// Drop-in binkw32.dll replacement that intercepts Bink video API calls.
// Features: audio replacement (.bik -> .wav), .mix archive parsing,
//           aspect-ratio fit scaling, call stack logging.
// ============================================================================

// Handle to the real Bink DLL loaded at runtime
static HMODULE g_hR = NULL;
static LONG g_initState = 0;

static BOOL LoadDll();

// Deferred initialization — called from sBinkOpen/sBinkOpenWithOptions
// instead of DllMain to avoid LoadLibrary + I/O under loader lock.
#ifdef BINK_TEST_BUILD
void EnsureInitialized();
#else
static void EnsureInitialized();
#endif

void EnsureInitialized() {
    LONG prev = InterlockedCompareExchange(&g_initState, 1, 0);
    if (prev == 0) {
        LoadAudioConfig();
        if (LoadDll()) {
            InterlockedExchange(&g_initState, 2);
        } else {
            InterlockedExchange(&g_initState, 0);
        }
    } else {
        while (g_initState != 2) {
            if (g_initState == 0) {
                if (InterlockedCompareExchange(&g_initState, 1, 0) == 0) {
                    LoadAudioConfig();
                    if (LoadDll()) {
                        InterlockedExchange(&g_initState, 2);
                    } else {
                        InterlockedExchange(&g_initState, 0);
                    }
                    return;
                }
            }
            SwitchToThread();
        }
    }
}

// Function pointers to real Bink DLL functions, resolved by ordinal at load time
#ifdef BINK_TEST_BUILD
#define D(n) void* p##n = NULL;
#else
#define D(n) static void* p##n = NULL;
#endif
D(BinkLogoAddress) D(BinkSetError) D(BinkGetError) D(BinkOpen)
D(BinkOpenWithOptions) D(BinkDoFrame) D(BinkDoFramePlane) D(BinkNextFrame)
D(BinkWait) D(BinkClose) D(BinkPause) D(BinkCopyToBuffer)
D(BinkCopyToBufferRect) D(BinkGetRects) D(BinkGoto) D(BinkGetKeyFrame)
D(BinkFreeGlobals) D(BinkGetPlatformInfo)
D(BinkGetFrameBuffersInfo) D(BinkRegisterFrameBuffers)
D(BinkSetVideoOnOff) D(BinkSetSoundOnOff)
D(BinkSetVolume) D(BinkSetPan) D(BinkSetSpeakerVolumes)
D(BinkService) D(BinkShouldSkip) D(BinkGetPalette)
D(BinkControlBackgroundIO) D(BinkControlPlatformFeatures)
D(BinkSetWillLoop) D(BinkOpenTrack) D(BinkCloseTrack)
D(BinkGetTrackData) D(BinkGetTrackType)
D(BinkGetTrackMaxSize) D(BinkGetTrackID)
D(BinkGetSummary) D(BinkGetRealtime)
D(BinkSetFileOffset) D(BinkSetSoundTrack8)
D(BinkSetIO) D(BinkSetFrameRate) D(BinkSetSimulate)
D(BinkSetIOSize) D(BinkSetSoundSystem) D(BinkSetMemory)
D(BinkOpenDirectSound) D(BinkOpenWaveOut) D(BinkOpenMiles)
D(BinkDX8SurfaceType) D(BinkDX9SurfaceType)
D(BinkBufferOpen) D(BinkBufferSetHWND)
D(BinkDDSurfaceType) D(BinkIsSoftwareCursor)
D(BinkCheckCursor) D(BinkBufferSetDirectDraw)
D(BinkBufferClose) D(BinkBufferLock) D(BinkBufferUnlock)
D(BinkBufferSetResolution) D(BinkBufferCheckWinPos)
D(BinkBufferSetOffset) D(BinkBufferBlit) D(BinkBufferSetScale)
D(BinkBufferGetDescription) D(BinkBufferGetError) D(BinkBufferClear)
D(BinkRestoreCursor) D(BinkStartAsyncThread)
D(BinkDoFrameAsync) D(BinkDoFrameAsyncWait)
D(BinkRequestStopAsyncThread) D(BinkWaitStopAsyncThread)
D(BinkSetMixBins) D(BinkSetMixBinVolumes)
D(ExpandBink) D(ExpandBundleSizes) D(RADSetMemory) D(RADTimerRead)
D(radmalloc) D(radfree)
D(YUV_init)
D(YUV_blit_16a1bpp) D(YUV_blit_16a1bpp_mask)
D(YUV_blit_16a4bpp) D(YUV_blit_16a4bpp_mask)
D(YUV_blit_16bpp) D(YUV_blit_16bpp_mask)
D(YUV_blit_24bpp) D(YUV_blit_24bpp_mask)
D(YUV_blit_24rbpp) D(YUV_blit_24rbpp_mask)
D(YUV_blit_32abpp) D(YUV_blit_32abpp_mask)
D(YUV_blit_32bpp) D(YUV_blit_32bpp_mask)
D(YUV_blit_32rabpp) D(YUV_blit_32rabpp_mask)
D(YUV_blit_32rbpp) D(YUV_blit_32rbpp_mask)
D(YUV_blit_UYVY) D(YUV_blit_UYVY_mask)
D(YUV_blit_YUY2) D(YUV_blit_YUY2_mask)
D(YUV_blit_YV12)
D(BinkOpenXAudio2) D(BinkServiceSound)
D(BinkUseTelemetry) D(BinkUseTmLite)
D(BinkSetSoundTrack) D(BinkDoFrameAsyncMulti)
D(BinkRequestStopAsyncThreadsMulti) D(BinkWaitStopAsyncThreadsMulti)
D(BinkAllocateFrameBuffers) D(BinkGetGPUDataBuffersInfo)
D(BinkRegisterGPUDataBuffers) D(BinkSetOSFileCallbacks)
D(BinkSetLowLevelFileCallbacks) D(BinkSetSoundSystem2)
D(BinkUtilCPUs) D(BinkUtilFree) D(BinkUtilMalloc)
D(BinkUtilMutexCreate) D(BinkUtilMutexDestroy)
D(BinkUtilMutexLock) D(BinkUtilMutexLockTimeOut) D(BinkUtilMutexUnlock)
#undef D

// ============================================================================
// DLL loader — resolves real Bink DLL functions by ordinal
//
// Ordinal tables are auto-generated from dumpbin exports.
// Run: tools/generate_ordinals.ps1 to regenerate.
// See tools/ordinals_map.json for version→group mapping.
//
// Excluded versions (in tools/generate_ordinals.ps1 skipVersions):
//   0.5a-0.9n: Too old, crashes internally
//   1.0c-1.0f: BinkOpen returns NULL
//   1.2h: Crashes after BinkSetSoundSystem
//   1.8r: BinkMake/BinkMix tool
//   1.99a-1.99w, 1.9y-1.9z, 2.1c: Pre-release, crashes after BinkOpen
//   2.4i, 2.7g: Bink 2.x, different implementation
// ============================================================================

struct OrdinalEntry {
    int ordinal;
    void** dest;
};

#define OE(func, ord) { ord, &p##func }

#include "ordinals.inc"

#undef OE

// Select ordinal table based on BINK_GROUP define (set by CMake)
#if defined(BINK_GROUP_1)
#define BINK_ORDINAL_TABLE g_ordinals_group1
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group1)/sizeof(g_ordinals_group1[0]))
#elif defined(BINK_GROUP_2)
#define BINK_ORDINAL_TABLE g_ordinals_group2
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group2)/sizeof(g_ordinals_group2[0]))
#elif defined(BINK_GROUP_3)
#define BINK_ORDINAL_TABLE g_ordinals_group3
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group3)/sizeof(g_ordinals_group3[0]))
#elif defined(BINK_GROUP_4)
#define BINK_ORDINAL_TABLE g_ordinals_group4
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group4)/sizeof(g_ordinals_group4[0]))
#elif defined(BINK_GROUP_5)
#define BINK_ORDINAL_TABLE g_ordinals_group5
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group5)/sizeof(g_ordinals_group5[0]))
#elif defined(BINK_GROUP_6)
#define BINK_ORDINAL_TABLE g_ordinals_group6
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group6)/sizeof(g_ordinals_group6[0]))
#elif defined(BINK_GROUP_7)
#define BINK_ORDINAL_TABLE g_ordinals_group7
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group7)/sizeof(g_ordinals_group7[0]))
#elif defined(BINK_GROUP_8)
#define BINK_ORDINAL_TABLE g_ordinals_group8
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group8)/sizeof(g_ordinals_group8[0]))
#elif defined(BINK_GROUP_9)
#define BINK_ORDINAL_TABLE g_ordinals_group9
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group9)/sizeof(g_ordinals_group9[0]))
#elif defined(BINK_GROUP_10)
#define BINK_ORDINAL_TABLE g_ordinals_group10
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group10)/sizeof(g_ordinals_group10[0]))
#elif defined(BINK_GROUP_11)
#define BINK_ORDINAL_TABLE g_ordinals_group11
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group11)/sizeof(g_ordinals_group11[0]))
#elif defined(BINK_GROUP_12)
#define BINK_ORDINAL_TABLE g_ordinals_group12
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group12)/sizeof(g_ordinals_group12[0]))
#elif defined(BINK_GROUP_13)
#define BINK_ORDINAL_TABLE g_ordinals_group13
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group13)/sizeof(g_ordinals_group13[0]))
#elif defined(BINK_GROUP_14)
#define BINK_ORDINAL_TABLE g_ordinals_group14
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group14)/sizeof(g_ordinals_group14[0]))
#elif defined(BINK_GROUP_15)
#define BINK_ORDINAL_TABLE g_ordinals_group15
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group15)/sizeof(g_ordinals_group15[0]))
#elif defined(BINK_GROUP_16)
#define BINK_ORDINAL_TABLE g_ordinals_group16
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group16)/sizeof(g_ordinals_group16[0]))
#elif defined(BINK_GROUP_17)
#define BINK_ORDINAL_TABLE g_ordinals_group17
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group17)/sizeof(g_ordinals_group17[0]))
#elif defined(BINK_GROUP_18)
#define BINK_ORDINAL_TABLE g_ordinals_group18
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group18)/sizeof(g_ordinals_group18[0]))
#elif defined(BINK_GROUP_19)
#define BINK_ORDINAL_TABLE g_ordinals_group19
#define BINK_ORDINAL_COUNT (sizeof(g_ordinals_group19)/sizeof(g_ordinals_group19[0]))
#else
#error "No BINK_GROUP_N defined. Set -DBINK_GROUP_N in CMake."
#endif

// Real DLL name derived from group — set by CMake via BINK_REAL_DLL define
#ifndef BINK_REAL_DLL
#define BINK_REAL_DLL "binkw32.dll"
#endif

static BOOL LoadDll() {
    if (g_hR) return TRUE;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (!slash) return FALSE;
    *(slash + 1) = 0;

    char dllPath[MAX_PATH];
    _snprintf_s(dllPath, sizeof(dllPath), _TRUNCATE, "%s" BINK_REAL_DLL, exePath);

    g_hR = LoadLibraryA(dllPath);
    if (!g_hR) {
        LogF("FAILED to load real DLL: %s (error %lu)", dllPath, GetLastError());
        return FALSE;
    }
    LogF("Real DLL loaded: %s", dllPath);

    for (int i = 0; i < (int)BINK_ORDINAL_COUNT; i++) {
        *BINK_ORDINAL_TABLE[i].dest = (void*)GetProcAddress(g_hR, (LPCSTR)BINK_ORDINAL_TABLE[i].ordinal);
    }

    LogF("Proxied functions resolved: pBinkOpen=%p pBinkDoFrame=%p pBinkClose=%p pBinkWait=%p",
         pBinkOpen, pBinkDoFrame, pBinkClose, pBinkWait);
    return TRUE;
}

// ============================================================================
// Video handle tracking + audio replacement trigger
// ============================================================================

VideoInfo g_vids[MAX_TRACKED];
int g_vidCount = 0;

void TrackVideo(void* h, const char* bikPath, const char* mixName) {
    if (!h || !pBinkGetSummary) return;
    unsigned char summary[1024];
    memset(summary, 0, sizeof(summary));
    ((void(__stdcall*)(void*, void*))pBinkGetSummary)(h, summary);
    uint32_t w = ReadU32(summary);
    uint32_t hv = ReadU32(summary + 4);
    uint32_t frameRate = ReadU32(summary + 20);
    uint32_t frameRateDiv = ReadU32(summary + 24);
    if (w > 0 && hv > 0 && g_vidCount < MAX_TRACKED) {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].handle == h) {
                g_vids[i].width = w;
                g_vids[i].height = hv;
                LogF("Updated video: %p %ux%u", h, w, hv);
                return;
            }
        }
        g_vids[g_vidCount].handle = h;
        g_vids[g_vidCount].width = w;
        g_vids[g_vidCount].height = hv;
        g_vids[g_vidCount].tempBuf = 0;
        g_vids[g_vidCount].tempPitch = 0;
        g_vids[g_vidCount].tempHeight = 0;
        g_vids[g_vidCount].scaleLookupX = NULL;
        g_vids[g_vidCount].scaleLookupY = NULL;
        g_vids[g_vidCount].scaleTableW = 0;
        g_vids[g_vidCount].scaleTableH = 0;
        g_vids[g_vidCount].wavPath[0] = '\0';
        g_vids[g_vidCount].wavPlayer = NULL;

        BinkFileInfo bfi = {0};
        bfi.width = w;
        bfi.height = hv;
        bfi.frameRate = frameRate;
        bfi.frameRateDiv = frameRateDiv;
        bfi.valid = TRUE;

        const char* wav = FindWavForBik(bikPath, mixName);
        if (wav) {
            strncpy_s(g_vids[g_vidCount].wavPath, sizeof(g_vids[g_vidCount].wavPath), wav, _TRUNCATE);
            LogF("Audio replacement: %s [%ux%u] -> %s", bikPath ? bikPath : "?", w, hv, wav);

            WavPlayer* pl = AllocPlayer();
            if (pl && WavPlayerStart(pl, wav)) {
                g_vids[g_vidCount].wavPlayer = pl;
            } else {
                if (pl) FreePlayer(pl);
                LogF("Failed to start WAV playback for %s", bikPath ? bikPath : "?");
            }
        } else {
            LogF("No audio mapping for: %s [%ux%u]", bikPath ? bikPath : "?", w, hv);
        }

        g_vidCount++;
        LogF("Tracked video: %p %ux%u", h, w, hv);
    }
}

void UntrackVideo(void* h) {
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
            if (g_vids[i].tempBuf) VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE);
            if (g_vids[i].scaleLookupX) free(g_vids[i].scaleLookupX);
            if (g_vids[i].scaleLookupY) free(g_vids[i].scaleLookupY);
            memmove(&g_vids[i], &g_vids[i + 1], (g_vidCount - i - 1) * sizeof(VideoInfo));
            g_vidCount--;
            memset(&g_vids[g_vidCount], 0, sizeof(VideoInfo));
            return;
        }
    }
}

VideoInfo* FindVideo(void* h) {
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) return &g_vids[i];
    }
    return NULL;
}

// ============================================================================
// Helpers for proxy exports
// ============================================================================

// Extract bits-per-pixel from BinkCopyToBuffer flags.
// RA2/RA2YR always use bpp=2 (RGB565, flags & 7 <= 4).
// bpp=3 (RGB888) and bpp=4 (RGB888+alpha) exist in other Bink versions but not used by RA2.
int BppFromFlags(int flags) {
    int st = flags & 7;
    if (st == 0) return 3;
    if (st <= 4) return 2;
    return 4;
}

#ifdef BINK_TEST_BUILD
void ExtractFileName(void* a, DWORD flags, char* out, int outSize) {
#else
static void ExtractFileName(void* a, DWORD flags, char* out, int outSize) {
#endif
    out[0] = '\0';

    if (flags & 0x00800000) {
        HANDLE hFile = (HANDLE)(intptr_t)a;
        char pathBuf[MAX_PATH];
        DWORD len = GetFinalPathNameByHandleA(hFile, pathBuf, MAX_PATH, FILE_NAME_NORMALIZED);
        if (len > 0 && len < MAX_PATH) {
            const char* p = pathBuf;
            if (memcmp(p, "\\\\?\\", 4) == 0) p += 4;
            const char* slash = strrchr(p, '\\');
            if (slash) strncpy_s(out, outSize, slash + 1, _TRUNCATE);
            else strncpy_s(out, outSize, p, _TRUNCATE);
        }
        return;
    }

    if (flags & 0x04000000) {
        return;
    }

    if (a) {
        strncpy_s(out, outSize, (const char*)a, _TRUNCATE);
    }
}

void LogCallStack(int skip) {
    void* stack[8];
    USHORT frames = CaptureStackBackTrace(skip, 8, stack, NULL);
    if (frames == 0) return;
    HMODULE hMod = NULL;
    char buf[1024] = "";
    int pos = 0;
    for (USHORT i = 0; i < frames && pos < (int)sizeof(buf) - 80; i++) {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)stack[i], &hMod);
        char modName[MAX_PATH] = "?";
        if (hMod) GetModuleFileNameA(hMod, modName, MAX_PATH);
        const char* slash = strrchr(modName, '\\');
        DWORD rva = (DWORD)((char*)stack[i] - (char*)hMod);
        int written = _snprintf_s(buf + pos, sizeof(buf) - pos, _TRUNCATE,
                           "  -> %s+0x%X", slash ? slash + 1 : modName, rva);
        if (written < 0) break;
        pos += written;
    }
    LogF("Call stack:%s", buf);
}

// ============================================================================
// DLL entry point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(h);
        {
            char dllPath[MAX_PATH];
            DWORD len = GetModuleFileNameA(h, dllPath, MAX_PATH);
            if (len == 0 || len >= MAX_PATH) {
                g_dllDir[0] = '\0';
            } else {
                char* slash = strrchr(dllPath, '\\');
                if (slash) { *(slash + 1) = 0; lstrcpynA(g_dllDir, dllPath, MAX_PATH); }
                else g_dllDir[0] = '\0';
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        if (reserved != NULL) break;
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                g_vids[i].wavPlayer->hWave = NULL;
                g_vids[i].wavPlayer->playing = FALSE;
                g_vids[i].wavPlayer = NULL;
            }
            if (g_vids[i].tempBuf) VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE);
            if (g_vids[i].scaleLookupX) free(g_vids[i].scaleLookupX);
            if (g_vids[i].scaleLookupY) free(g_vids[i].scaleLookupY);
        }
        g_vidCount = 0;
        for (int i = 0; i < g_playerCount; i++) {
            g_players[i].hWave = NULL;
            g_players[i].playing = FALSE;
            for (int j = 0; j < 4; j++) {
                if (g_players[i].buffers[j]) {
                    VirtualFree(g_players[i].buffers[j], 0, MEM_RELEASE);
                    g_players[i].buffers[j] = NULL;
                }
            }
            if (g_players[i].pcmData) {
                VirtualFree(g_players[i].pcmData, 0, MEM_RELEASE);
                g_players[i].pcmData = NULL;
            }
            if (g_players[i].csValid) {
                DeleteCriticalSection(&g_players[i].cs);
                g_players[i].csValid = FALSE;
            }
        }
        g_playerCount = 0;
        g_mixCacheCount = 0;
        if (g_hR) { FreeLibrary(g_hR); g_hR = NULL; }
        ShutdownLog();
        break;
    }
    return TRUE;
}

// ============================================================================
// Proxy exports — one stub per Bink API function
// ============================================================================

extern "C" {

intptr_t __stdcall sBinkLogoAddress() {
    void* p = pBinkLogoAddress;
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkLogoAddress->%p", (void*)r);
    return r;
}

void __stdcall sBinkSetError(void* a) {
    void* p = pBinkSetError;
    LogF("BinkSetError(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetError() {
    void* p = pBinkGetError;
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkGetError()=%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkOpen(void* a, void* b) {
    EnsureInitialized();
    void* p = pBinkOpen;
    DWORD flags = (DWORD)(intptr_t)b;
    LogF("BinkOpen(%p,%p) flags=0x%08X ptr=%p", a, b, flags, p);

    char extractedName[MAX_PATH] = "";
    char mixFileName[MAX_PATH] = "";
    char* bikName = extractedName;

    if (flags & 0x00800000) {
        HANDLE hFile = (HANDLE)(intptr_t)a;
        DWORD pos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
        BinkFileInfo bfi = ReadBinkHeaderFromFile(hFile);
        if (bfi.valid) {
            LogF("Bink header: %ux%u, %u frames, %u/%u fps (file pos=%u)",
                 bfi.width, bfi.height, bfi.frameCount,
                 bfi.frameRate, bfi.frameRateDiv, pos);
        } else {
            LogF("No Bink header at current pos=%u", pos);
        }

        char mixPath[MAX_PATH] = "";
        ExtractFileName(a, flags, mixPath, sizeof(mixPath));
        if (mixPath[0]) {
            LogF("BinkOpen file: %s", mixPath);
            strncpy_s(mixFileName, sizeof(mixFileName), mixPath, _TRUNCATE);
            char fullMixPath[MAX_PATH];
            _snprintf_s(fullMixPath, sizeof(fullMixPath), _TRUNCATE, "%s%s", g_dllDir, mixPath);

            char bikInternal[MAX_PATH] = "";
            if (FindBikNameInMix(fullMixPath, pos, bikInternal, sizeof(bikInternal))) {
                LogF("Bik name from .mix: %s", bikInternal);
                strncpy_s(extractedName, sizeof(extractedName), bikInternal, _TRUNCATE);
                bikName = extractedName;
            } else {
                bikName = mixPath;
            }
        }

        LogCallStack(1);
    } else if (a && !(flags & 0x04000000)) {
        bikName = (char*)a;
    }

    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkOpen->%p", (void*)r);
    if (r) {
        if (bikName[0]) LogF("BinkOpen resolved: %s", bikName);
        TrackVideo((void*)r, bikName[0] ? bikName : NULL, mixFileName[0] ? mixFileName : NULL);
    }
    return r;
}

intptr_t __stdcall sBinkOpenWithOptions(void* a, void* b, void* c) {
    EnsureInitialized();
    void* p = pBinkOpenWithOptions;
    LogF("BinkOpenWithOptions(%p,%p,%p)", a, b, c);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
    LogF("BinkOpenWithOptions->%p", (void*)r);
    if (r && a) {
        const char* name = (const char*)a;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(a, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
            (mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            TrackVideo((void*)r, name[0] ? name : NULL, NULL);
        } else {
            TrackVideo((void*)r, NULL, NULL);
        }
    }
    return r;
}

void __stdcall sBinkDoFrame(void* a) {
    void* p = pBinkDoFrame;
    if (g_logWait) LogF("BinkDoFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDoFramePlane(void* a, void* b) {
    void* p = pBinkDoFramePlane;
    if (g_logWait) LogF("BinkDoFramePlane(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    if (g_logWait) LogF("BinkDoFramePlane->%p", (void*)r);
    return r;
}

void __stdcall sBinkNextFrame(void* a) {
    void* p = pBinkNextFrame;
    if (g_logWait) LogF("BinkNextFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkWait(void* a) {
    void* p = pBinkWait;
    if (g_logWait) LogF("BinkWait(%p)", a);
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

void __stdcall sBinkClose(void* a) {
    void* p = pBinkClose;
    LogF("BinkClose(%p)", a);
    UntrackVideo(a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkPause(void* a, void* b) {
    void* p = pBinkPause;
    int pause = (int)(intptr_t)b;
    LogF("BinkPause(%p, %d)", a, pause);
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        if (pause) WavPlayerPause(vi->wavPlayer);
        else WavPlayerResume(vi->wavPlayer);
    }
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkCopyToBuffer(void* a, void* b, void* c, void* d, void* e, void* f, void* g) {
    void* p = pBinkCopyToBuffer;
    if (!p) return 0;

    VideoInfo* vi = FindVideo(a);
    if (vi) {
        int dstPitch = (int)(intptr_t)c;
        int dstHeight = (int)(intptr_t)d;
        int destX = (int)(intptr_t)e;
        int destY = (int)(intptr_t)f;
        int flags = (int)(intptr_t)g;
        int bpp = BppFromFlags(flags);

        // RA2/RA2YR only uses bpp=2 (RGB565). Skip scaling for other modes.
        if (bpp == 2 && dstPitch > 0 && dstHeight > 0 && destX >= 0 && destY >= 0) {
            int dstW = dstPitch / bpp;
            int needScale = (vi->width > (uint32_t)dstW || vi->height > (uint32_t)dstHeight);

            if (needScale) {
                uint32_t srcW = vi->width;
                uint32_t srcHH = vi->height;

                int srcPitch = srcW * bpp;
                srcPitch = (srcPitch + 15) & ~15;
                int srcH = srcHH;
                SIZE_T requiredSize = (SIZE_T)srcPitch * srcH;

                if (!vi->tempBuf || vi->tempPitch != srcPitch || vi->tempHeight != srcH) {
                    if (vi->tempBuf) VirtualFree(vi->tempBuf, 0, MEM_RELEASE);
                    vi->tempBuf = VirtualAlloc(0, requiredSize, MEM_COMMIT, PAGE_READWRITE);
                    if (vi->tempBuf) {
                        vi->tempPitch = srcPitch;
                        vi->tempHeight = srcH;
                    } else {
                        LogF("VirtualAlloc failed: %zu bytes (error %lu)", requiredSize, GetLastError());
                        vi->tempPitch = 0;
                        vi->tempHeight = 0;
                    }
                }

                if (vi->tempBuf) {
                    int availW = dstW - destX;
                    int availH = dstHeight - destY;

                    int scaleW = availW;
                    int scaleH = (int)((uint64_t)srcHH * availW / srcW);
                    if (scaleH > availH) {
                        scaleH = availH;
                        scaleW = (int)((uint64_t)srcW * availH / srcHH);
                    }
                    if (scaleW < 1) scaleW = 1;
                    if (scaleH < 1) scaleH = 1;

                    int offX = destX + (availW - scaleW) / 2;
                    int offY = destY + (availH - scaleH) / 2;

                    if (offX < 0) { scaleW += offX; offX = 0; }
                    if (offY < 0) { scaleH += offY; offY = 0; }
                    if (offX + scaleW > dstW) scaleW = dstW - offX;
                    if (offY + scaleH > dstHeight) scaleH = dstHeight - offY;
                    if (scaleW < 1 || scaleH < 1) {
                        return ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(
                            a, vi->tempBuf, (void*)(intptr_t)srcPitch,
                            (void*)(intptr_t)srcH, (void*)0, (void*)0, g);
                    }

                    if (vi->scaleTableW != scaleW || vi->scaleTableH != scaleH) {
                        int* newX = (int*)malloc(scaleW * sizeof(int));
                        int* newY = (int*)malloc(scaleH * sizeof(int));
                        if (newX && newY) {
                            free(vi->scaleLookupX);
                            free(vi->scaleLookupY);
                            vi->scaleLookupX = newX;
                            vi->scaleLookupY = newY;
                            for (int x = 0; x < scaleW; x++)
                                vi->scaleLookupX[x] = x * (int)srcW / scaleW;
                            for (int y = 0; y < scaleH; y++)
                                vi->scaleLookupY[y] = y * (int)srcHH / scaleH;
                            vi->scaleTableW = scaleW;
                            vi->scaleTableH = scaleH;
                            LogF("Scaling %ux%u -> %dx%d (fit in %dx%d) at (%d,%d)",
                                 (unsigned)srcW, (unsigned)srcHH, scaleW, scaleH, availW, availH, offX, offY);
                        } else {
                            LogF("Failed to allocate scale table: %dx%d + %dx%d bytes", scaleW, scaleH, scaleW, scaleH);
                            free(newX);
                            free(newY);
                        }
                    }

                    intptr_t result = ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(
                        a, vi->tempBuf, (void*)(intptr_t)srcPitch,
                        (void*)(intptr_t)srcH, (void*)0, (void*)0, g);

                    if (vi->scaleLookupX && vi->scaleLookupY) {
                        for (int y = 0; y < scaleH; y++) {
                            int sy = vi->scaleLookupY[y];
                            const uint16_t* srcLine = (const uint16_t*)((const uint8_t*)vi->tempBuf + (SIZE_T)sy * srcPitch);
                            uint16_t* dstLine = (uint16_t*)((uint8_t*)b + (SIZE_T)(offY + y) * dstPitch + (SIZE_T)offX * bpp);
                            for (int x = 0; x < scaleW; x++) {
                                dstLine[x] = srcLine[vi->scaleLookupX[x]];
                            }
                        }
                    }

                    return result;
                }
            }
        }
    }

    return ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g);
}

intptr_t __stdcall sBinkCopyToBufferRect(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k) {
    void* p = pBinkCopyToBufferRect;
    LogF("BinkCopyToBufferRect(%p,...)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k) : 0;
    LogF("BinkCopyToBufferRect->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetRects(void* a, void* b) {
    void* p = pBinkGetRects;
    LogF("BinkGetRects(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetRects->%p", (void*)r);
    return r;
}

void __stdcall sBinkGoto(void* a, void* b, void* c) {
    void* p = pBinkGoto;
    uint32_t frame = (uint32_t)(intptr_t)b;
    LogF("BinkGoto(%p, %u)", a, frame);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer && vi->height > 0) {
        if (pBinkGetSummary) {
            unsigned char summary[1024];
            memset(summary, 0, sizeof(summary));
            ((void(__stdcall*)(void*, void*))pBinkGetSummary)(a, summary);
            uint32_t fr = ReadU32(summary + 20);
            uint32_t frd = ReadU32(summary + 24);
            if (fr > 0 && frd > 0) {
                uint64_t sampleOffset64 = (uint64_t)frame * vi->wavPlayer->format.nSamplesPerSec * frd / fr;
                DWORD sampleOffset = (sampleOffset64 > 0xFFFFFFFF) ? 0xFFFFFFFF : (DWORD)sampleOffset64;
                WavPlayerSeek(vi->wavPlayer, sampleOffset);
            }
        }
    }
}

intptr_t __stdcall sBinkGetKeyFrame(void* a, void* b, void* c) {
    void* p = pBinkGetKeyFrame;
    LogF("BinkGetKeyFrame(%p,%p,%p)", a, b, c);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
    LogF("BinkGetKeyFrame->%p", (void*)r);
    return r;
}

void __stdcall sBinkFreeGlobals() {
    void* p = pBinkFreeGlobals;
    LogF("BinkFreeGlobals()");
    if (p) ((void(__stdcall*)())p)();
}

void __stdcall sBinkGetPlatformInfo(void* a, void* b) {
    void* p = pBinkGetPlatformInfo;
    LogF("BinkGetPlatformInfo(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetFrameBuffersInfo(void* a, void* b) {
    void* p = pBinkGetFrameBuffersInfo;
    LogF("BinkGetFrameBuffersInfo(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRegisterFrameBuffers(void* a, void* b) {
    void* p = pBinkRegisterFrameBuffers;
    LogF("BinkRegisterFrameBuffers(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetVideoOnOff(void* a, void* b) {
    void* p = pBinkSetVideoOnOff;
    LogF("BinkSetVideoOnOff(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundOnOff(void* a, void* b) {
    void* p = pBinkSetSoundOnOff;
    int on = (int)(intptr_t)b;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer && on) {
        LogF("BinkSetSoundOnOff: muted (WAV replacement active)");
        if (p) ((void(__stdcall*)(void*,void*))p)(a, (void*)0);
        return;
    }
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

// ============================================================================
// BinkSetVolume/Pan adapters
//
// Game imports: BinkSetVolume@8, BinkSetPan@12
// Real DLL has: @8 or @12 depending on version
// Adapters bridge the calling convention difference.
// ============================================================================

#if defined(BINK_GROUP_1) || defined(BINK_GROUP_2) || defined(BINK_GROUP_3) || \
    defined(BINK_GROUP_4) || defined(BINK_GROUP_6) || defined(BINK_GROUP_7) || \
    defined(BINK_GROUP_9) || defined(BINK_GROUP_10) || defined(BINK_GROUP_18)
#define BINK_HAS_VOLUME_12
#define BINK_HAS_PAN_12
#endif

void __stdcall sBinkSetVolume2(void* a, void* b) {
    void* p = pBinkSetVolume;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        LogF("BinkSetVolume2: muted (WAV replacement active)");
#ifdef BINK_HAS_VOLUME_12
        if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, (void*)0, (void*)0);
#else
        if (p) ((void(__stdcall*)(void*,void*))p)(a, (void*)0);
#endif
        return;
    }
    LogF("BinkSetVolume2(%p,%p)", a, b);
#ifdef BINK_HAS_VOLUME_12
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, 0);
#else
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
#endif
}

void __stdcall sBinkSetPan(void* a, void* b, void* c) {
    void* p = pBinkSetPan;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        LogF("BinkSetPan: muted (WAV replacement active)");
        return;
    }
    LogF("BinkSetPan(%p,%p,%p)", a, b, c);
#ifdef BINK_HAS_PAN_12
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
#else
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
#endif
}

void __stdcall sBinkSetSpeakerVolumes(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkSetSpeakerVolumes;
    LogF("BinkSetSpeakerVolumes(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sBinkService(void* a) {
    void* p = pBinkService;
    LogF("BinkService(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkShouldSkip(void* a) {
    void* p = pBinkShouldSkip;
    LogF("BinkShouldSkip(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkShouldSkip->%p", (void*)r);
    return r;
}

void __stdcall sBinkGetPalette(void* a) {
    void* p = pBinkGetPalette;
    LogF("BinkGetPalette(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkControlBackgroundIO(void* a, void* b) {
    void* p = pBinkControlBackgroundIO;
    LogF("BinkControlBackgroundIO(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkControlBackgroundIO->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkControlPlatformFeatures(void* a, void* b) {
    void* p = pBinkControlPlatformFeatures;
    LogF("BinkControlPlatformFeatures(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkControlPlatformFeatures->%p", (void*)r);
    return r;
}

void __stdcall sBinkSetWillLoop(void* a, void* b) {
    void* p = pBinkSetWillLoop;
    LogF("BinkSetWillLoop(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
    else LogF("BinkSetWillLoop: not available in this Bink version");
}

intptr_t __stdcall sBinkOpenTrack(void* a, void* b) {
    void* p = pBinkOpenTrack;
    LogF("BinkOpenTrack(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkOpenTrack->%p", (void*)r);
    return r;
}

void __stdcall sBinkCloseTrack(void* a) {
    void* p = pBinkCloseTrack;
    LogF("BinkCloseTrack(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetTrackData(void* a, void* b) {
    void* p = pBinkGetTrackData;
    LogF("BinkGetTrackData(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackData->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetTrackType(void* a, void* b) {
    void* p = pBinkGetTrackType;
    LogF("BinkGetTrackType(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetTrackMaxSize(void* a, void* b) {
    void* p = pBinkGetTrackMaxSize;
    LogF("BinkGetTrackMaxSize(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackMaxSize->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetTrackID(void* a, void* b) {
    void* p = pBinkGetTrackID;
    LogF("BinkGetTrackID(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackID->%p", (void*)r);
    return r;
}

void __stdcall sBinkGetSummary(void* a, void* b) {
    void* p = pBinkGetSummary;
    LogF("BinkGetSummary(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetRealtime(void* a, void* b, void* c) {
    void* p = pBinkGetRealtime;
    LogF("BinkGetRealtime(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkSetFileOffset(void* a, void* b) {
    void* p = pBinkSetFileOffset;
    LogF("BinkSetFileOffset(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundTrack8(void* a, void* b) {
    void* p = pBinkSetSoundTrack;
    LogF("BinkSetSoundTrack8(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundTrack4(void* a) {
    void* p = pBinkSetSoundTrack;
    LogF("BinkSetSoundTrack4(%p)", a);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, 0);
}

void __stdcall sBinkSetIO(void* a) {
    void* p = pBinkSetIO;
    LogF("BinkSetIO(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetFrameRate(void* a, void* b) {
    void* p = pBinkSetFrameRate;
    LogF("BinkSetFrameRate(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSimulate(void* a) {
    void* p = pBinkSetSimulate;
    LogF("BinkSetSimulate(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetIOSize(void* a) {
    void* p = pBinkSetIOSize;
    LogF("BinkSetIOSize(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkSetSoundSystem(void* a, void* b) {
    void* p = pBinkSetSoundSystem;
    LogF("BinkSetSoundSystem(%p,%p)", a, b);
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkOpenDirectSound(void* a) {
    void* p = pBinkOpenDirectSound;
    LogF("BinkOpenDirectSound(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenWaveOut(void* a) {
    void* p = pBinkOpenWaveOut;
    LogF("BinkOpenWaveOut(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenMiles(void* a) {
    void* p = pBinkOpenMiles;
    LogF("BinkOpenMiles(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDX8SurfaceType(void* a) {
    void* p = pBinkDX8SurfaceType;
    LogF("BinkDX8SurfaceType(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkDX8SurfaceType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkDX9SurfaceType(void* a) {
    void* p = pBinkDX9SurfaceType;
    LogF("BinkDX9SurfaceType(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkDX9SurfaceType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkBufferOpen(void* a, void* b, void* c, void* d) {
    void* p = pBinkBufferOpen;
    LogF("BinkBufferOpen(%p,%p,%p,%p)", a, b, c, d);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d) : 0;
    LogF("BinkBufferOpen->%p", (void*)r);
    return r;
}

void __stdcall sBinkBufferSetHWND(void* a, void* b) {
    void* p = pBinkBufferSetHWND;
    LogF("BinkBufferSetHWND(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sBinkDDSurfaceType(void* a) {
    void* p = pBinkDDSurfaceType;
    LogF("BinkDDSurfaceType(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkDDSurfaceType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkIsSoftwareCursor(void* a, void* b) {
    void* p = pBinkIsSoftwareCursor;
    LogF("BinkIsSoftwareCursor(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkIsSoftwareCursor->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkCheckCursor(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkCheckCursor;
    LogF("BinkCheckCursor(%p,%p,...)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e) : 0;
    LogF("BinkCheckCursor->%p", (void*)r);
    return r;
}

void __stdcall sBinkBufferSetDirectDraw(void* a, void* b) {
    void* p = pBinkBufferSetDirectDraw;
    LogF("BinkBufferSetDirectDraw(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkBufferClose(void* a) {
    void* p = pBinkBufferClose;
    LogF("BinkBufferClose(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferLock(void* a) {
    void* p = pBinkBufferLock;
    LogF("BinkBufferLock(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferUnlock(void* a) {
    void* p = pBinkBufferUnlock;
    LogF("BinkBufferUnlock(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferSetResolution(void* a, void* b, void* c) {
    void* p = pBinkBufferSetResolution;
    LogF("BinkBufferSetResolution(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferCheckWinPos(void* a, void* b, void* c) {
    void* p = pBinkBufferCheckWinPos;
    LogF("BinkBufferCheckWinPos(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetOffset(void* a, void* b, void* c) {
    void* p = pBinkBufferSetOffset;
    LogF("BinkBufferSetOffset(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferBlit(void* a, void* b, void* c) {
    void* p = pBinkBufferBlit;
    LogF("BinkBufferBlit(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetScale(void* a, void* b, void* c) {
    void* p = pBinkBufferSetScale;
    LogF("BinkBufferSetScale(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

intptr_t __stdcall sBinkBufferGetDescription(void* a) {
    void* p = pBinkBufferGetDescription;
    LogF("BinkBufferGetDescription(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkBufferGetDescription->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkBufferGetError() {
    void* p = pBinkBufferGetError;
    LogF("BinkBufferGetError()");
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkBufferGetError->%p", (void*)r);
    return r;
}

void __stdcall sBinkBufferClear(void* a, void* b) {
    void* p = pBinkBufferClear;
    LogF("BinkBufferClear(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRestoreCursor(void* a) {
    void* p = pBinkRestoreCursor;
    LogF("BinkRestoreCursor(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkStartAsyncThread(void* a, void* b) {
    void* p = pBinkStartAsyncThread;
    LogF("BinkStartAsyncThread(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkDoFrameAsync(void* a, void* b, void* c) {
    void* p = pBinkDoFrameAsync;
    LogF("BinkDoFrameAsync(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkDoFrameAsyncWait(void* a, void* b) {
    void* p = pBinkDoFrameAsyncWait;
    LogF("BinkDoFrameAsyncWait(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRequestStopAsyncThread(void* a) {
    void* p = pBinkRequestStopAsyncThread;
    LogF("BinkRequestStopAsyncThread(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkWaitStopAsyncThread(void* a) {
    void* p = pBinkWaitStopAsyncThread;
    LogF("BinkWaitStopAsyncThread(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetMixBins(void* a, void* b, void* c, void* d) {
    void* p = pBinkSetMixBins;
    LogF("BinkSetMixBins(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d);
}

void __stdcall sBinkSetMixBinVolumes(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkSetMixBinVolumes;
    LogF("BinkSetMixBinVolumes(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sExpandBink(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m, void* n) {
    void* p = pExpandBink;
    LogF("ExpandBink(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
}

void __stdcall sExpandBundleSizes(void* a, void* b) {
    void* p = pExpandBundleSizes;
    LogF("ExpandBundleSizes(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sRADSetMemory(void* a, void* b) {
    void* p = pRADSetMemory;
    LogF("RADSetMemory(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetMemory(void* a, void* b) {
    void* p = pBinkSetMemory;
    LogF("BinkSetMemory(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sRADTimerRead() {
    void* p = pRADTimerRead;
    LogF("RADTimerRead()");
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("RADTimerRead->%p", (void*)r);
    return r;
}

intptr_t __stdcall sradmalloc(void* a) {
    void* p = pradmalloc;
    LogF("radmalloc(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("radmalloc->%p", (void*)r);
    return r;
}

void __stdcall sradfree(void* a) {
    void* p = pradfree;
    LogF("radfree(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

// ============================================================================
// YUV blit proxy stubs
// ============================================================================

void __stdcall sYUV_init(void* a) {
    void* p = pYUV_init;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sYUV_blit_16a1bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a1bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16a1bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a1bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16a4bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a4bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16a4bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a4bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_16bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_16bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_16bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24rbpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24rbpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24rbpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24rbpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32abpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32abpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32abpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32abpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32rabpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32rabpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32rabpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32rabpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32rbpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32rbpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32rbpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32rbpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_UYVY(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_UYVY;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_UYVY_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_UYVY_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_YUY2(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_YUY2;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_YUY2_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_YUY2_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_YV12(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_YV12;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

} /* extern "C" */
