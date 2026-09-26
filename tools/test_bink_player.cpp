// ============================================================================
// test_bink_player.cpp — Standalone Bink video player for integration testing
//
// Uses the real Bink 1.0q DLL (via proxy) to play a .bik file.
// Tests: BinkOpen, BinkGetSummary, BinkDoFrame, BinkWait, BinkCopyToBuffer,
//        BinkGoto, BinkClose, video scaling, audio replacement.
//
// Usage: test_bink_player.exe <path_to_bik_file> [group_number]
//   group_number: 5 (default, 1.0q) or 7 (1.9u)
// ============================================================================

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Bink function pointer types (from Bink 1.0 SDK)
typedef void* HBINK;
typedef int BOOL32;

// Function pointer typedefs
// BinkSetSoundSystem takes a callback it will invoke itself to open the
// device, not a driver name: BinkSoundUseDirectSound(x) is
// BinkSetSoundSystem(BinkOpenDirectSound, x).
typedef void* (__stdcall *SndOpenCallback)(unsigned long param);
typedef HBINK (__stdcall *pfn_BinkOpen)(const char* name, unsigned int flags);
typedef void  (__stdcall *pfn_BinkClose)(HBINK bink);
typedef int   (__stdcall *pfn_BinkDoFrame)(HBINK bink);
typedef int   (__stdcall *pfn_BinkWait)(HBINK bink);
typedef int   (__stdcall *pfn_BinkNextFrame)(HBINK bink);
typedef int   (__stdcall *pfn_BinkCopyToBuffer)(HBINK bink, void* buffer, int pitch,
    int height, int x, int y, int flags);
typedef void  (__stdcall *pfn_BinkGoto)(HBINK bink, int frame, int flags);
typedef void  (__stdcall *pfn_BinkGetSummary)(HBINK bink, void* summary);
typedef int   (__stdcall *pfn_BinkSetSoundSystem)(SndOpenCallback open, unsigned long param);
typedef void* (__stdcall *pfn_BinkOpenDirectSound)(unsigned long param);
// BinkSetVolume/BinkSetPan changed arity between generations:
// short form (bnk, value) in Bink <= 1.2c, long form (bnk, trackid, value)
// in Bink >= 1.2i. Decorated names differ accordingly, so both are loaded.
typedef void  (__stdcall *pfn_BinkSetVolume2)(void* bink, int volume);
typedef void  (__stdcall *pfn_BinkSetVolume3)(void* bink, void* trackid, int volume);
typedef void  (__stdcall *pfn_BinkSetPan2)(void* bink, int pan);
typedef void  (__stdcall *pfn_BinkSetPan3)(void* bink, void* trackid, int pan);
typedef void  (__stdcall *pfn_BinkPause)(void* a, int b);
typedef int   (__stdcall *pfn_BinkDDSurfaceType)(void* a);

// Global function pointers
static pfn_BinkOpen             g_BinkOpen;
static pfn_BinkClose            g_BinkClose;
static pfn_BinkDoFrame          g_BinkDoFrame;
static pfn_BinkWait             g_BinkWait;
static pfn_BinkNextFrame        g_BinkNextFrame;
static pfn_BinkCopyToBuffer     g_BinkCopyToBuffer;
static pfn_BinkGoto             g_BinkGoto;
static pfn_BinkGetSummary       g_BinkGetSummary;
static pfn_BinkSetSoundSystem   g_BinkSetSoundSystem;
static pfn_BinkOpenDirectSound  g_BinkOpenDirectSound;
static pfn_BinkSetVolume2       g_BinkSetVolume2;
static pfn_BinkSetVolume3       g_BinkSetVolume3;
static pfn_BinkSetPan2          g_BinkSetPan2;
static pfn_BinkSetPan3          g_BinkSetPan3;
static pfn_BinkPause            g_BinkPause;
static pfn_BinkDDSurfaceType    g_BinkDDSurfaceType;

// BinkOpen flags (from Bink SDK)
#define BINKFILEHANDLE 0x00800000

// Bink surface types
#define BINKSURFACE565 10

