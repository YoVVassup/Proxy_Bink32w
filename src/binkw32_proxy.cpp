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
// Bink 1.x open flags (used in sBinkOpen and ExtractFileName)
// ============================================================================
constexpr DWORD BINK_FLAG_FILEHANDLE  = 0x00800000; // 'a' is a HANDLE
constexpr DWORD BINK_FLAG_IOPROCESSOR = 0x02000000; // 'a' is custom IO context
constexpr DWORD BINK_FLAG_FROM_MEMORY = 0x04000000; // 'a' is memory buffer pointer

// ============================================================================
// Proxy_Bink32w v2.1.0 — Bink Video API Proxy DLL
//
// Drop-in binkw32.dll replacement that intercepts Bink video API calls.
// Features: audio replacement (.bik -> .wav), .mix archive parsing,
//           aspect-ratio fit scaling, call stack logging.
// ============================================================================

// Handle to the real Bink DLL loaded at runtime
static HMODULE g_hR = NULL;
#ifdef BINK_TEST_BUILD
LONG g_initState = 0;
#else
static LONG g_initState = 0;
#endif

// ============================================================================
// BinkSetVolume/Pan/MixBins adapters
//
// Two generations of the audio API are in the wild (RAD bink.h):
//   Bink <=1.2c  Volume@8  (bnk, volume)           Pan@8  (bnk, pan)
//                MixBins@8 (bnk, mix_bins)
//   Bink >=1.2i  Volume@12  (bnk, trackid, volume) Pan@12 (bnk, trackid, pan)
//                MixBins@16 (bnk, trackid, mix_bins, total)
// `trackid` is the encoder-chosen sound track id (see BinkGetTrackID) - not a
// track index, and not necessarily 0.
//
// The game imports one generation while the real DLL implements the other.
// Both arities are exported from exports.def; the gates below pick the arity
// of the call into the real DLL, and ResolveTrackId()/CallSet*() (after the
// pointer table) do the conversion.
//
// Declared up here (not next to the stubs) because RestoreOriginalAudio() in
// the audio-replacement path needs the same knowledge.
// ============================================================================

#if defined(BINK_GROUP_1) || defined(BINK_GROUP_2) || defined(BINK_GROUP_3) || \
    defined(BINK_GROUP_4) || defined(BINK_GROUP_6) || defined(BINK_GROUP_7) || \
    defined(BINK_GROUP_9) || defined(BINK_GROUP_10) || defined(BINK_GROUP_18)
#define BINK_HAS_VOLUME_12
#define BINK_HAS_PAN_12
#endif

// Group 12 (1.2c/1.2d) is the only generation with the two-argument MixBins.
#if defined(BINK_GROUP_12)
#define BINK_HAS_MIXBINS_8
#endif

constexpr int WAV_START_ATTEMPTS = 3; // WavPlayerStart tries before giving up

static BOOL LoadDll();

// Deferred initialization — called from sBinkOpen/sBinkOpenWithOptions
// instead of DllMain to avoid LoadLibrary + I/O under loader lock.
#ifdef BINK_TEST_BUILD
BOOL EnsureInitialized();
#else
static BOOL EnsureInitialized();
#endif

// One-time init slot: 0 = unclaimed, 1 = a thread is initializing,
// 2 = ready, 3 = failed. State 3 used to be terminal: a single transient
// LoadLibrary failure (file briefly locked, path not yet visible) killed
// video for the whole process. Retries are bounded so a genuinely missing
// real DLL cannot turn every Bink* call into a LoadLibrary attempt.
#define INIT_MAX_ATTEMPTS 4
static LONG g_initAttempts = 0;

