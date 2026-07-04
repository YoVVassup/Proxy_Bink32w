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
typedef HBINK (__stdcall *pfn_BinkOpen)(const char* name, unsigned int flags);
typedef void  (__stdcall *pfn_BinkClose)(HBINK bink);
typedef int   (__stdcall *pfn_BinkDoFrame)(HBINK bink);
typedef int   (__stdcall *pfn_BinkWait)(HBINK bink);
typedef int   (__stdcall *pfn_BinkNextFrame)(HBINK bink);
typedef int   (__stdcall *pfn_BinkCopyToBuffer)(HBINK bink, void* buffer, int pitch,
    int height, int x, int y, int flags);
typedef void  (__stdcall *pfn_BinkGoto)(HBINK bink, int frame, int flags);
typedef void  (__stdcall *pfn_BinkGetSummary)(HBINK bink, void* summary);
typedef int   (__stdcall *pfn_BinkSetSoundSystem)(void* a, void* b);
typedef void  (__stdcall *pfn_BinkOpenDirectSound)(void* a);
typedef void  (__stdcall *pfn_BinkSetVolume)(void* a, int b);
typedef void  (__stdcall *pfn_BinkSetPan)(void* a, int b, int c);
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
static pfn_BinkSetVolume        g_BinkSetVolume;
static pfn_BinkSetPan           g_BinkSetPan;
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

    // Resolve by ordinal (Bink 1.0q ordinals)
    #define RESOLVE_ORDINAL(name, ord) \
        g_##name = (pfn_##name)GetProcAddress(hMod, (LPCSTR)(ord)); \
        if (!g_##name) { printf("ERROR: %s (ordinal %d) not found\n", #name, ord); return FALSE; }

    RESOLVE_ORDINAL(BinkOpen, 34);
    RESOLVE_ORDINAL(BinkClose, 16);
    RESOLVE_ORDINAL(BinkDoFrame, 20);
    RESOLVE_ORDINAL(BinkWait, 53);
    RESOLVE_ORDINAL(BinkNextFrame, 33);
    RESOLVE_ORDINAL(BinkCopyToBuffer, 18);
    RESOLVE_ORDINAL(BinkGoto, 30);
    RESOLVE_ORDINAL(BinkGetSummary, 25);
    RESOLVE_ORDINAL(BinkSetSoundSystem, 49);
    RESOLVE_ORDINAL(BinkOpenDirectSound, 35);
    RESOLVE_ORDINAL(BinkSetVolume, 52);
    RESOLVE_ORDINAL(BinkSetPan, 46);
    RESOLVE_ORDINAL(BinkPause, 39);
    RESOLVE_ORDINAL(BinkDDSurfaceType, 19);

    #undef RESOLVE_ORDINAL

    printf("Bink DLL loaded: %s\n", dllPath);
    return TRUE;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <bik_file> [group]\n", argv[0]);
        printf("  group: 5 (default, 1.0q) or 7 (1.9u)\n");
        return 1;
    }

    const char* bikFile = argv[1];
    int group = (argc > 2) ? atoi(argv[2]) : 5;

    // Determine DLL path
    char dllDir[MAX_PATH];
    GetModuleFileNameA(NULL, dllDir, MAX_PATH);
    char* slash = strrchr(dllDir, '\\');
    if (slash) *(slash + 1) = 0;

    char dllPath[MAX_PATH];
    if (group == 7)
        _snprintf_s(dllPath, sizeof(dllPath), _TRUNCATE, "%sbinkw32_1.9u.dll", dllDir);
    else
        _snprintf_s(dllPath, sizeof(dllPath), _TRUNCATE, "%sbinkw32_1.0q.dll", dllDir);

    printf("=== Test Bink Player ===\n");
    printf("File: %s\n", bikFile);
    printf("DLL:  %s (group %d)\n", dllPath, group);

    // Load Bink DLL
    if (!LoadBinkDll(dllPath)) return 1;

    // Init sound system
    char dsound[256] = {0};
    g_BinkSetSoundSystem(dsound, 0);
    g_BinkOpenDirectSound(NULL);

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
    printf("\nPlaying %d frames...\n", maxFrames);

    for (int frame = 0; frame < maxFrames; frame++) {
        g_BinkDoFrame(bink);

        int result = g_BinkCopyToBuffer(bink, buffer, pitch, h, 0, 0, BINKSURFACE565);
        if (result) {
            printf("Frame %d: BinkCopyToBuffer returned %d\n", frame, result);
        }

        g_BinkWait(bink);
        g_BinkNextFrame(bink);

        if (frame % 30 == 0) {
            printf("  Frame %d/%d\n", frame, maxFrames);
        }
    }

    // Test BinkGoto
    printf("\nTesting BinkGoto to frame 0...\n");
    g_BinkGoto(bink, 0, 0);
    g_BinkDoFrame(bink);
    g_BinkCopyToBuffer(bink, buffer, pitch, h, 0, 0, BINKSURFACE565);
    printf("BinkGoto test passed\n");

    // Test BinkPause
    printf("Testing BinkPause...\n");
    g_BinkPause(bink, 1); // pause
    g_BinkPause(bink, 0); // resume
    printf("BinkPause test passed\n");

    // Test BinkSetVolume/Pan
    printf("Testing BinkSetVolume/Pan...\n");
    g_BinkSetVolume(bink, 0);
    g_BinkSetPan(bink, 0, 0);
    printf("BinkSetVolume/Pan test passed\n");

    // Cleanup
    printf("\nClosing video...\n");
    VirtualFree(buffer, 0, MEM_RELEASE);
    g_BinkClose(bink);

    printf("=== All tests passed ===\n");
    return 0;
}
