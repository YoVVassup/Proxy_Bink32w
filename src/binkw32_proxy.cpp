#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static HMODULE g_hR = NULL;

#define D(n) static void* p##n = NULL;
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
D(BinkSetFileOffset) D(BinkSetSoundTrack8) D(BinkSetSoundTrack4)
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
#undef D

static HANDLE g_log = INVALID_HANDLE_VALUE;
static BOOL g_logEnabled = TRUE;
static char g_dllDir[MAX_PATH] = {0};

static void InitLog() {
    char env[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("BINK_PROXY_LOG", env, MAX_PATH)) {
        if (!env[0] || lstrcmpA(env, "0") == 0) { g_logEnabled = FALSE; return; }
        g_log = CreateFileA(env, GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        return;
    }
    char nologPath[MAX_PATH];
    lstrcpynA(nologPath, g_dllDir, MAX_PATH);
    lstrcatA(nologPath, "binkw32.nolog");
    if (GetFileAttributesA(nologPath) != INVALID_FILE_ATTRIBUTES) { g_logEnabled = FALSE; return; }
    char logPath[MAX_PATH];
    lstrcpynA(logPath, g_dllDir, MAX_PATH);
    lstrcatA(logPath, "binkw32_proxy.log");
    g_log = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void Log(const char* msg) {
    if (!g_logEnabled) return;
    if (g_log == INVALID_HANDLE_VALUE) InitLog();
    if (g_log != INVALID_HANDLE_VALUE) {
        static BOOL logged_header = FALSE;
        if (!logged_header) {
            DWORD bw;
            const char* header =
                "=== Proxy_Bink32w v1.0.1 ===\r\n"
#ifdef BINK_10Q
                "Target: Bink 1.0q\r\n"
#else
                "Target: Bink 1.9u\r\n"
#endif
                "\r\n";
            WriteFile(g_log, header, (DWORD)strlen(header), &bw, NULL);
            logged_header = TRUE;
        }
        DWORD bw;
        SetFilePointer(g_log, 0, NULL, FILE_END);
        WriteFile(g_log, msg, (DWORD)strlen(msg), &bw, NULL);
        WriteFile(g_log, "\r\n", 2, &bw, NULL);
    }
}

static void LogF(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

static BOOL LoadDll() {
    if (g_hR) return TRUE;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (!slash) return FALSE;
    *(slash + 1) = 0;

    char dllPath[MAX_PATH];
#ifdef BINK_10Q
    snprintf(dllPath, MAX_PATH, "%sbinkw32_1.0q.dll", exePath);
#else
    snprintf(dllPath, MAX_PATH, "%sbinkw32_1.9u.dll", exePath);
#endif

    g_hR = LoadLibraryA(dllPath);
    if (!g_hR) {
        LogF("FAILED to load real DLL: %s (error %lu)", dllPath, GetLastError());
        char msg[MAX_PATH + 64];
        wsprintfA(msg, "Proxy_Bink32w v1.0.1\nFailed to load real Bink DLL:\n%s", dllPath);
        MessageBoxA(NULL, msg, "binkw32.dll", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    LogF("Real DLL loaded: %s", dllPath);

#define L(n, o) p##n = (void*)GetProcAddress(g_hR, (LPCSTR)o)

#ifdef BINK_10Q
    L(BinkBufferBlit,1);          L(BinkBufferCheckWinPos,2);
    L(BinkBufferClear,3);         L(BinkBufferClose,4);
    L(BinkBufferGetDescription,5); L(BinkBufferGetError,6);
    L(BinkBufferLock,7);          L(BinkBufferOpen,8);
    L(BinkBufferSetDirectDraw,9); L(BinkBufferSetHWND,10);
    L(BinkBufferSetOffset,11);    L(BinkBufferSetResolution,12);
    L(BinkBufferSetScale,13);     L(BinkBufferUnlock,14);
    L(BinkCheckCursor,15);        L(BinkClose,16);
    L(BinkCloseTrack,17);         L(BinkCopyToBuffer,18);
    L(BinkDDSurfaceType,19);      L(BinkDoFrame,20);
    L(BinkGetError,21);           L(BinkGetKeyFrame,22);
    L(BinkGetRealtime,23);        L(BinkGetRects,24);
    L(BinkGetSummary,25);         L(BinkGetTrackData,26);
    L(BinkGetTrackID,27);         L(BinkGetTrackMaxSize,28);
    L(BinkGetTrackType,29);       L(BinkGoto,30);
    L(BinkIsSoftwareCursor,31);   L(BinkLogoAddress,32);
    L(BinkNextFrame,33);          L(BinkOpen,34);
    L(BinkOpenDirectSound,35);    L(BinkOpenMiles,36);
    L(BinkOpenTrack,37);          L(BinkOpenWaveOut,38);
    L(BinkPause,39);              L(BinkRestoreCursor,40);
    L(BinkService,41);            L(BinkSetError,42);
    L(BinkSetFrameRate,43);       L(BinkSetIO,44);
    L(BinkSetIOSize,45);          L(BinkSetPan,46);
    L(BinkSetSimulate,47);        L(BinkSetSoundOnOff,48);
    L(BinkSetSoundSystem,49);     L(BinkSetSoundTrack8,50);
    L(BinkSetVideoOnOff,51);      L(BinkSetVolume,52);
    L(BinkWait,53);
    L(ExpandBink,54);             L(ExpandBundleSizes,55);
    L(RADSetMemory,56);           L(RADTimerRead,57);
    L(YUV_blit_16a1bpp,58);       L(YUV_blit_16a1bpp_mask,59);
    L(YUV_blit_16a4bpp,60);       L(YUV_blit_16a4bpp_mask,61);
    L(YUV_blit_16bpp,62);         L(YUV_blit_16bpp_mask,63);
    L(YUV_blit_24bpp,64);         L(YUV_blit_24bpp_mask,65);
    L(YUV_blit_24rbpp,66);        L(YUV_blit_24rbpp_mask,67);
    L(YUV_blit_32abpp,68);        L(YUV_blit_32abpp_mask,69);
    L(YUV_blit_32bpp,70);         L(YUV_blit_32bpp_mask,71);
    L(YUV_blit_32rabpp,72);       L(YUV_blit_32rabpp_mask,73);
    L(YUV_blit_32rbpp,74);        L(YUV_blit_32rbpp_mask,75);
    L(YUV_blit_UYVY,76);          L(YUV_blit_UYVY_mask,77);
    L(YUV_blit_YUY2,78);          L(YUV_blit_YUY2_mask,79);
    L(YUV_blit_YV12,80);          L(YUV_init,81);
    L(radfree,82);                L(radmalloc,83);
#else
    L(BinkBufferBlit,1);          L(BinkBufferCheckWinPos,2);
    L(BinkBufferClear,3);         L(BinkBufferClose,4);
    L(BinkBufferGetDescription,5); L(BinkBufferGetError,6);
    L(BinkBufferLock,7);          L(BinkBufferOpen,8);
    L(BinkBufferSetDirectDraw,9); L(BinkBufferSetHWND,10);
    L(BinkBufferSetOffset,11);    L(BinkBufferSetResolution,12);
    L(BinkBufferSetScale,13);     L(BinkBufferUnlock,14);
    L(BinkCheckCursor,15);        L(BinkClose,16);
    L(BinkCloseTrack,17);         L(BinkControlBackgroundIO,18);
    L(BinkControlPlatformFeatures,19); L(BinkCopyToBuffer,20);
    L(BinkCopyToBufferRect,21);   L(BinkDDSurfaceType,22);
    L(BinkDX8SurfaceType,23);     L(BinkDX9SurfaceType,24);
    L(BinkDoFrame,25);            L(BinkDoFrameAsync,26);
    L(BinkDoFrameAsyncWait,27);   L(BinkDoFramePlane,28);
    L(BinkGetError,29);           L(BinkGetFrameBuffersInfo,30);
    L(BinkGetKeyFrame,31);        L(BinkGetPalette,32);
    L(BinkGetRealtime,33);        L(BinkGetRects,34);
    L(BinkGetSummary,35);         L(BinkGetTrackData,36);
    L(BinkGetTrackID,37);         L(BinkGetTrackMaxSize,38);
    L(BinkGetTrackType,39);       L(BinkGoto,40);
    L(BinkIsSoftwareCursor,41);   L(BinkLogoAddress,42);
    L(BinkNextFrame,43);          L(BinkOpen,44);
    L(BinkOpenDirectSound,45);    L(BinkOpenMiles,46);
    L(BinkOpenTrack,47);          L(BinkOpenWaveOut,48);
    L(BinkPause,49);              L(BinkRegisterFrameBuffers,50);
    L(BinkRequestStopAsyncThread,51); L(BinkRestoreCursor,52);
    L(BinkService,53);            L(BinkSetError,54);
    L(BinkSetFrameRate,55);       L(BinkSetIO,56);
    L(BinkSetIOSize,57);          L(BinkSetMemory,58);
    L(BinkSetMixBinVolumes,59);   L(BinkSetMixBins,60);
    L(BinkSetPan,61);             L(BinkSetSimulate,62);
    L(BinkSetSoundOnOff,63);      L(BinkSetSoundSystem,64);
    L(BinkSetSoundTrack8,65);     L(BinkSetVideoOnOff,66);
    L(BinkSetVolume,67);          L(BinkSetWillLoop,68);
    L(BinkShouldSkip,69);         L(BinkStartAsyncThread,70);
    L(BinkWait,71);               L(BinkWaitStopAsyncThread,72);
    L(RADTimerRead,73);
#endif

#undef L
    LogF("Proxied functions resolved: pBinkOpen=%p pBinkDoFrame=%p pBinkClose=%p pBinkWait=%p",
         pBinkOpen, pBinkDoFrame, pBinkClose, pBinkWait);
    return TRUE;
}

struct VideoInfo {
    void* handle;
    uint32_t width;
    uint32_t height;
    void* tempBuf;
    int tempPitch;
    int tempHeight;
};

#define MAX_TRACKED 32
static VideoInfo g_vids[MAX_TRACKED];
static int g_vidCount = 0;

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(h);
        {
            char dllPath[MAX_PATH];
            GetModuleFileNameA(h, dllPath, MAX_PATH);
            char* slash = strrchr(dllPath, '\\');
            if (slash) { *(slash + 1) = 0; lstrcpynA(g_dllDir, dllPath, MAX_PATH); }
        }
        LoadDll();
        break;
    case DLL_PROCESS_DETACH:
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].tempBuf) VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE);
        }
        g_vidCount = 0;
        if (g_hR) { FreeLibrary(g_hR); g_hR = NULL; }
        if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
        break;
    }
    return TRUE;
}