BOOL EnsureInitialized() {
    LONG prev = InterlockedCompareExchange(&g_initState, 1, 0);
    if (prev == 3) {
        // Claim the slot again for a retry; if someone else got there
        // first, fall through as an ordinary waiter.
        prev = (InterlockedCompareExchange(&g_initState, 1, 3) == 3) ? 0 : 1;
    }
    if (prev == 0) {
        if (InterlockedIncrement(&g_initAttempts) > INIT_MAX_ATTEMPTS) {
            InterlockedExchange(&g_initState, 3);
        } else {
            LoadAudioConfig();
            if (LoadDll()) {
                InterlockedExchange(&g_initState, 2);
            } else {
                InterlockedExchange(&g_initState, 3);
            }
        }
    } else if (prev == 1) {
        // A waiter only observes. The old timeout CAS(1 -> 3) could record
        // "failed" while the loader was still running, and the loader then
        // unconditionally published 2, so two callers disagreed about whether
        // init had happened.
        // Interlocked read — a plain `g_initState == 1` may be hoisted out
        // of the loop by the optimizer, spinning forever.
        DWORD start = GetTickCount();
        while (InterlockedCompareExchange(&g_initState, 1, 1) == 1 &&
               GetTickCount() - start < 5000) {
            SwitchToThread();
        }
    }
    return InterlockedCompareExchange(&g_initState, 2, 2) == 2;
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
D(BinkSetFileOffset)
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
D(BinkSetSoundTrack)
#undef D

// ============================================================================
// Audio arity helpers (see the BINK_HAS_* gates at the top of the file)
// ============================================================================

// Sound track id to use when the game speaks the short (<=1.2c) API but the
// real DLL expects a track id: the movie's primary audio, track index 0.
// Track ids are chosen by the encoder, so ask the real DLL instead of
// assuming 0. BinkGetTrackID dereferences the handle without a null check and
// faults on a movie without audio tracks, hence the SEH fallback. Returns
// NULL whenever the real DLL takes the short form (it has no track id).
static void* ResolveTrackId(void* bnk) {
#ifndef BINK_HAS_VOLUME_12
    (void)bnk;
    return NULL;
#else
    void* get = pBinkGetTrackID;
    if (!get) return NULL;
    void* id = NULL;
    __try {
        id = ((void*(__stdcall*)(void*, void*))get)(bnk, (void*)0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        id = NULL;
    }
    return id;
#endif
}

// Forward to the real DLL with the arity it exports. The track id exists only
// in the long form; the short form simply has nowhere to put it.
static void CallSetVolume(void* p, void* bnk, void* trackid, void* volume) {
#ifdef BINK_HAS_VOLUME_12
    ((void(__stdcall*)(void*, void*, void*))p)(bnk, trackid, volume);
#else
    (void)trackid;
    ((void(__stdcall*)(void*, void*))p)(bnk, volume);
#endif
}

static void CallSetPan(void* p, void* bnk, void* trackid, void* pan) {
#ifdef BINK_HAS_PAN_12
    ((void(__stdcall*)(void*, void*, void*))p)(bnk, trackid, pan);
#else
    (void)trackid;
    ((void(__stdcall*)(void*, void*))p)(bnk, pan);
#endif
}

// The short MixBins has neither a track id nor an entry count, so a short
// call into a long real DLL cannot recover `total`. It is forwarded as 0 -
// no bins applied - rather than a guessed count, which would read past the
// caller's array. The long -> short direction keeps the bin array, which is
// the only argument the short form has room for.
static void CallSetMixBins(void* p, void* bnk, void* trackid, void* mixBins, void* total) {
#ifdef BINK_HAS_MIXBINS_8
    (void)trackid;
    (void)total;
    ((void(__stdcall*)(void*, void*))p)(bnk, mixBins);
#else
    ((void(__stdcall*)(void*, void*, void*, void*))p)(bnk, trackid, mixBins, total);
#endif
}

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

// Byte size of a stdcall decoration `_Name@N`. Resolving by ordinal says
// nothing about the callee's arity, and forwarding the wrong number of
// arguments unbalances the caller's stack (AV when it returns), so the real
// decoration is matched by name once, at load time.
static int ProbeArity(void* fn, const char* base, const int* bytes, int count) {
    if (!fn || !g_hR) return 0;
    char name[96];
    for (int i = 0; i < count; i++) {
        _snprintf_s(name, sizeof(name), _TRUNCATE, "_%s@%d", base, bytes[i]);
        WarnTruncate("decorated export name", name, sizeof(name));
        if ((void*)GetProcAddress(g_hR, name) == fn) return bytes[i];
    }
    return 0;
}

// Index of a YUV blit in g_yuvArity[] — order matches the YUV_BLIT_* stubs
// (declared in binkw32_proxy.h so tests can address the same slots).

// BinkSetSoundTrack is exported at two different arities by RAD's DLLs:
// groups 5, 8, 11-17, 19 carry `@4`, the rest `@8`. The probe is the
// authority; the group define is only a fallback when the real DLL does not
// expose the decorated name at all.
#if defined(BINK_GROUP_5) || defined(BINK_GROUP_8) || defined(BINK_GROUP_11) || \
    defined(BINK_GROUP_12) || defined(BINK_GROUP_13) || defined(BINK_GROUP_14) || \
    defined(BINK_GROUP_15) || defined(BINK_GROUP_16) || defined(BINK_GROUP_17) || \
    defined(BINK_GROUP_19)
#define BINK_SETSOUNDTRACK_FALLBACK 4
#else
#define BINK_SETSOUNDTRACK_FALLBACK 8
#endif

#ifdef BINK_TEST_BUILD
int g_soundTrackArity = BINK_SETSOUNDTRACK_FALLBACK;   // bytes; 4 or 8
int g_yuvArity[YUV_ARITY_COUNT] = { 0 };
#else
static int g_soundTrackArity = BINK_SETSOUNDTRACK_FALLBACK;
static int g_yuvArity[YUV_ARITY_COUNT] = { 0 };
#endif

#define PROBE_YUV(n, id) \
    g_yuvArity[id] = ProbeArity(p##n, #n, kYuvBytes, \
        (int)(sizeof(kYuvBytes) / sizeof(kYuvBytes[0])));

static BOOL LoadDll() {
    if (g_hR) return TRUE;

    // GetModuleFileNameA fails when the buffer is too small (the return value
    // then equals the buffer size) and returns 0 on error; without the check
    // strrchr below runs over an uninitialized buffer.
    char exePath[MAX_PATH] = "";
    DWORD exeLen = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (exeLen == 0 || exeLen >= MAX_PATH) {
        LogF("Cannot resolve the host exe path (rc=%lu); using the proxy directory only",
             (unsigned long)exeLen);
        exePath[0] = '\0';
    }
    char* slash = strrchr(exePath, '\\');
    if (slash) *(slash + 1) = 0;
    else exePath[0] = '\0';

    // Try exe dir first, then the proxy DLL's own directory
    const char* dirs[2] = { exePath, g_dllDir };
    char dllPath[MAX_PATH];
    for (int d = 0; d < 2 && !g_hR; d++) {
        if (!dirs[d][0]) continue;
        if (d == 1 && _stricmp(dirs[d], exePath) == 0) continue;

        if (strlen(dirs[d]) + strlen(BINK_REAL_DLL) >= sizeof(dllPath)) {
            LogF("Real DLL path would overflow MAX_PATH, skipped: %s%s",
                 dirs[d], BINK_REAL_DLL);
            continue;
        }
        _snprintf_s(dllPath, sizeof(dllPath), _TRUNCATE, "%s%s", dirs[d], BINK_REAL_DLL);
        g_hR = LoadLibraryA(dllPath);
        if (g_hR) {
            LogF("Real DLL loaded: %s", dllPath);
            break;
        }
#ifdef BINK_COMPAT_DLLS
        // Parse comma-separated list of compatible DLLs
        char compatBuf[512];
        strncpy_s(compatBuf, sizeof(compatBuf), BINK_COMPAT_DLLS, _TRUNCATE);
        char* ctx = NULL;
        char* token = strtok_s(compatBuf, ",", &ctx);
        while (token) {
            // Skip primary (already tried)
            if (_stricmp(token, BINK_REAL_DLL) != 0) {
                if (strlen(dirs[d]) + strlen(token) >= sizeof(dllPath)) {
                    LogF("Compat DLL path would overflow MAX_PATH, skipped: %s%s",
                         dirs[d], token);
                } else {
                    _snprintf_s(dllPath, sizeof(dllPath), _TRUNCATE, "%s%s", dirs[d], token);
                    g_hR = LoadLibraryA(dllPath);
                    if (g_hR) {
                        LogF("Real DLL loaded (compat): %s", dllPath);
                        break;
                    }
                }
            }
            token = strtok_s(NULL, ",", &ctx);
        }
#endif
    }

    if (!g_hR) {
        LogF("FAILED to load any real DLL from %s or %s", exePath, g_dllDir);
        return FALSE;
    }

    for (int i = 0; i < (int)BINK_ORDINAL_COUNT; i++) {
        *BINK_ORDINAL_TABLE[i].dest = (void*)GetProcAddress(g_hR, (LPCSTR)BINK_ORDINAL_TABLE[i].ordinal);
    }

    static const int kSoundTrackBytes[] = { 4, 8 };
    g_soundTrackArity = ProbeArity(pBinkSetSoundTrack, "BinkSetSoundTrack",
                                   kSoundTrackBytes, 2);
    if (!g_soundTrackArity) g_soundTrackArity = BINK_SETSOUNDTRACK_FALLBACK;

    static const int kYuvBytes[] = { 36, 40, 44, 48, 52, 56, 60 };
    PROBE_YUV(YUV_blit_16a1bpp, YUV_A_16a1bpp)
    PROBE_YUV(YUV_blit_16a1bpp_mask, YUV_A_16a1bpp_mask)
    PROBE_YUV(YUV_blit_16a4bpp, YUV_A_16a4bpp)
    PROBE_YUV(YUV_blit_16a4bpp_mask, YUV_A_16a4bpp_mask)
    PROBE_YUV(YUV_blit_16bpp, YUV_A_16bpp)
    PROBE_YUV(YUV_blit_16bpp_mask, YUV_A_16bpp_mask)
    PROBE_YUV(YUV_blit_24bpp, YUV_A_24bpp)
    PROBE_YUV(YUV_blit_24bpp_mask, YUV_A_24bpp_mask)
    PROBE_YUV(YUV_blit_24rbpp, YUV_A_24rbpp)
    PROBE_YUV(YUV_blit_24rbpp_mask, YUV_A_24rbpp_mask)
    PROBE_YUV(YUV_blit_32abpp, YUV_A_32abpp)
    PROBE_YUV(YUV_blit_32abpp_mask, YUV_A_32abpp_mask)
    PROBE_YUV(YUV_blit_32bpp, YUV_A_32bpp)
    PROBE_YUV(YUV_blit_32bpp_mask, YUV_A_32bpp_mask)
    PROBE_YUV(YUV_blit_32rabpp, YUV_A_32rabpp)
    PROBE_YUV(YUV_blit_32rabpp_mask, YUV_A_32rabpp_mask)
    PROBE_YUV(YUV_blit_32rbpp, YUV_A_32rbpp)
    PROBE_YUV(YUV_blit_32rbpp_mask, YUV_A_32rbpp_mask)
    PROBE_YUV(YUV_blit_UYVY, YUV_A_UYVY)
    PROBE_YUV(YUV_blit_UYVY_mask, YUV_A_UYVY_mask)
    PROBE_YUV(YUV_blit_YUY2, YUV_A_YUY2)
    PROBE_YUV(YUV_blit_YUY2_mask, YUV_A_YUY2_mask)
    PROBE_YUV(YUV_blit_YV12, YUV_A_YV12)

    LogF("Proxied functions resolved: pBinkOpen=%p pBinkDoFrame=%p pBinkClose=%p pBinkWait=%p",
         pBinkOpen, pBinkDoFrame, pBinkClose, pBinkWait);
    LogF("BinkSetSoundTrack arity: %d bytes", g_soundTrackArity);
    return TRUE;
}

// ============================================================================
// Video handle tracking + audio replacement trigger
// ============================================================================

VideoInfo g_vids[MAX_TRACKED];
int g_vidCount = 0;

static CRITICAL_SECTION g_trackCs;
static LONG g_trackCsOnce = 0;

static void InitTrackCs() {
    if (InterlockedCompareExchange(&g_trackCsOnce, 1, 0) == 0) {
        InitializeCriticalSection(&g_trackCs);
        InterlockedExchange(&g_trackCsOnce, 2);
    } else {
        // InitializeCriticalSection cannot block, so this wait is normally
        // microseconds; the bound mirrors the one in EnsureInitialized and
        // exists so a thread that died after claiming the slot is reported
        // instead of hanging the caller silently.
        DWORD start = GetTickCount();
        bool warned = false;
        while (InterlockedCompareExchange(&g_trackCsOnce, 2, 2) != 2) {
            if (!warned && GetTickCount() - start >= 5000) {
                warned = true;
                LogF("WARNING: track lock initialization still pending after 5000 ms");
            }
            SwitchToThread();
        }
    }
}

void TrackLock() {
    InitTrackCs();
    EnterCriticalSection(&g_trackCs);
}

void TrackUnlock() {
    LeaveCriticalSection(&g_trackCs);
}

// --- ScaleBufs: refcounted scale scratch (see binkw32_proxy.h) -----------

ScaleBufs* ScaleBufsCreate(void) {
    ScaleBufs* sb = (ScaleBufs*)calloc(1, sizeof(ScaleBufs));
    if (!sb) return NULL;
    InitializeCriticalSection(&sb->cs);
    sb->refs = 1;
    return sb;
}

void ScaleBufsRef(ScaleBufs* sb) {
    if (sb) InterlockedIncrement(&sb->refs);
}

void ScaleBufsUnref(ScaleBufs* sb) {
    if (!sb) return;
    if (InterlockedDecrement(&sb->refs) != 0) return;
    // Last reference. Nobody can be inside sb->cs: a thread may only enter
    // while it holds a reference, and this drop is the last one.
    DeleteCriticalSection(&sb->cs);
    if (sb->tempBuf) VirtualFree(sb->tempBuf, 0, MEM_RELEASE);
    free(sb->lookupX);
    free(sb->lookupY);
    free(sb);
}

void TrackVideo(void* h, const char* bikPath, const char* mixName) {
    if (!h || !pBinkGetSummary) return;
    unsigned char summary[128];
    memset(summary, 0, sizeof(summary));
    // BINKSUMMARY is 124 bytes; SEH guards against a callee that overruns.
    __try {
        ((void(__stdcall*)(void*, void*))pBinkGetSummary)(h, summary);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        LogF("TrackVideo: BinkGetSummary faulted for %p", h);
        return;
    }
    uint32_t w = ReadU32(summary);
    uint32_t hv = ReadU32(summary + 4);
    uint32_t frameRate = ReadU32(summary + 20);
    uint32_t frameRateDiv = ReadU32(summary + 24);
    TrackLock();
    if (w == 0 || hv == 0) {
        TrackUnlock();
        return;
    }
    // Upper bound as well: the summary is the one place video dimensions
    // arrive without validation, and everything downstream (scaling buffer
    // size, pitch math) assumes they are bounded. A lying/faulting callee
    // must not push 0xFFFFFFFF through to VirtualAlloc. Far above any real
    // Bink movie (Bink 2 tops out at 4096x4096).
    if (w > 16384 || hv > 16384) {
        LogF("TrackVideo: implausible size %ux%u for %p, not tracking", w, hv, h);
        TrackUnlock();
        return;
    }
    if (g_vidCount >= MAX_TRACKED) {
        // Silently dropping the video here made the failure invisible: no
        // scaling and no audio replacement, with nothing in the log to say
        // why. The array is fixed at MAX_TRACKED, so the only fix is to
        // close a movie — surface it instead of swallowing it.
        LogF("TrackVideo: table full (%d/%d), not tracking %p [%ux%u]",
             g_vidCount, MAX_TRACKED, h, w, hv);
        TrackUnlock();
        return;
    }
    {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].handle == h) {
                g_vids[i].width = w;
                g_vids[i].height = hv;
                // A re-track (BinkOpen of the same handle) must not leave the
                // slot without scratch buffers: ScaleBufsCreate failing once
                // used to stick for the life of the slot, silently disabling
                // scaling with no way to recover short of closing the movie.
                if (!g_vids[i].scale) {
                    g_vids[i].scale = ScaleBufsCreate();
                    if (g_vids[i].scale)
                        LogF("ScaleBufsCreate retry succeeded for %p", h);
                }
                LogF("Updated video: %p %ux%u", h, w, hv);
                TrackUnlock();
                return;
            }
        }
        g_vids[g_vidCount].handle = h;
        g_vids[g_vidCount].width = w;
        g_vids[g_vidCount].height = hv;
        g_vids[g_vidCount].scale = ScaleBufsCreate();
        if (!g_vids[g_vidCount].scale)
            LogF("ScaleBufsCreate failed for %p — scaled blit disabled", h);
        g_vids[g_vidCount].wavPath[0] = '\0';
        g_vids[g_vidCount].wavPlayer = NULL;
        g_vids[g_vidCount].wavStarted = false;
        g_vids[g_vidCount].wavFailed = false;
        g_vids[g_vidCount].wavAttempts = 0;
        g_vids[g_vidCount].pauseRequested = false;
        g_vids[g_vidCount].seekPending = false;
        g_vids[g_vidCount].seekFrame = 0;
        g_vids[g_vidCount].seekFr = 0;
        g_vids[g_vidCount].seekFrD = 0;
        g_vids[g_vidCount].soundOn = TRUE;
        g_vids[g_vidCount].soundReqSet = FALSE;
        g_vids[g_vidCount].volumeSet = FALSE;
        g_vids[g_vidCount].volumeTrack = NULL;
        g_vids[g_vidCount].volumeValue = NULL;
        g_vids[g_vidCount].panSet = FALSE;
        g_vids[g_vidCount].panValue0 = NULL;
        g_vids[g_vidCount].panValue1 = NULL;

        const char* wav = FindWavForBik(bikPath, mixName);
        if (wav) {
            // Verify the replacement file actually exists before committing to it.
            // If the file is missing, skip the replacement and let original audio play.
            char fullWavPath[MAX_PATH];
            if (wav[1] == ':' || (wav[0] == '\\' && wav[1] == '\\')) {
                WarnTruncate("audio mapping path", wav, sizeof(fullWavPath));
                strncpy_s(fullWavPath, sizeof(fullWavPath), wav, _TRUNCATE);
            } else {
                if (strlen(g_dllDir) + strlen(wav) >= sizeof(fullWavPath))
                    LogF("WARNING: audio mapping path longer than %u chars, truncated: %s%s",
                         (unsigned)(sizeof(fullWavPath) - 1), g_dllDir, wav);
                _snprintf_s(fullWavPath, sizeof(fullWavPath), _TRUNCATE, "%s%s", g_dllDir, wav);
            }
            DWORD attr = GetFileAttributesA(fullWavPath);
            if (attr != INVALID_FILE_ATTRIBUTES) {
                WarnTruncate("audio mapping target", wav, sizeof(g_vids[g_vidCount].wavPath));
                strncpy_s(g_vids[g_vidCount].wavPath, sizeof(g_vids[g_vidCount].wavPath), wav, _TRUNCATE);
                LogF("Audio replacement queued: %s [%ux%u] -> %s", bikPath ? bikPath : "?", w, hv, wav);
            } else {
                LogF("Replacement file not found, using original audio: %s (looked for %s)", wav, fullWavPath);
            }
        } else {
            LogF("No audio mapping for: %s [%ux%u]", bikPath ? bikPath : "?", w, hv);
        }

        g_vidCount++;
        // frameRate/frameRateDiv come from the same summary block — report
        // them instead of reading them and dropping them on the floor.
        LogF("Tracked video: %p %ux%u @ %u/%u fps", h, w, hv, frameRate, frameRateDiv);
    }
    TrackUnlock();
}