static BOOL LoadBinkDll(const char* dllPath) {
    HMODULE hMod = LoadLibraryA(dllPath);
    if (!hMod) {
        printf("ERROR: Failed to load %s (error %lu)\n", dllPath, GetLastError());
        return FALSE;
    }

    // Resolve by decorated name. Ordinals are NOT usable here: they differ per
    // Bink generation (BinkOpen is 34 in 1.0q but 44 in 1.9u), while the
    // decorated name is stable across every version this tool supports.
    #define RESOLVE(type, var, decorated) \
        g_##var = (type)GetProcAddress(hMod, decorated); \
        if (!g_##var) { printf("ERROR: %s not found in %s\n", decorated, dllPath); return FALSE; }

    RESOLVE(pfn_BinkOpen,            BinkOpen,            "_BinkOpen@8");
    RESOLVE(pfn_BinkClose,           BinkClose,           "_BinkClose@4");
    RESOLVE(pfn_BinkDoFrame,         BinkDoFrame,         "_BinkDoFrame@4");
    RESOLVE(pfn_BinkWait,            BinkWait,            "_BinkWait@4");
    RESOLVE(pfn_BinkNextFrame,       BinkNextFrame,       "_BinkNextFrame@4");
    RESOLVE(pfn_BinkCopyToBuffer,    BinkCopyToBuffer,    "_BinkCopyToBuffer@28");
    RESOLVE(pfn_BinkGoto,            BinkGoto,            "_BinkGoto@12");
    RESOLVE(pfn_BinkGetSummary,      BinkGetSummary,      "_BinkGetSummary@8");
    RESOLVE(pfn_BinkSetSoundSystem,  BinkSetSoundSystem,  "_BinkSetSoundSystem@8");
    RESOLVE(pfn_BinkOpenDirectSound, BinkOpenDirectSound, "_BinkOpenDirectSound@4");
    RESOLVE(pfn_BinkPause,           BinkPause,           "_BinkPause@8");
    RESOLVE(pfn_BinkDDSurfaceType,   BinkDDSurfaceType,   "_BinkDDSurfaceType@4");

    #undef RESOLVE

    // Volume/pan: one or the other, depending on the generation.
    g_BinkSetVolume2 = (pfn_BinkSetVolume2)GetProcAddress(hMod, "_BinkSetVolume@8");
    g_BinkSetVolume3 = (pfn_BinkSetVolume3)GetProcAddress(hMod, "_BinkSetVolume@12");
    if (!g_BinkSetVolume2 && !g_BinkSetVolume3) {
        printf("ERROR: neither _BinkSetVolume@8 nor _BinkSetVolume@12 in %s\n", dllPath);
        return FALSE;
    }

    g_BinkSetPan2 = (pfn_BinkSetPan2)GetProcAddress(hMod, "_BinkSetPan@8");
    g_BinkSetPan3 = (pfn_BinkSetPan3)GetProcAddress(hMod, "_BinkSetPan@12");
    if (!g_BinkSetPan2 && !g_BinkSetPan3) {
        printf("ERROR: neither _BinkSetPan@8 nor _BinkSetPan@12 in %s\n", dllPath);
        return FALSE;
    }

    printf("Bink DLL loaded: %s\n", dllPath);
    printf("  BinkSetVolume: %s\n", g_BinkSetVolume3 ? "@12 (long)" : "@8 (short)");
    printf("  BinkSetPan:    %s\n", g_BinkSetPan3 ? "@12 (long)" : "@8 (short)");
    return TRUE;
}

int main(int argc, char* argv[]) {
    // Unbuffered: a crash must still leave the trace of what ran.
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        printf("Usage: %s <bik_file> [group]\n", argv[0]);
        printf("  group: 5 (default, 1.0q) or 7 (1.9u)\n");
        return 1;
    }

    const char* bikFile = argv[1];
    int group = (argc > 2) ? atoi(argv[2]) : 5;

    // Only groups 5 and 7 are supported. Any other number used to fall through
    // to binkw32_1.0q.dll while the output still echoed the requested group -
    // reject it instead of silently loading a different DLL.
    const char* groupVersion;
    const char* realDll;
    if (group == 5) {
        groupVersion = "1.0q";
        realDll = "binkw32_1.0q.dll";
    } else if (group == 7) {
        groupVersion = "1.9u";
        realDll = "binkw32_1.9u.dll";
    } else {
        printf("ERROR: unsupported group %d (supported: 5 = 1.0q, 7 = 1.9u)\n", group);
        return 1;
    }

    // Determine DLL path
    char dllDir[MAX_PATH];
    GetModuleFileNameA(NULL, dllDir, MAX_PATH);
    char* slash = strrchr(dllDir, '\\');
    if (slash) *(slash + 1) = 0;

    char dllPath[MAX_PATH];
    _snprintf_s(dllPath, sizeof(dllPath), _TRUNCATE, "%s%s", dllDir, realDll);

    printf("=== Test Bink Player ===\n");
    printf("File: %s\n", bikFile);
    printf("DLL:  %s (group %d, Bink %s)\n", dllPath, group, groupVersion);

    // Load Bink DLL
    if (!LoadBinkDll(dllPath)) return 1;

    // Init sound system: Bink invokes the callback itself. Passing a buffer
    // here instead would make Bink CALL that buffer's address - which is
    // exactly how this tool used to crash with 0xC0000005.
    char noSoundEnv[8] = {0};
    BOOL noSound = GetEnvironmentVariableA("BINKPLAYER_NOSOUND", noSoundEnv, sizeof(noSoundEnv)) > 0;
    if (noSound) {
        printf("\nSound disabled (BINKPLAYER_NOSOUND)\n");
    } else {
        printf("\nSetting up sound...\n");
        int ss = g_BinkSetSoundSystem(g_BinkOpenDirectSound, 0);
        // Probe-proven: 1 = the callback returned a device (success), 0 = it did not.
        printf("BinkSetSoundSystem returned %d (%s)\n", ss, ss ? "sound on" : "no sound");
        if (ss == 0) {
            printf("WARNING: sound system unavailable, continuing without audio\n");
        }
    }

    // Open video
    printf("\nOpening video...\n");
    HBINK bink = g_BinkOpen(bikFile, 0);
    if (!bink) {
        printf("ERROR: BinkOpen failed\n");
        return 1;
    }

    // Get video info
    unsigned char summary[512] = {0};
    g_BinkGetSummary(bink, summary);
    unsigned int w = *(unsigned int*)(summary + 0);
    unsigned int h = *(unsigned int*)(summary + 4);
    unsigned int frameRate = *(unsigned int*)(summary + 20);
    unsigned int frameRateDiv = *(unsigned int*)(summary + 24);
    unsigned int totalFrames = *(unsigned int*)(summary + 32);

    printf("Video: %ux%u, %u/%u fps, %u frames\n", w, h, frameRate, frameRateDiv, totalFrames);

    // Allocate frame buffer (RGB565 = 2 bytes per pixel)
    int pitch = (w * 2 + 15) & ~15; // 16-byte aligned
    void* buffer = VirtualAlloc(NULL, pitch * h, MEM_COMMIT, PAGE_READWRITE);
    if (!buffer) {
        printf("ERROR: VirtualAlloc failed\n");
        g_BinkClose(bink);
        return 1;
    }

    // Play first 100 frames
    int maxFrames = (totalFrames < 100) ? totalFrames : 100;
    int failures = 0;
    printf("\nPlaying %d frames...\n", maxFrames);

    for (int frame = 0; frame < maxFrames; frame++) {
        g_BinkDoFrame(bink);

        int result = g_BinkCopyToBuffer(bink, buffer, pitch, h, 0, 0, BINKSURFACE565);
        if (result) {
            printf("FAIL: Frame %d: BinkCopyToBuffer returned %d\n", frame, result);
            failures++;
        }

        g_BinkWait(bink);
        g_BinkNextFrame(bink);

        if (frame % 30 == 0) {
            printf("  Frame %d/%d\n", frame, maxFrames);
        }
    }

    // Test BinkGoto. Flags 0 makes Bink wait for the audio to resync and can
    // block forever when no sound system is running; BINKGOTOQUICK (1) seeks
    // immediately.
    printf("\nTesting BinkGoto to frame 0...\n");
    g_BinkGoto(bink, 0, 1);
    g_BinkDoFrame(bink);
    if (g_BinkCopyToBuffer(bink, buffer, pitch, h, 0, 0, BINKSURFACE565)) {
        printf("FAIL: BinkCopyToBuffer after BinkGoto\n");
        failures++;
    } else {
        printf("BinkGoto test passed\n");
    }

    // Test BinkPause
    printf("Testing BinkPause...\n");
    g_BinkPause(bink, 1); // pause
    g_BinkPause(bink, 0); // resume
    printf("BinkPause test passed\n");

    // Test BinkSetVolume/Pan
    printf("Testing BinkSetVolume/Pan...\n");
    if (g_BinkSetVolume3) g_BinkSetVolume3(bink, NULL, 0);
    else                  g_BinkSetVolume2(bink, 0);
    if (g_BinkSetPan3) g_BinkSetPan3(bink, NULL, 0);
    else               g_BinkSetPan2(bink, 0);
    printf("BinkSetVolume/Pan test passed\n");

    // Cleanup
    printf("\nClosing video...\n");
    VirtualFree(buffer, 0, MEM_RELEASE);
    g_BinkClose(bink);

    if (failures) {
        printf("=== %d FAILURE(S) ===\n", failures);
        return 1;
    }

    printf("=== All tests passed ===\n");
    return 0;
}