static void TrackVideo(void* h) {
    if (!h || !pBinkGetSummary) return;
    unsigned char summary[512];
    memset(summary, 0, sizeof(summary));
    ((void(__stdcall*)(void*, void*))pBinkGetSummary)(h, summary);
    uint32_t w = *(uint32_t*)(summary);
    uint32_t hv = *(uint32_t*)(summary + 4);
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
        g_vidCount++;
        LogF("Tracked video: %p %ux%u", h, w, hv);
    }
}

static void UntrackVideo(void* h) {
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) {
            if (g_vids[i].tempBuf) VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE);
            g_vids[i] = g_vids[g_vidCount - 1];
            g_vidCount--;
            return;
        }
    }
}

static VideoInfo* FindVideo(void* h) {
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) return &g_vids[i];
    }
    return 0;
}

static int BppFromFlags(int flags) {
    int st = flags & 7;
    if (st == 0) return 3;
    if (st <= 4) return 2;
    return 4;
}

static void ScaleNN(const uint8_t* src, int sw, int sh, int sp,
                     uint8_t* dst, int dw, int dh, int dp, int bpp) {
    if (dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        if (sy >= sh) sy = sh - 1;
        const uint8_t* sr = src + sy * sp;
        uint8_t* dr = dst + y * dp;
        for (int x = 0; x < dw; x++) {
            int sx = x * sw / dw;
            if (sx >= sw) sx = sw - 1;
            memcpy(dr + x * bpp, sr + sx * bpp, bpp);
        }
    }
}