void UntrackVideo(void* h) {
    // Detach the slot first, release what it owned afterwards.
    // FreePlayer blocks (waveOutReset + drain + waveOutClose) and the teardown
    // below is plain heap work — neither belongs inside the track lock that
    // every other sBink* entry point takes. Detaching first also means
    // the player can no longer be reached through g_vids[] while it dies.
    VideoInfo victim;
    memset(&victim, 0, sizeof(victim));
    BOOL found = FALSE;
    TrackLock();
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) {
            victim = g_vids[i];
            memmove(&g_vids[i], &g_vids[i + 1], (g_vidCount - i - 1) * sizeof(VideoInfo));
            g_vidCount--;
            memset(&g_vids[g_vidCount], 0, sizeof(VideoInfo));
            found = TRUE;
            break;
        }
    }
    TrackUnlock();
    if (!found) return;
    if (victim.wavPlayer) FreePlayer(victim.wavPlayer);
    // Drops the slot's reference only: an in-flight sBinkCopyToBuffer holds
    // its own reference, so the buffers outlive this call if it does.
    if (victim.scale) ScaleBufsUnref(victim.scale);
}

#ifdef BINK_TEST_BUILD
// Test-only — see the note in binkw32_proxy.h. The returned pointer is
// only valid while nothing compacts g_vids[].
VideoInfo* FindVideo(void* h) {
    VideoInfo* result = NULL;
    TrackLock();
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) {
            result = &g_vids[i];
            break;
        }
    }
    TrackUnlock();
    return result;
}
#endif

VideoInfo* FindVideoLocked(void* h) {
    TrackLock();
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) return &g_vids[i];
    }
    TrackUnlock();
    return NULL;
}

// Caller must already hold the track lock; the lock stays held on return.
static VideoInfo* FindVideoHeld(void* h) {
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

    if (flags & BINK_FLAG_FILEHANDLE) {
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

    if (flags & BINK_FLAG_FROM_MEMORY) {
        return;
    }

    if (flags & BINK_FLAG_IOPROCESSOR) {
        return;
    }

    if (a) {
        __try {
            size_t len = strnlen((const char*)a, 1024);
            if (len > 0 && len < 1024) {
                strncpy_s(out, outSize, (const char*)a, _TRUNCATE);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            out[0] = '\0';
        }
    }
}

// ============================================================================
// Extract filename from CCFileClass*
//
// When BINKIOPROCESSOR flag is used, the first parameter to BinkOpen is a
// CCFileClass* (RA2/YR engine). We extract the .bik filename for audio
// replacement matching using two approaches:
//
// 1. Vtable: call GetFileName() via vtable[1] (FileClass hierarchy from YRpp)
// 2. Fallback: read FileName field at offset 24 (RawFileClass::FileName)
//
// The fallback handles cases where the vtable is hooked by third-party code
// (e.g., IHCore hooks CCFileClass methods to redirect I/O to zip archives).
// Both approaches use SEH to protect against invalid memory access.
// ============================================================================

#ifdef BINK_TEST_BUILD
BOOL ExtractNameFromCCFileClass(void* ccFile, char* out, int outSize) {
#else
static BOOL ExtractNameFromCCFileClass(void* ccFile, char* out, int outSize) {
#endif
    if (!ccFile || !out || outSize <= 0) return FALSE;
    out[0] = '\0';

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ccFile, &mbi, sizeof(mbi)) < sizeof(mbi)) return FALSE;
    if (!(mbi.State & MEM_COMMIT) || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return FALSE;

    const char* name = NULL;

    // Approach 1: vtable GetFileName() — vtable[1] in FileClass hierarchy
    __try {
        void** vtable = *(void***)ccFile;
        if (vtable &&
            VirtualQuery(vtable, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
            (mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {

            void* fnPtr = vtable[1];
            if (fnPtr &&
                VirtualQuery(fnPtr, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
                (mbi.State & MEM_COMMIT)) {

                DWORD prot = mbi.Protect & 0xFF;
                if (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
                    prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY) {

                    typedef const char* (__thiscall *GetFileNameFn)(void* thisptr);
                    GetFileNameFn getFileName = (GetFileNameFn)fnPtr;
                    name = getFileName(ccFile);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        name = NULL;
    }

    // `name` points into memory owned by the callee (possibly a hooked
    // vtable). VirtualQuery is only a fast reject — a committed page can still
    // fault, so the dereference itself must sit inside SEH.
    if (name && VirtualQuery((void*)name, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
        (mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {

        BOOL copied = FALSE;
        __try {
            if (name[0] != '\0') {
                size_t len = strnlen(name, 256);
                if (len > 0 && len < 256) {
                    strncpy_s(out, outSize, name, _TRUNCATE);
                    copied = TRUE;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            copied = FALSE;
        }
        if (copied) {
            LogF("CCFileClass: extracted '%s' via vtable", out);
            return TRUE;
        }
    }

    // Approach 2: RawFileClass::FileName at offset 24
    // Works when vtable is hooked (IHCore) or has unexpected layout
    __try {
        const char* fallbackName = *(const char**)((const char*)ccFile + 24);
        if (fallbackName && fallbackName[0] != '\0' &&
            VirtualQuery((void*)fallbackName, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
            (mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {

            size_t len = strnlen(fallbackName, 256);
            if (len > 0 && len < 256) {
                strncpy_s(out, outSize, fallbackName, _TRUNCATE);
                LogF("CCFileClass: extracted '%s' via FileName offset", out);
                return TRUE;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }

    LogF("CCFileClass: failed to extract filename from %p", ccFile);
    return FALSE;
}

void LogCallStack(int skip) {
    void* stack[8];
    USHORT frames = CaptureStackBackTrace(skip, 8, stack, NULL);
    if (frames == 0) return;
    char buf[1024] = "";
    int pos = 0;
    for (USHORT i = 0; i < frames && pos < (int)sizeof(buf) - 80; i++) {
        HMODULE hMod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)stack[i], &hMod);
        int written;
        if (hMod) {
            char modName[MAX_PATH] = "?";
            GetModuleFileNameA(hMod, modName, MAX_PATH);
            const char* slash = strrchr(modName, '\\');
            DWORD rva = (DWORD)((char*)stack[i] - (char*)hMod);
            written = _snprintf_s(buf + pos, sizeof(buf) - pos, _TRUNCATE,
                           "  -> %s+0x%X", slash ? slash + 1 : modName, rva);
        } else {
            written = _snprintf_s(buf + pos, sizeof(buf) - pos, _TRUNCATE,
                           "  -> %p", stack[i]);
        }
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
        // reserved != NULL means the process is terminating: the OS reclaims
        // every handle, heap block and thread, so teardown is deliberately
        // skipped — doing waveOut/logging work under the loader lock at exit
        // would only add hang risk for no benefit. Everything below runs for
        // FreeLibrary (reserved == NULL) only.
        if (reserved != NULL) break;
        // This runs under the loader lock (FreeLibrary, not process exit —
        // see the guard above). Three mitigations: logging is switched off first
        // so no teardown step can open or extend the log file (LogF -> CreateFile),
        // the players are released only after their g_vids[] slots are gone, and
        // FreeMixCache() takes g_mixCs with TryEnterCriticalSection (a thread
        // holding that lock may itself be waiting on the loader).
        // Residual risk accepted: waveOutReset/Close inside FreePlayer may still
        // wait for a driver callback while other threads are suspended by the
        // loader lock. Deferring the close is not possible (nothing runs after
        // DllMain) and skipping it would leak the device until process exit.
        g_logEnabled = FALSE;
        for (int i = 0; i < g_vidCount; i++) {
            g_vids[i].wavPlayer = NULL;
            if (g_vids[i].scale) ScaleBufsUnref(g_vids[i].scale);
        }
        g_vidCount = 0;
        // The player is released after the tracking slot it belonged to, so
        // nothing can still be reached through g_vids when the device goes away.
        for (int i = 0; i < g_playerCount; i++) {
            FreePlayer(&g_players[i]);
        }
        g_playerCount = 0;
        FreeMixCache(); // was `g_mixCacheCount = 0` — leaked the index arrays;
                        // best-effort under the loader lock (TryEnterCriticalSection)
        if (g_hR) { FreeLibrary(g_hR); g_hR = NULL; }
        ShutdownLog();
        break;
    }
    return TRUE;
}

// ============================================================================
// Resolve IHCore's ExtBink_GetCurrentBikName getter.
//
// IHCore declares it as
//   extern "C" __declspec(dllexport) const char* __stdcall ExtBink_GetCurrentBikName();
// with no .def file and no /export linker directives, so MSVC exports the
// stdcall-decorated name "_ExtBink_GetCurrentBikName@0" (verified with a
// minimal cl /LD build + dumpbin /exports). Looking up the plain name would
// silently return NULL and lose the only authoritative .bik name source that
// works when IHCore has hooked the CCFileClass vtable. Try the decorated
// names first, then the plain name for a future .def-based IHCore build.
//
// Kept OUTSIDE the extern "C" block below: the test-only declaration in
// binkw32_proxy.h has C++ linkage, and mixing it with C linkage here is a
// hard C2732 error.
// ============================================================================
#ifdef BINK_TEST_BUILD
FARPROC ResolveExtBikNameGetter(HMODULE mod) {
#else
static FARPROC ResolveExtBikNameGetter(HMODULE mod) {
#endif
    if (!mod) return NULL;
    static const char* const kNames[] = {
        "_ExtBink_GetCurrentBikName@0",
        "ExtBink_GetCurrentBikName@0",
        "ExtBink_GetCurrentBikName",
    };
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        FARPROC p = GetProcAddress(mod, kNames[i]);
        if (p) return p;
    }
    return NULL;
}

// ============================================================================
// Proxy exports — one stub per Bink API function
// ============================================================================

extern "C" {

intptr_t __stdcall sBinkLogoAddress() {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkLogoAddress;
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkLogoAddress->%p", (void*)r);
    return r;
}

// When the real DLL never loaded there is no pBinkSetError/pBinkGetError
// to talk to, so the proxy keeps its own message. Without this a failed
// init just makes BinkOpen return NULL and BinkGetError() empty.
//
// Written from the thread that failed (SetError) and read from whichever
// thread next asks (GetError) — without a lock a torn read hands the caller
// a string that is not NUL-terminated anywhere. The CS is initialized with
// the rest of the module state and, like `g_players[].cs`, never deleted.
static char g_proxyError[512] = "";
static CRITICAL_SECTION g_proxyErrorCs;
static LONG g_proxyErrorCsOnce = 0;

static void EnsureProxyErrorCs() {
    if (InterlockedCompareExchange(&g_proxyErrorCsOnce, 1, 0) == 0) {
        InitializeCriticalSection(&g_proxyErrorCs);
        InterlockedExchange(&g_proxyErrorCsOnce, 2);
    } else {
        // Same bounded diagnostic as InitTrackCs; OutputDebugStringA rather
        // than LogF so this path can never route back into the error string.
        DWORD start = GetTickCount();
        bool warned = false;
        while (InterlockedCompareExchange(&g_proxyErrorCsOnce, 2, 2) != 2) {
            if (!warned && GetTickCount() - start >= 5000) {
                warned = true;
                OutputDebugStringA("WARNING: proxy error lock initialization still pending after 5000 ms\n");
            }
            SwitchToThread();
        }
    }
}

static void SetProxyError(const char* msg) {
    EnsureProxyErrorCs();
    EnterCriticalSection(&g_proxyErrorCs);
    if (!msg) g_proxyError[0] = '\0';
    else {
        __try {
            _snprintf_s(g_proxyError, sizeof(g_proxyError), _TRUNCATE, "%s", msg);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_proxyError[0] = '\0';
        }
    }
    LeaveCriticalSection(&g_proxyErrorCs);
}

// Copy under the lock: the returned buffer is thread-local, so the caller
// can keep it after SetProxyError overwrites the shared one.
static void CopyProxyError(char* out, size_t outSize) {
    out[0] = '\0';
    if (!outSize) return;
    EnsureProxyErrorCs();
    EnterCriticalSection(&g_proxyErrorCs);
    if (g_proxyError[0]) {
        __try {
            strncpy_s(out, outSize, g_proxyError, _TRUNCATE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            out[0] = '\0';
        }
    }
    LeaveCriticalSection(&g_proxyErrorCs);
}

// Per-thread copy handed out by sBinkGetError: Bink's contract is that the
// returned pointer stays readable, and a shared buffer could be rewritten by
// SetProxyError on another thread the moment we return it.
static __declspec(thread) char g_proxyErrorCopy[512];

void __stdcall sBinkSetError(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetError;
    LogF("BinkSetError(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
    else SetProxyError((const char*)a);
}

intptr_t __stdcall sBinkGetError() {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetError;
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    if (!r) {
        // Copied out under the lock into per-thread storage: handing the
        // caller `g_proxyError` itself would let a concurrent SetProxyError
        // rewrite the bytes under it, and there is no guarantee the read and
        // the write happen on the same thread.
        CopyProxyError(g_proxyErrorCopy, sizeof(g_proxyErrorCopy));
        if (g_proxyErrorCopy[0]) r = (intptr_t)g_proxyErrorCopy;
    }
    if (g_logWait) LogF("BinkGetError()=%p", (void*)r);
    return r;
}

// Validates that s is a readable NUL-terminated string within maxLength bytes.
// SEH-protected: a committed page can still fault (string crossing into an
// unmapped/guard page), and a missing NUL would make strlen run away.
static BOOL IsValidStringA(const char* s, int maxLength) {
    if (!s) return FALSE;
    __try {
        for (int i = 0; i < maxLength; i++) {
            if (s[i] == '\0') return TRUE;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
    return FALSE;
}

intptr_t __stdcall sBinkOpen(void* a, void* b) {
    if (!EnsureInitialized()) {
        // Give BinkGetError() something to report.
        SetProxyError("Proxy_Bink32w: could not load the real " BINK_REAL_DLL " (see binkw32_proxy.log)");
        LogF("BinkOpen: initialization failed, returning NULL");
        return 0;
    }
    void* p = pBinkOpen;
    DWORD flags = (DWORD)(intptr_t)b;
    LogF("BinkOpen(%p,%p) flags=0x%08X ptr=%p", a, b, flags, p);

    char extractedName[MAX_PATH] = "";
    char mixFileName[MAX_PATH] = "";
    char* bikName = extractedName;

    if (flags & BINK_FLAG_IOPROCESSOR) {
        LogF("BinkOpen: BINKIOPROCESSOR mode, first param=%p (custom IO context)", a);

        // Try to get .bik filename from IHCore's exported function
        HMODULE ihCore = GetModuleHandleA("IHCore.dll");
        if (ihCore) {
            typedef const char* (__stdcall *GetBikNameFn)();
            GetBikNameFn fn = (GetBikNameFn)ResolveExtBikNameGetter(ihCore);
            if (fn) {
                const char* name = NULL;
                __try {
                    name = fn();
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    name = NULL;
                }
                if (name && IsValidStringA(name, MAX_PATH) && name[0]) {
                    strncpy_s(extractedName, sizeof(extractedName), name, _TRUNCATE);
                    bikName = extractedName;
                    LogF("BINKIOPROCESSOR: resolved filename from IHCore: %s", bikName);
                }
            }
        }

        if (!bikName[0]) {
            ExtractNameFromCCFileClass(a, extractedName, sizeof(extractedName));
            if (extractedName[0]) bikName = extractedName;
        }
    }

    if (flags & BINK_FLAG_FILEHANDLE) {
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

            // The handle's own directory first: the archive is often NOT
            // next to the proxy DLL (ExtractFileName keeps only the base
            // name, which is what the substitution key needs). Fall back to
            // the proxy directory, the historical behaviour.
            char handlePath[MAX_PATH] = "";
            DWORD pathLen = GetFinalPathNameByHandleA(hFile, handlePath,
                                                      MAX_PATH, FILE_NAME_NORMALIZED);
            if (pathLen > 0 && pathLen < MAX_PATH) {
                if (memcmp(handlePath, "\\\\?\\", 4) == 0) {
                    memmove(handlePath, handlePath + 4, strlen(handlePath + 4) + 1);
                }
            } else {
                handlePath[0] = '\0';
            }

            char bikInternal[MAX_PATH] = "";
            BOOL found = handlePath[0] &&
                         FindBikNameInMix(handlePath, pos, bikInternal, sizeof(bikInternal));
            if (!found && strlen(g_dllDir) + strlen(mixPath) < MAX_PATH) {
                char fullMixPath[MAX_PATH];
                _snprintf_s(fullMixPath, sizeof(fullMixPath), _TRUNCATE, "%s%s",
                            g_dllDir, mixPath);
                found = FindBikNameInMix(fullMixPath, pos, bikInternal, sizeof(bikInternal));
            }
            if (found) {
                LogF("Bik name from .mix: %s", bikInternal);
                strncpy_s(extractedName, sizeof(extractedName), bikInternal, _TRUNCATE);
                bikName = extractedName;
            } else {
                strncpy_s(extractedName, sizeof(extractedName), mixPath, _TRUNCATE);
                bikName = extractedName;
            }
        }

        LogCallStack(1);
    } else if (a && !(flags & BINK_FLAG_FROM_MEMORY) && !(flags & BINK_FLAG_IOPROCESSOR)) {
        // Only treat 'a' as filename string when no special flag is set.
        // BINK_FLAG_FROM_MEMORY: 'a' is a memory buffer pointer
        // BINK_FLAG_IOPROCESSOR: 'a' is a custom IO context (e.g. CCFileClass*)
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(a, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
            (mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            IsValidStringA((const char*)a, MAX_PATH)) {
            bikName = (char*)a;
        }
    }

    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkOpen->%p", (void*)r);
    if (r) {
        if (bikName && bikName[0]) LogF("BinkOpen resolved: %s", bikName);
        TrackVideo((void*)r, (bikName && bikName[0]) ? bikName : NULL, mixFileName[0] ? mixFileName : NULL);
    }
    return r;
}

intptr_t __stdcall sBinkOpenWithOptions(void* a, void* b, void* c) {
    if (!EnsureInitialized()) {
        // Give BinkGetError() something to report.
        SetProxyError("Proxy_Bink32w: could not load the real " BINK_REAL_DLL " (see binkw32_proxy.log)");
        LogF("BinkOpenWithOptions: initialization failed, returning NULL");
        return 0;
    }
    void* p = pBinkOpenWithOptions;
    LogF("BinkOpenWithOptions(%p,%p,%p)", a, b, c);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
    LogF("BinkOpenWithOptions->%p", (void*)r);
    if (r && a) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(a, &mbi, sizeof(mbi)) >= sizeof(mbi) &&
            (mbi.State & MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            IsValidStringA((const char*)a, MAX_PATH)) {
            const char* name = (const char*)a;
            TrackVideo((void*)r, name[0] ? name : NULL, NULL);
        } else {
            TrackVideo((void*)r, NULL, NULL);
        }
    }
    return r;
}

// ============================================================================
// Replacement-audio service
//
// Both helpers must be called with TrackLock held.
// ============================================================================

// Re-apply the volume/pan the game originally asked for. Called once the
// replacement player has been abandoned: without it the track we muted would
// stay muted for the rest of the video.
static void RestoreOriginalAudio(void* handle, VideoInfo* vi) {
    void* vol = pBinkSetVolume;
    if (vol && vi->volumeSet)
        CallSetVolume(vol, handle, vi->volumeTrack, vi->volumeValue);
    void* pan = pBinkSetPan;
    if (pan && vi->panSet)
        CallSetPan(pan, handle, vi->panValue0, vi->panValue1);
    // While the replacement owned the audio, sBinkSetSoundOnOff forwarded the
    // game's "sound on" as "off" to keep the real Bink track silent. Push the
    // game's actual request back or the movie stays silent after we hand over.
    BOOL soundRestored = vi->soundReqSet && vi->soundOn && pBinkSetSoundOnOff;
    if (soundRestored)
        ((void(__stdcall*)(void*, void*))pBinkSetSoundOnOff)(handle, (void*)1);
    if (vi->volumeSet || vi->panSet || soundRestored)
        LogF("Original Bink audio restored for %p", handle);
}

// Applies the accumulated playback requests to the replacement player: the
// game may pause the video and/or turn sound off, and either request must be
// able to hold the player without being cancelled by the other (BinkPause(0)
// must not resume a player the game silenced, SetSoundOnOff(1) must not
// resume a paused video). Idempotent — the player's own guards make repeat
// calls no-ops. Contract: called with the track lock HELD.
static void ApplyPlaybackState(VideoInfo* vi) {
    if (!vi || !vi->wavPlayer) return;
    BOOL wantPause = vi->pauseRequested || (vi->soundReqSet && !vi->soundOn);
    if (wantPause) {
        if (!vi->wavPlayer->paused) WavPlayerPause(vi->wavPlayer);
    } else {
        if (vi->wavPlayer->paused) WavPlayerResume(vi->wavPlayer);
    }
}

// Applies a BinkGoto that arrived before the replacement player existed, so
// the replacement audio starts at the frame the game asked for instead of at
// the beginning of the file. Idempotent — `seekPending` clears it.
// Contract: called with the track lock HELD.
static void ApplyPendingSeek(VideoInfo* vi) {
    if (!vi || !vi->seekPending) return;
    vi->seekPending = false;
    if (!vi->wavPlayer || !vi->seekFr || !vi->seekFrD) return;
    uint64_t sampleOffset64 = (uint64_t)vi->seekFrame *
                              vi->wavPlayer->format.nSamplesPerSec *
                              vi->seekFrD / vi->seekFr;
    DWORD sampleOffset = (sampleOffset64 > 0xFFFFFFFF) ? 0xFFFFFFFF : (DWORD)sampleOffset64;
    WavPlayerSeek(vi->wavPlayer, sampleOffset);
    LogF("Applied pending seek to frame %lu", (unsigned long)vi->seekFrame);
}

// Starts the replacement player for `vi`, retrying a bounded number of times
// before giving up, and applies a pause that arrived before the first
// frame created the player.
//
// Contract: called with the track lock HELD, returns with it HELD. Starting a
// player decodes a whole file, so the lock is dropped for that I/O and `vi` is
// re-resolved by handle afterwards — the array may have compacted.
static void ServiceVideoAudio(void* handle, VideoInfo* vi) {
    if (!vi) return;

    if (!vi->wavStarted && !vi->wavFailed && vi->wavPath[0]) {
        char path[MAX_PATH];
        strncpy_s(path, sizeof(path), vi->wavPath, _TRUNCATE);
        WavPlayer* pl = AllocPlayer();

        // Reserve before dropping the lock so a concurrent DoFrame on the same
        // handle cannot start a second player for this video.
        vi->wavStarted = true;

        TrackUnlock();
        BOOL started = (pl != NULL) && WavPlayerStart(pl, path);
        TrackLock();

        vi = FindVideoHeld(handle);
        if (!vi) {
            // The video was closed while we were decoding. FreePlayer blocks
            // (waveOutReset + drain), so the track lock is dropped for it too;
            // the contract still holds: the lock is re-taken before returning.
            if (pl) {
                TrackUnlock();
                FreePlayer(pl);
                TrackLock();
            }
            return;
        }

        if (started) {
            vi->wavPlayer = pl;
            ApplyPendingSeek(vi);
            LogF("Audio playback started: %s", path);
        } else {
            if (pl) {
                // FreePlayer blocks and must never run under the track lock.
                // Dropping the lock means `vi` has to be re-resolved: g_vids[]
                // may have compacted while we were tearing the slot down.
                TrackUnlock();
                FreePlayer(pl);
                TrackLock();
                vi = FindVideoHeld(handle);
                if (!vi) return;
            }
            vi->wavStarted = false;
            vi->wavAttempts++;
            if (vi->wavAttempts >= WAV_START_ATTEMPTS) {
                vi->wavFailed = true;
                LogF("Replacement audio failed to start for %s after %d attempt(s); "
                     "falling back to the original Bink audio",
                     vi->wavPath, vi->wavAttempts);
                RestoreOriginalAudio(handle, vi);
            } else {
                LogF("Replacement audio start attempt %d failed for %s (will retry)",
                     vi->wavAttempts, vi->wavPath);
            }
        }
    }

    // The stream died mid-clip: the device refused a buffer (waveOutWrite
    // failed) so WaveOutProc cleared `playing` while PCM data remains
    // (pcmPos < pcmSize). A clip that simply finished has pcmPos == pcmSize,
    // and a paused player keeps `paused` set — neither is a failure. Without
    // this the movie would stay silent for the rest of its runtime while the
    // proxy still believes the replacement owns the audio.
    if (vi->wavPlayer && !vi->wavPlayer->playing && !vi->wavPlayer->paused &&
        vi->wavPlayer->pcmPos < vi->wavPlayer->pcmSize) {
        WavPlayer* dead = vi->wavPlayer;
        DWORD pos = dead->pcmPos, size = dead->pcmSize;
        vi->wavPlayer = NULL;
        vi->wavFailed = true;
        LogF("Replacement audio stopped mid-clip for %s (%u/%u bytes); "
             "falling back to the original Bink audio",
             vi->wavPath, pos, size);
        RestoreOriginalAudio(handle, vi);
        TrackUnlock();
        FreePlayer(dead);
        TrackLock();
        vi = FindVideoHeld(handle);
        if (!vi) return;
    }

    ApplyPlaybackState(vi);
}

void __stdcall sBinkDoFrame(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDoFrame;
    if (g_logWait) LogF("BinkDoFrame(%p)", a);

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        ServiceVideoAudio(a, vi);
        TrackUnlock();
    }

    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDoFramePlane(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDoFramePlane;
    if (g_logWait) LogF("BinkDoFramePlane(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    if (g_logWait) LogF("BinkDoFramePlane->%p", (void*)r);
    return r;
}

void __stdcall sBinkNextFrame(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkNextFrame;
    if (g_logWait) LogF("BinkNextFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkWait(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkWait;
    if (g_logWait) LogF("BinkWait(%p)", a);
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

void __stdcall sBinkClose(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkClose;
    LogF("BinkClose(%p)", a);
    UntrackVideo(a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkPause(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkPause;
    int pause = (int)(intptr_t)b;
    LogF("BinkPause(%p, %d)", a, pause);
    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        // Remember the request — the player may not exist yet (before the
        // first frame). ServiceVideoAudio() applies it once it is created.
        vi->pauseRequested = (pause != 0);
        // Not a direct Resume(): a game that turned sound off must not have
        // its replacement player resumed by BinkPause(0).
        ApplyPlaybackState(vi);
        TrackUnlock();
    }
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

static intptr_t ForwardCopy(void* p, void* a, void* b, void* c, void* d,
                            void* e, void* f, void* g) {
    return ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g);
}

// Leaves sb->cs on every exit path, including the forward-fallback returns.
struct CsGuard {
    CRITICAL_SECTION* cs;
    explicit CsGuard(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
    ~CsGuard() { LeaveCriticalSection(cs); }
};

// Two-pass scaled blit, runs WITHOUT the track lock: the caller pinned `sb`
// with its own reference and took a snapshot of the source dimensions, so
// g_vids[] may be compacted concurrently without affecting anything here.
// sb->cs serializes concurrent scaled copies of the same video (the track
// lock used to do that, at the cost of stalling every other sBink* call).
static intptr_t CopyToBufferScaled(ScaleBufs* sb, uint32_t srcW, uint32_t srcHH,
                                   void* p, void* a, void* b, void* c, void* d,
                                   void* e, void* f, void* g) {
    int dstPitch = (int)(intptr_t)c;
    int dstHeight = (int)(intptr_t)d;
    int destX = (int)(intptr_t)e;
    int destY = (int)(intptr_t)f;
    int flags = (int)(intptr_t)g;
    int bpp = BppFromFlags(flags);

    // RA2/RA2YR only uses bpp=2 (RGB565). Skip scaling for other modes.
    if (!(bpp == 2 && dstPitch > 0 && dstHeight > 0 && destX >= 0 && destY >= 0))
        return ForwardCopy(p, a, b, c, d, e, f, g);

    int dstW = dstPitch / bpp;
    int needScale = (srcW > (uint32_t)dstW || srcHH > (uint32_t)dstHeight);
    if (!needScale)
        return ForwardCopy(p, a, b, c, d, e, f, g);

    CsGuard lock(&sb->cs);

    int srcPitch = (int)(srcW * bpp);
    srcPitch = (srcPitch + 15) & ~15;
    int srcH = (int)srcHH;
    SIZE_T requiredSize = (SIZE_T)srcPitch * srcH;

    if (!sb->tempBuf || sb->tempPitch != srcPitch || sb->tempHeight != srcH) {
        if (sb->tempBuf) VirtualFree(sb->tempBuf, 0, MEM_RELEASE);
        sb->tempBuf = VirtualAlloc(0, requiredSize, MEM_COMMIT, PAGE_READWRITE);
        if (sb->tempBuf) {
            sb->tempPitch = srcPitch;
            sb->tempHeight = srcH;
        } else {
            LogF("VirtualAlloc failed: %zu bytes (error %lu)", requiredSize, GetLastError());
            sb->tempPitch = 0;
            sb->tempHeight = 0;
        }
    }

    if (sb->tempBuf) {
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
        if (scaleW < 1 || scaleH < 1 ||
            offX < 0 || offY < 0 ||
            offX + scaleW > dstW || offY + scaleH > dstHeight) {
            // No valid destination rectangle. Hand the frame to
            // Bink's own scaler instead of returning a success code
            // for a buffer the proxy never wrote to.
            LogF("sBinkCopyToBuffer: scaled blit skipped (offX=%d offY=%d "
                 "scaleW=%d scaleH=%d dstW=%d dstH=%d)",
                 offX, offY, scaleW, scaleH, dstW, dstHeight);
            return ForwardCopy(p, a, b, c, d, e, f, g);
        }

        if (sb->tableW != scaleW || sb->tableH != scaleH) {
            int* newX = (int*)malloc(scaleW * sizeof(int));
            int* newY = (int*)malloc(scaleH * sizeof(int));
            if (newX && newY) {
                free(sb->lookupX);
                free(sb->lookupY);
                sb->lookupX = newX;
                sb->lookupY = newY;
                for (int x = 0; x < scaleW; x++)
                    sb->lookupX[x] = x * (int)srcW / scaleW;
                for (int y = 0; y < scaleH; y++)
                    sb->lookupY[y] = y * (int)srcHH / scaleH;
                sb->tableW = scaleW;
                sb->tableH = scaleH;
                LogF("Scaling %ux%u -> %dx%d (fit in %dx%d) at (%d,%d)",
                     (unsigned)srcW, (unsigned)srcHH, scaleW, scaleH, availW, availH, offX, offY);
            } else {
                LogF("Failed to allocate scale table: %dx%d + %dx%d bytes", scaleW, scaleH, scaleW, scaleH);
                free(newX);
                free(newY);
            }
        }

        if (!sb->lookupX || !sb->lookupY ||
            sb->tableW != scaleW || sb->tableH != scaleH) {
            // Without the lookup tables nothing would be blitted
            // into `b`, so fall back rather than report success.
            LogF("sBinkCopyToBuffer: scale table unavailable, using Bink scaler");
            return ForwardCopy(p, a, b, c, d, e, f, g);
        }

        intptr_t result = ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(
            a, sb->tempBuf, (void*)(intptr_t)srcPitch,
            (void*)(intptr_t)srcH, (void*)0, (void*)0, g);

        for (int y = 0; y < scaleH; y++) {
            int sy = sb->lookupY[y];
            const uint16_t* srcLine = (const uint16_t*)((const uint8_t*)sb->tempBuf + (SIZE_T)sy * srcPitch);
            uint16_t* dstLine = (uint16_t*)((uint8_t*)b + (SIZE_T)(offY + y) * dstPitch + (SIZE_T)offX * bpp);
            for (int x = 0; x < scaleW; x++) {
                dstLine[x] = srcLine[sb->lookupX[x]];
            }
        }

        return result;
    }

    return ForwardCopy(p, a, b, c, d, e, f, g);
}

intptr_t __stdcall sBinkCopyToBuffer(void* a, void* b, void* c, void* d, void* e, void* f, void* g) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkCopyToBuffer;
    if (!p) {
        // Not an anonymous failure: BinkCopyToBuffer is what hands the game
        // its pixels, so a null target means a black screen with no clue.
        // Record it where BinkGetError() will surface it.
        SetProxyError("BinkCopyToBuffer: real BinkCopyToBuffer unavailable");
        LogF("BinkCopyToBuffer(%p,...): real function unavailable, returning 0", a);
        return 0;
    }

    // Snapshot the dimensions, pin the scale scratch with an extra reference
    // and drop the track lock BEFORE the blit: CopyToBufferScaled waits on the
    // real BinkCopyToBuffer, and holding the lock would stall every other
    // sBink* entry point (they all take it). The reference keeps sb alive
    // even if UntrackVideo compacts g_vids[] in the meantime.
    ScaleBufs* sb = NULL;
    uint32_t srcW = 0, srcH = 0;
    TrackLock();
    VideoInfo* vi = FindVideoHeld(a);
    if (vi && vi->scale) {
        sb = vi->scale;
        ScaleBufsRef(sb);
        srcW = vi->width;
        srcH = vi->height;
    }
    TrackUnlock();

    if (!sb)
        return ForwardCopy(p, a, b, c, d, e, f, g);

    intptr_t r = CopyToBufferScaled(sb, srcW, srcH, p, a, b, c, d, e, f, g);
    ScaleBufsUnref(sb);
    return r;
}

intptr_t __stdcall sBinkCopyToBufferRect(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkCopyToBufferRect;
    LogF("BinkCopyToBufferRect(%p,...)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k) : 0;
    LogF("BinkCopyToBufferRect->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetRects(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetRects;
    LogF("BinkGetRects(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetRects->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGoto(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGoto;
    uint32_t frame = (uint32_t)(intptr_t)b;
    LogF("BinkGoto(%p, %u)", a, frame);
    // stdcall decoration (@12) is param-only, so returning the real DLL's
    // value changes no export name; dropping it forced callers that check
    // the result to see 0 (uninitialised EAX) on every seek.
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;

    // BinkGetSummary reads from disk — do it before taking the track lock
    // so a seek does not stall every other sBink* call.
    uint32_t fr = 0, frd = 0;
    if (pBinkGetSummary) {
        unsigned char summary[128];
        memset(summary, 0, sizeof(summary));
        // BINKSUMMARY is 124 bytes; SEH guards against overrun.
        __try {
            ((void(__stdcall*)(void*, void*))pBinkGetSummary)(a, summary);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            LogF("BinkGoto: BinkGetSummary faulted for %p", a);
        }
        fr = ReadU32(summary + 20);
        frd = ReadU32(summary + 24);
    }

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        if (vi->height > 0 && fr > 0 && frd > 0) {
            if (vi->wavPlayer) {
                uint64_t sampleOffset64 = (uint64_t)frame * vi->wavPlayer->format.nSamplesPerSec * frd / fr;
                DWORD sampleOffset = (sampleOffset64 > 0xFFFFFFFF) ? 0xFFFFFFFF : (DWORD)sampleOffset64;
                WavPlayerSeek(vi->wavPlayer, sampleOffset);
            } else {
                // The replacement player is created on the first frame, so a
                // Goto before that used to drop the seek and leave the audio
                // at the start of the file. Remember it instead.
                vi->seekPending = true;
                vi->seekFrame = (DWORD)frame;
                vi->seekFr = fr;
                vi->seekFrD = frd;
            }
        }
        TrackUnlock();
    }
    return r;
}

intptr_t __stdcall sBinkGetKeyFrame(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetKeyFrame;
    LogF("BinkGetKeyFrame(%p,%p,%p)", a, b, c);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
    LogF("BinkGetKeyFrame->%p", (void*)r);
    return r;
}

void __stdcall sBinkFreeGlobals() {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkFreeGlobals;
    LogF("BinkFreeGlobals()");
    if (p) ((void(__stdcall*)())p)();
}

void __stdcall sBinkGetPlatformInfo(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetPlatformInfo;
    LogF("BinkGetPlatformInfo(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetFrameBuffersInfo(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetFrameBuffersInfo;
    LogF("BinkGetFrameBuffersInfo(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRegisterFrameBuffers(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkRegisterFrameBuffers;
    LogF("BinkRegisterFrameBuffers(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetVideoOnOff(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetVideoOnOff;
    LogF("BinkSetVideoOnOff(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundOnOff(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetSoundOnOff;
    int on = (int)(intptr_t)b;
    BOOL muteBink = FALSE;

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        BOOL wavActive = vi->wavPath[0] && !vi->wavFailed;
        muteBink = wavActive && on;
        // Turning the game's sound off must stop the replacement too,
        // otherwise a "silent" movie keeps playing its WAV/OGG track. The
        // request is recorded so a later BinkPause(0) cannot resume a player
        // the game silenced, and RestoreOriginalAudio() can push "on" back
        // to the real DLL once the replacement is abandoned.
        vi->soundOn = on ? TRUE : FALSE;
        vi->soundReqSet = TRUE;
        ApplyPlaybackState(vi);
        TrackUnlock();
    }

    if (muteBink) {
        LogF("BinkSetSoundOnOff: muted (WAV replacement active)");
        if (p) ((void(__stdcall*)(void*,void*))p)(a, (void*)0);
        return;
    }
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

// ============================================================================
// BinkSetVolume/Pan adapters (see BINK_HAS_VOLUME_12 near the top)
//
//   sBinkSetVolume2 / sBinkSetPan2  short game API: (bnk, value)
//   sBinkSetVolume3 / sBinkSetPan    long game API: (bnk, trackid, value)
// ============================================================================

void __stdcall sBinkSetVolume2(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetVolume;
    BOOL wavActive = FALSE;
    void* track = p ? ResolveTrackId(a) : NULL;

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        // Remember what the game asked for so RestoreOriginalAudio() can put
        // it back if the replacement player has to be abandoned.
        vi->volumeSet = TRUE;
        vi->volumeTrack = track;
        vi->volumeValue = b;
        wavActive = vi->wavPath[0] && !vi->wavFailed;
        TrackUnlock();
    }

    if (wavActive) {
        LogF("BinkSetVolume2: muted (WAV replacement active)");
        if (p) CallSetVolume(p, a, track, NULL);
        return;
    }
    LogF("BinkSetVolume2(%p,%p)", a, b);
    if (p) CallSetVolume(p, a, track, b);
}

// Long game API: (bnk, trackid, volume) - what bink >= 1.2i imports.
void __stdcall sBinkSetVolume3(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetVolume;
    BOOL wavActive = FALSE;

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        // Kept for RestoreOriginalAudio(); see sBinkSetVolume2.
        vi->volumeSet = TRUE;
        vi->volumeTrack = b;
        vi->volumeValue = c;
        wavActive = vi->wavPath[0] && !vi->wavFailed;
        TrackUnlock();
    }

    if (wavActive) {
        LogF("BinkSetVolume3: muted (WAV replacement active)");
        if (p) CallSetVolume(p, a, b, NULL);
        return;
    }
    LogF("BinkSetVolume3(%p,%p,%p)", a, b, c);
    if (p) CallSetVolume(p, a, b, c);
}

// Long game API: (bnk, trackid, pan).
void __stdcall sBinkSetPan(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetPan;
    BOOL wavActive = FALSE;

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        // Kept for RestoreOriginalAudio(); see sBinkSetVolume2.
        vi->panSet = TRUE;
        vi->panValue0 = b;
        vi->panValue1 = c;
        wavActive = vi->wavPath[0] && !vi->wavFailed;
        TrackUnlock();
    }

    if (wavActive) {
        // Volume is forced to 0 while a replacement is active, so Bink's pan
        // is inaudible; the requested value is re-applied only if the
        // replacement is later abandoned (this mirrors the volume path).
        LogF("BinkSetPan: muted (WAV replacement active)");
        return;
    }
    LogF("BinkSetPan(%p,%p,%p)", a, b, c);
    if (p) CallSetPan(p, a, b, c);
}

// Short game API: (bnk, pan).
void __stdcall sBinkSetPan2(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetPan;
    BOOL wavActive = FALSE;
    void* track = p ? ResolveTrackId(a) : NULL;

    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        // Kept for RestoreOriginalAudio(); see sBinkSetVolume2.
        vi->panSet = TRUE;
        vi->panValue0 = track;
        vi->panValue1 = b;
        wavActive = vi->wavPath[0] && !vi->wavFailed;
        TrackUnlock();
    }

    if (wavActive) {
        LogF("BinkSetPan2: muted (WAV replacement active)");
        return;
    }
    LogF("BinkSetPan2(%p,%p)", a, b);
    if (p) CallSetPan(p, a, track, b);
}

void __stdcall sBinkSetSpeakerVolumes(void* a, void* b, void* c, void* d, void* e) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetSpeakerVolumes;
    LogF("BinkSetSpeakerVolumes(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sBinkService(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkService;
    if (g_logWait) LogF("BinkService(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkShouldSkip(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkShouldSkip;
    LogF("BinkShouldSkip(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkShouldSkip->%p", (void*)r);
    return r;
}

void __stdcall sBinkGetPalette(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetPalette;
    LogF("BinkGetPalette(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkControlBackgroundIO(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkControlBackgroundIO;
    LogF("BinkControlBackgroundIO(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkControlBackgroundIO->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkControlPlatformFeatures(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkControlPlatformFeatures;
    LogF("BinkControlPlatformFeatures(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkControlPlatformFeatures->%p", (void*)r);
    return r;
}

void __stdcall sBinkSetWillLoop(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetWillLoop;
    LogF("BinkSetWillLoop(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
    else LogF("BinkSetWillLoop: not available in this Bink version");
}

intptr_t __stdcall sBinkOpenTrack(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkOpenTrack;
    LogF("BinkOpenTrack(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkOpenTrack->%p", (void*)r);
    return r;
}

void __stdcall sBinkCloseTrack(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkCloseTrack;
    LogF("BinkCloseTrack(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetTrackData(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetTrackData;
    LogF("BinkGetTrackData(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackData->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetTrackType(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetTrackType;
    LogF("BinkGetTrackType(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetTrackMaxSize(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetTrackMaxSize;
    LogF("BinkGetTrackMaxSize(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackMaxSize->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkGetTrackID(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetTrackID;
    LogF("BinkGetTrackID(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkGetTrackID->%p", (void*)r);
    return r;
}

void __stdcall sBinkGetSummary(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetSummary;
    LogF("BinkGetSummary(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetRealtime(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkGetRealtime;
    LogF("BinkGetRealtime(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkSetFileOffset(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetFileOffset;
    LogF("BinkSetFileOffset(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundTrack8(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetSoundTrack;
    LogF("BinkSetSoundTrack8(%p,%p) arity=%d", a, b, g_soundTrackArity);
    if (!p) return;
    if (g_soundTrackArity == 4) {
        // The two generations differ in meaning, not only in argument count:
        // `@8` is (count, track list), `@4` stores one track index. Forward
        // the first requested track — forwarding `count` would store 2 as
        // "track 2" and pick a different track than the game asked for.
        DWORD track = 0;
        if (a && b) track = *(const DWORD*)b;
        ((void(__stdcall*)(void*))p)((void*)(uintptr_t)track);
    } else {
        ((void(__stdcall*)(void*,void*))p)(a, b);
    }
}

void __stdcall sBinkSetSoundTrack4(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetSoundTrack;
    LogF("BinkSetSoundTrack4(%p) arity=%d", a, g_soundTrackArity);
    if (!p) return;
    if (g_soundTrackArity == 8) {
        // Two-argument form: it walks the track list it is handed, so pass
        // one real entry — never (count>0, NULL), which it dereferences.
        void* track = a;
        ((void(__stdcall*)(void*,void*))p)((void*)1, &track);
    } else {
        ((void(__stdcall*)(void*))p)(a);
    }
}

void __stdcall sBinkSetIO(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetIO;
    LogF("BinkSetIO(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetFrameRate(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetFrameRate;
    LogF("BinkSetFrameRate(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSimulate(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetSimulate;
    LogF("BinkSetSimulate(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetIOSize(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetIOSize;
    LogF("BinkSetIOSize(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkSetSoundSystem(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetSoundSystem;
    LogF("BinkSetSoundSystem(%p,%p)", a, b);
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkOpenDirectSound(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkOpenDirectSound;
    LogF("BinkOpenDirectSound(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenWaveOut(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkOpenWaveOut;
    LogF("BinkOpenWaveOut(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenMiles(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkOpenMiles;
    LogF("BinkOpenMiles(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDX8SurfaceType(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDX8SurfaceType;
    LogF("BinkDX8SurfaceType(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkDX8SurfaceType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkDX9SurfaceType(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDX9SurfaceType;
    LogF("BinkDX9SurfaceType(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkDX9SurfaceType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkBufferOpen(void* a, void* b, void* c, void* d) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferOpen;
    LogF("BinkBufferOpen(%p,%p,%p,%p)", a, b, c, d);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d) : 0;
    LogF("BinkBufferOpen->%p", (void*)r);
    return r;
}

void __stdcall sBinkBufferSetHWND(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferSetHWND;
    LogF("BinkBufferSetHWND(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sBinkDDSurfaceType(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDDSurfaceType;
    LogF("BinkDDSurfaceType(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkDDSurfaceType->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkIsSoftwareCursor(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkIsSoftwareCursor;
    LogF("BinkIsSoftwareCursor(%p,%p)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkIsSoftwareCursor->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkCheckCursor(void* a, void* b, void* c, void* d, void* e) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkCheckCursor;
    LogF("BinkCheckCursor(%p,%p,...)", a, b);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e) : 0;
    LogF("BinkCheckCursor->%p", (void*)r);
    return r;
}

void __stdcall sBinkBufferSetDirectDraw(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferSetDirectDraw;
    LogF("BinkBufferSetDirectDraw(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkBufferClose(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferClose;
    LogF("BinkBufferClose(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferLock(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferLock;
    LogF("BinkBufferLock(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferUnlock(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferUnlock;
    LogF("BinkBufferUnlock(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferSetResolution(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferSetResolution;
    LogF("BinkBufferSetResolution(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferCheckWinPos(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferCheckWinPos;
    LogF("BinkBufferCheckWinPos(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetOffset(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferSetOffset;
    LogF("BinkBufferSetOffset(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferBlit(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferBlit;
    LogF("BinkBufferBlit(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetScale(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferSetScale;
    LogF("BinkBufferSetScale(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

intptr_t __stdcall sBinkBufferGetDescription(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferGetDescription;
    LogF("BinkBufferGetDescription(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    LogF("BinkBufferGetDescription->%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkBufferGetError() {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferGetError;
    LogF("BinkBufferGetError()");
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkBufferGetError->%p", (void*)r);
    return r;
}

void __stdcall sBinkBufferClear(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkBufferClear;
    LogF("BinkBufferClear(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRestoreCursor(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkRestoreCursor;
    LogF("BinkRestoreCursor(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkStartAsyncThread(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkStartAsyncThread;
    LogF("BinkStartAsyncThread(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkDoFrameAsync(void* a, void* b, void* c) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDoFrameAsync;
    LogF("BinkDoFrameAsync(%p,%p,%p)", a, b, c);
    // This is a BinkDoFrame equivalent, so it needs the same
    // replacement-audio start block.
    VideoInfo* vi = FindVideoLocked(a);
    if (vi) {
        ServiceVideoAudio(a, vi);
        TrackUnlock();
    }
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkDoFrameAsyncWait(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkDoFrameAsyncWait;
    LogF("BinkDoFrameAsyncWait(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRequestStopAsyncThread(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkRequestStopAsyncThread;
    LogF("BinkRequestStopAsyncThread(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkWaitStopAsyncThread(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkWaitStopAsyncThread;
    LogF("BinkWaitStopAsyncThread(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

// Long game API: (bnk, trackid, mix_bins, total).
void __stdcall sBinkSetMixBins(void* a, void* b, void* c, void* d) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetMixBins;
    LogF("BinkSetMixBins(%p,%p,...)", a, b);
    if (p) CallSetMixBins(p, a, b, c, d);
}

// Short game API: (bnk, mix_bins) - bink <=1.2c has no track id and no
// entry count, hence the NULL `total` for the long real DLL.
void __stdcall sBinkSetMixBins2(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetMixBins;
    void* track = p ? ResolveTrackId(a) : NULL;
    LogF("BinkSetMixBins2(%p,%p)", a, b);
    if (p) CallSetMixBins(p, a, track, b, NULL);
}

void __stdcall sBinkSetMixBinVolumes(void* a, void* b, void* c, void* d, void* e) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetMixBinVolumes;
    LogF("BinkSetMixBinVolumes(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sExpandBink(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m, void* n) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pExpandBink;
    LogF("ExpandBink(%p,%p,...)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
}

void __stdcall sExpandBundleSizes(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pExpandBundleSizes;
    LogF("ExpandBundleSizes(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sRADSetMemory(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pRADSetMemory;
    LogF("RADSetMemory(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetMemory(void* a, void* b) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pBinkSetMemory;
    LogF("BinkSetMemory(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sRADTimerRead() {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pRADTimerRead;
    if (g_logWait) LogF("RADTimerRead()");
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    if (g_logWait) LogF("RADTimerRead->%p", (void*)r);
    return r;
}

intptr_t __stdcall sradmalloc(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pradmalloc;
    if (g_logWait) LogF("radmalloc(%p)", a);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
    if (g_logWait) LogF("radmalloc->%p", (void*)r);
    return r;
}

void __stdcall sradfree(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pradfree;
    if (g_logWait) LogF("radfree(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

// ============================================================================
// YUV blit proxy stubs — generated via macro to eliminate boilerplate
//
// Two variants: 12-arg (exported @48) and 13-arg (exported @52). RAD's DLLs
// decorate these blitters anywhere between @36 and @60 depending on the
// generation (probed into g_yuvArity[] at load), and the callee pops exactly
// what its own decoration promises. Forwarding the arity the *caller* used
// would therefore leave the caller's stack unbalanced (AV on return), so the
// forward pushes exactly what the real export pops: trailing arguments the
// caller never supplied go as zero, leading arguments beyond what the callee
// takes are dropped. Unknown arity -> the call is dropped instead of guessed.
// ============================================================================

#define YUV_T9  void*,void*,void*,void*,void*,void*,void*,void*,void*
#define YUV_T10 YUV_T9,void*
#define YUV_T11 YUV_T10,void*
#define YUV_T12 YUV_T11,void*
#define YUV_T13 YUV_T12,void*
#define YUV_T14 YUV_T13,void*
#define YUV_T15 YUV_T14,void*

#define YUV_FWD_9(fn)  ((void(__stdcall*)(YUV_T9))fn)(a,b,c,d,e,f,g,h,i)
#define YUV_FWD_10(fn) ((void(__stdcall*)(YUV_T10))fn)(a,b,c,d,e,f,g,h,i,j)
#define YUV_FWD_11(fn) ((void(__stdcall*)(YUV_T11))fn)(a,b,c,d,e,f,g,h,i,j,k)
#define YUV_FWD_12(fn) ((void(__stdcall*)(YUV_T12))fn)(a,b,c,d,e,f,g,h,i,j,k,l)
#define YUV_FWD_13(fn) ((void(__stdcall*)(YUV_T13))fn)(a,b,c,d,e,f,g,h,i,j,k,l,m)
#define YUV_FWD_14(fn) ((void(__stdcall*)(YUV_T14))fn)(a,b,c,d,e,f,g,h,i,j,k,l,m,(void*)0)
#define YUV_FWD_15(fn) ((void(__stdcall*)(YUV_T15))fn)(a,b,c,d,e,f,g,h,i,j,k,l,m,(void*)0,(void*)0)

// A 12-arg stub has no `m`, so the wider forwards pad from `l` instead.
#define YUV_FWD_13_12(fn) ((void(__stdcall*)(YUV_T13))fn)(a,b,c,d,e,f,g,h,i,j,k,l,(void*)0)
#define YUV_FWD_14_12(fn) ((void(__stdcall*)(YUV_T14))fn)(a,b,c,d,e,f,g,h,i,j,k,l,(void*)0,(void*)0)
#define YUV_FWD_15_12(fn) ((void(__stdcall*)(YUV_T15))fn)(a,b,c,d,e,f,g,h,i,j,k,l,(void*)0,(void*)0,(void*)0)

// A 12-arg stub receives parameters a..l; a 13-arg one a..m.
#define YUV_DISPATCH_12(fn, nm) \
    switch (arity) { \
    case 36: YUV_FWD_9(fn); break; \
    case 40: YUV_FWD_10(fn); break; \
    case 44: YUV_FWD_11(fn); break; \
    case 48: YUV_FWD_12(fn); break; \
    case 52: YUV_FWD_13_12(fn); break; \
    case 56: YUV_FWD_14_12(fn); break; \
    case 60: YUV_FWD_15_12(fn); break; \
    default: LogF("YUV %s: unknown real arity (%d), call dropped", nm, arity); break; \
    }

#define YUV_DISPATCH_13(fn, nm) \
    switch (arity) { \
    case 36: YUV_FWD_9(fn); break; \
    case 40: YUV_FWD_10(fn); break; \
    case 44: YUV_FWD_11(fn); break; \
    case 48: YUV_FWD_12(fn); break; \
    case 52: YUV_FWD_13(fn); break; \
    case 56: YUV_FWD_14(fn); break; \
    case 60: YUV_FWD_15(fn); break; \
    default: LogF("YUV %s: unknown real arity (%d), call dropped", nm, arity); break; \
    }

#define YUV_BLIT_12(name, id) \
void __stdcall s##name(void* a, void* b, void* c, void* d, void* e, void* f, \
    void* g, void* h, void* i, void* j, void* k, void* l) { \
    EnsureInitialized(); \
    void* p = p##name; \
    if (!p) return; \
    int arity = g_yuvArity[id]; \
    YUV_DISPATCH_12(p, #name) \
}

#define YUV_BLIT_13(name, id) \
void __stdcall s##name(void* a, void* b, void* c, void* d, void* e, void* f, \
    void* g, void* h, void* i, void* j, void* k, void* l, void* m) { \
    EnsureInitialized(); \
    void* p = p##name; \
    if (!p) return; \
    int arity = g_yuvArity[id]; \
    YUV_DISPATCH_13(p, #name) \
}

void __stdcall sYUV_init(void* a) {
    // A stub may be reached before the first BinkOpen; ensure the real
    // DLL is loaded instead of silently dropping the call.
    EnsureInitialized();
    void* p = pYUV_init;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

YUV_BLIT_13(YUV_blit_16a1bpp, YUV_A_16a1bpp)
YUV_BLIT_13(YUV_blit_16a1bpp_mask, YUV_A_16a1bpp_mask)
YUV_BLIT_13(YUV_blit_16a4bpp, YUV_A_16a4bpp)
YUV_BLIT_13(YUV_blit_16a4bpp_mask, YUV_A_16a4bpp_mask)
YUV_BLIT_12(YUV_blit_16bpp, YUV_A_16bpp)
YUV_BLIT_12(YUV_blit_16bpp_mask, YUV_A_16bpp_mask)
YUV_BLIT_12(YUV_blit_24bpp, YUV_A_24bpp)
YUV_BLIT_12(YUV_blit_24bpp_mask, YUV_A_24bpp_mask)
YUV_BLIT_12(YUV_blit_24rbpp, YUV_A_24rbpp)
YUV_BLIT_12(YUV_blit_24rbpp_mask, YUV_A_24rbpp_mask)
YUV_BLIT_13(YUV_blit_32abpp, YUV_A_32abpp)
YUV_BLIT_13(YUV_blit_32abpp_mask, YUV_A_32abpp_mask)
YUV_BLIT_12(YUV_blit_32bpp, YUV_A_32bpp)
YUV_BLIT_12(YUV_blit_32bpp_mask, YUV_A_32bpp_mask)
YUV_BLIT_13(YUV_blit_32rabpp, YUV_A_32rabpp)
YUV_BLIT_13(YUV_blit_32rabpp_mask, YUV_A_32rabpp_mask)
YUV_BLIT_12(YUV_blit_32rbpp, YUV_A_32rbpp)
YUV_BLIT_12(YUV_blit_32rbpp_mask, YUV_A_32rbpp_mask)
YUV_BLIT_12(YUV_blit_UYVY, YUV_A_UYVY)
YUV_BLIT_12(YUV_blit_UYVY_mask, YUV_A_UYVY_mask)
YUV_BLIT_12(YUV_blit_YUY2, YUV_A_YUY2)
YUV_BLIT_12(YUV_blit_YUY2_mask, YUV_A_YUY2_mask)
YUV_BLIT_13(YUV_blit_YV12, YUV_A_YV12)

} /* extern "C" */