static void ScaleBilinear(const uint8_t* src, int sw, int sh, int sp,
                           uint8_t* dst, int dw, int dh, int dp, int bpp) {
    if (dw <= 0 || dh <= 0) return;

    if (bpp == 2) {
        for (int y = 0; y < dh; y++) {
            int sy = y * sh / dh;
            if (sy >= sh) sy = sh - 1;
            const uint8_t* sr = src + sy * sp;
            uint8_t* dr = dst + y * dp;
            for (int x = 0; x < dw; x++) {
                int sx = x * sw / dw;
                if (sx >= sw) sx = sw - 1;
                memcpy(dr + x * 2, sr + sx * 2, 2);
            }
        }
    } else if (bpp == 4) {
        for (int y = 0; y < dh; y++) {
            int sy = y * sh / dh;
            int sy2 = sy + 1 < sh ? sy + 1 : sy;
            int fy = ((y * sh) % dh) * 256 / dh;
            int ify = 256 - fy;

            const uint8_t* r0 = src + sy * sp;
            const uint8_t* r1 = src + sy2 * sp;
            uint8_t* dr = dst + y * dp;

            for (int x = 0; x < dw; x++) {
                int sx = x * sw / dw;
                int sx2 = sx + 1 < sw ? sx + 1 : sx;
                int fx = ((x * sw) % dw) * 256 / dw;
                int ifx = 256 - fx;

                for (int c = 0; c < 4; c++) {
                    int p00 = r0[sx * 4 + c];
                    int p10 = r1[sx * 4 + c];
                    int p01 = r0[sx2 * 4 + c];
                    int p11 = r1[sx2 * 4 + c];

                    int top = p00 * ifx + p10 * fx;
                    int bot = p01 * ifx + p11 * fx;
                    int val = top * ify + bot * fy;

                    dr[x * 4 + c] = (uint8_t)((val + 32768) >> 16);
                }
            }
        }
    } else {
        for (int y = 0; y < dh; y++) {
            int sy = y * sh / dh;
            if (sy >= sh) sy = sh - 1;
            const uint8_t* sr = src + sy * sp;
            uint8_t* dr = dst + y * dp;
            for (int x = 0; x < dw; x++) {
                int sx = x * sw / dw;
                if (sx >= sw) sx = sw - 1;
                memcpy(dr + x * bpp, sr + sx * bpp, bpp);
            }
        }
    }
}

extern "C" {

intptr_t __stdcall sBinkLogoAddress() {
    void* p = pBinkLogoAddress;
    return p ? ((intptr_t(__stdcall*)())p)() : 0;
}

void __stdcall sBinkSetError(void* a) {
    void* p = pBinkSetError;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetError() {
    void* p = pBinkGetError;
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkGetError()=%p", (void*)r);
    return r;
}

intptr_t __stdcall sBinkOpen(void* a, void* b) {
    void* p = pBinkOpen;
    LogF("BinkOpen(%p,%p) ptr=%p", a, b, p);
    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkOpen->%p", (void*)r);
    if (r) TrackVideo((void*)r);
    return r;
}

intptr_t __stdcall sBinkOpenWithOptions(void* a, void* b, void* c) {
    void* p = pBinkOpenWithOptions;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
}

void __stdcall sBinkDoFrame(void* a) {
    void* p = pBinkDoFrame;
    LogF("BinkDoFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDoFramePlane(void* a, void* b) {
    void* p = pBinkDoFramePlane;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkNextFrame(void* a) {
    void* p = pBinkNextFrame;
    LogF("BinkNextFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkWait(void* a) {
    void* p = pBinkWait;
    LogF("BinkWait(%p)", a);
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
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkCopyToBuffer(void* a, void* b, void* c, void* d, void* e, void* f, void* g) {
    void* p = pBinkCopyToBuffer;
    LogF("BinkCopyToBuffer(%p,...)", a);
    if (!p) return 0;

    VideoInfo* vi = FindVideo(a);
    if (vi) {
        int dstPitch = (int)(intptr_t)c;
        int dstHeight = (int)(intptr_t)d;
        int destX = (int)(intptr_t)e;
        int destY = (int)(intptr_t)f;
        int flags = (int)(intptr_t)g;
        int bpp = BppFromFlags(flags);

        if (bpp > 0 && dstPitch > 0 && dstHeight > 0) {
            int dstW = dstPitch / bpp;
            int needScale = (vi->width > (uint32_t)dstW || vi->height > (uint32_t)dstHeight);

                if (needScale) {
                int srcPitch = vi->width * bpp;
                srcPitch = (srcPitch + 15) & ~15;
                int srcH = vi->height;
                SIZE_T requiredSize = (SIZE_T)srcPitch * srcH;

                if (!vi->tempBuf || vi->tempPitch != srcPitch || vi->tempHeight != srcH) {
                    if (vi->tempBuf) VirtualFree(vi->tempBuf, 0, MEM_RELEASE);
                    vi->tempBuf = VirtualAlloc(0, requiredSize, MEM_COMMIT, PAGE_READWRITE);
                    vi->tempPitch = srcPitch;
                    vi->tempHeight = srcH;
                }

                if (vi->tempBuf) {
                    LogF("Scaling %ux%u -> %dx%d (bpp=%d)", vi->width, vi->height, dstW, dstHeight, bpp);

                    intptr_t result = ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(
                        a, vi->tempBuf, (void*)(intptr_t)srcPitch,
                        (void*)(intptr_t)srcH, (void*)0, (void*)0, g);

                    ScaleBilinear((const uint8_t*)vi->tempBuf, vi->width, vi->height, srcPitch,
                            (uint8_t*)b + destY * dstPitch + destX * bpp,
                            dstW - destX, dstHeight - destY, dstPitch, bpp);

                    return result;
                }
            }
        }
    }

    return ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g);
}

intptr_t __stdcall sBinkCopyToBufferRect(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k) {
    void* p = pBinkCopyToBufferRect;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k) : 0;
}

intptr_t __stdcall sBinkGetRects(void* a, void* b) {
    void* p = pBinkGetRects;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkGoto(void* a, void* b, void* c) {
    void* p = pBinkGoto;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

intptr_t __stdcall sBinkGetKeyFrame(void* a, void* b, void* c) {
    void* p = pBinkGetKeyFrame;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
}

void __stdcall sBinkFreeGlobals() {
    void* p = pBinkFreeGlobals;
    if (p) ((void(__stdcall*)())p)();
}

void __stdcall sBinkGetPlatformInfo(void* a, void* b) {
    void* p = pBinkGetPlatformInfo;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetFrameBuffersInfo(void* a, void* b) {
    void* p = pBinkGetFrameBuffersInfo;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRegisterFrameBuffers(void* a, void* b) {
    void* p = pBinkRegisterFrameBuffers;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetVideoOnOff(void* a, void* b) {
    void* p = pBinkSetVideoOnOff;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundOnOff(void* a, void* b) {
    void* p = pBinkSetSoundOnOff;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetVolume(void* a, void* b, void* c) {
    void* p = pBinkSetVolume;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

#ifdef BINK_10Q
void __stdcall sBinkSetVolume2(void* a, void* b) {
    void* p = pBinkSetVolume;
    LogF("BinkSetVolume2(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}
void __stdcall sBinkSetPan(void* a, void* b, void* c) {
    void* p = pBinkSetPan;
    LogF("BinkSetPan(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}
#else
void __stdcall sBinkSetVolume2(void* a, void* b) {
    void* p = pBinkSetVolume;
    LogF("BinkSetVolume2(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, 0);
}
void __stdcall sBinkSetPan(void* a, void* b, void* c) {
    void* p = pBinkSetPan;
    LogF("BinkSetPan(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}
#endif

void __stdcall sBinkSetSpeakerVolumes(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkSetSpeakerVolumes;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sBinkService(void* a) {
    void* p = pBinkService;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkShouldSkip(void* a) {
    void* p = pBinkShouldSkip;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

void __stdcall sBinkGetPalette(void* a) {
    void* p = pBinkGetPalette;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkControlBackgroundIO(void* a, void* b) {
    void* p = pBinkControlBackgroundIO;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkControlPlatformFeatures(void* a, void* b) {
    void* p = pBinkControlPlatformFeatures;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkSetWillLoop(void* a, void* b) {
    void* p = pBinkSetWillLoop;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sBinkOpenTrack(void* a, void* b) {
    void* p = pBinkOpenTrack;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkCloseTrack(void* a) {
    void* p = pBinkCloseTrack;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetTrackData(void* a, void* b) {
    void* p = pBinkGetTrackData;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkGetTrackType(void* a, void* b) {
    void* p = pBinkGetTrackType;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkGetTrackMaxSize(void* a, void* b) {
    void* p = pBinkGetTrackMaxSize;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkGetTrackID(void* a, void* b) {
    void* p = pBinkGetTrackID;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkGetSummary(void* a, void* b) {
    void* p = pBinkGetSummary;
    LogF("BinkGetSummary(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetRealtime(void* a, void* b, void* c) {
    void* p = pBinkGetRealtime;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkSetFileOffset(void* a, void* b) {
    void* p = pBinkSetFileOffset;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSoundTrack8(void* a, void* b) {
    void* p = pBinkSetSoundTrack8;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

#ifdef BINK_10Q
void __stdcall sBinkSetSoundTrack4(void* a) {
    void* p = pBinkSetSoundTrack8;
    if (p) ((void(__stdcall*)(void*))p)(a);
}
#else
void __stdcall sBinkSetSoundTrack4(void* a) {
    void* p = pBinkSetSoundTrack8;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, 0);
}
#endif

void __stdcall sBinkSetIO(void* a) {
    void* p = pBinkSetIO;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetFrameRate(void* a, void* b) {
    void* p = pBinkSetFrameRate;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSimulate(void* a) {
    void* p = pBinkSetSimulate;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetIOSize(void* a) {
    void* p = pBinkSetIOSize;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkSetSoundSystem(void* a, void* b) {
    void* p = pBinkSetSoundSystem;
    LogF("BinkSetSoundSystem(%p,%p)", a, b);
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkOpenDirectSound(void* a) {
    void* p = pBinkOpenDirectSound;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenWaveOut(void* a) {
    void* p = pBinkOpenWaveOut;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenMiles(void* a) {
    void* p = pBinkOpenMiles;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDX8SurfaceType(void* a) {
    void* p = pBinkDX8SurfaceType;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkDX9SurfaceType(void* a) {
    void* p = pBinkDX9SurfaceType;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkBufferOpen(void* a, void* b, void* c, void* d) {
    void* p = pBinkBufferOpen;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d) : 0;
}

void __stdcall sBinkBufferSetHWND(void* a, void* b) {
    void* p = pBinkBufferSetHWND;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sBinkDDSurfaceType(void* a) {
    void* p = pBinkDDSurfaceType;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkIsSoftwareCursor(void* a, void* b) {
    void* p = pBinkIsSoftwareCursor;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkCheckCursor(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkCheckCursor;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e) : 0;
}

void __stdcall sBinkBufferSetDirectDraw(void* a, void* b) {
    void* p = pBinkBufferSetDirectDraw;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkBufferClose(void* a) {
    void* p = pBinkBufferClose;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferLock(void* a) {
    void* p = pBinkBufferLock;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferUnlock(void* a) {
    void* p = pBinkBufferUnlock;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferSetResolution(void* a, void* b, void* c) {
    void* p = pBinkBufferSetResolution;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferCheckWinPos(void* a, void* b, void* c) {
    void* p = pBinkBufferCheckWinPos;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetOffset(void* a, void* b, void* c) {
    void* p = pBinkBufferSetOffset;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferBlit(void* a, void* b, void* c) {
    void* p = pBinkBufferBlit;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetScale(void* a, void* b, void* c) {
    void* p = pBinkBufferSetScale;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

intptr_t __stdcall sBinkBufferGetDescription(void* a) {
    void* p = pBinkBufferGetDescription;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkBufferGetError() {
    void* p = pBinkBufferGetError;
    return p ? ((intptr_t(__stdcall*)())p)() : 0;
}

void __stdcall sBinkBufferClear(void* a, void* b) {
    void* p = pBinkBufferClear;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRestoreCursor(void* a) {
    void* p = pBinkRestoreCursor;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkStartAsyncThread(void* a, void* b) {
    void* p = pBinkStartAsyncThread;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkDoFrameAsync(void* a, void* b, void* c) {
    void* p = pBinkDoFrameAsync;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkDoFrameAsyncWait(void* a, void* b) {
    void* p = pBinkDoFrameAsyncWait;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRequestStopAsyncThread(void* a) {
    void* p = pBinkRequestStopAsyncThread;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkWaitStopAsyncThread(void* a) {
    void* p = pBinkWaitStopAsyncThread;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetMixBins(void* a, void* b, void* c, void* d) {
    void* p = pBinkSetMixBins;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d);
}

void __stdcall sBinkSetMixBinVolumes(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkSetMixBinVolumes;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sExpandBink(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m, void* n) {
    void* p = pExpandBink;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
}

void __stdcall sExpandBundleSizes(void* a, void* b) {
    void* p = pExpandBundleSizes;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sRADSetMemory(void* a, void* b) {
    void* p = pRADSetMemory;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sRADTimerRead() {
    void* p = pRADTimerRead;
    return p ? ((intptr_t(__stdcall*)())p)() : 0;
}

intptr_t __stdcall sradmalloc(void* a) {
    void* p = pradmalloc;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

void __stdcall sradfree(void* a) {
    void* p = pradfree;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

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
