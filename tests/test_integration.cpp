#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstring>
#include <string>
#include <cstdio>

// ============================================================================
// Integration tests — Test with real DLLs, real .mix files, real .wav files
//
// Game directory is resolved in order:
//   1. GAME_DIR environment variable
//   2. Interactive prompt at first use
//
// All file paths are relative to the game directory.
// ============================================================================

static std::string g_gameDir;
static bool g_gameDirResolved = false;

static void ResolveGameDir() {
    if (g_gameDirResolved) return;
    g_gameDirResolved = true;

    char envBuf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("GAME_DIR", envBuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        g_gameDir = envBuf;
        return;
    }
}

static const std::string& GameDir() {
    ResolveGameDir();
    return g_gameDir;
}

static bool HasGameDir() {
    return !GameDir().empty();
}

static std::string GamePath(const char* relativePath) {
    return GameDir() + "\\" + relativePath;
}

// ============================================================================
// Project-root relative paths (for binkw32.cfg, etc.)
// ============================================================================

static std::string ProjectRoot() {
    return std::string(TEST_DATA_DIR) + "\\..\\..";
}

static std::string ProjectPath(const char* relativePath) {
    return ProjectRoot() + "\\" + relativePath;
}

// ============================================================================
// Real Bink DLL paths — always in Real/ directory
// ============================================================================

static std::string RealDllPath(const char* dllName) {
    return ProjectRoot() + "\\Real\\" + dllName;
}

// ============================================================================
// Proxy DLL path — in build output
// ============================================================================

static std::string ProxyDllPath() {
    std::string path5 = ProjectRoot() + "\\build\\GROUP_5\\Release\\binkw32.dll";
    DWORD attr = GetFileAttributesA(path5.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return path5;

    std::string path7 = ProjectRoot() + "\\build\\GROUP_7\\Release\\binkw32.dll";
    attr = GetFileAttributesA(path7.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return path7;

    return ProjectRoot() + "\\build\\GROUP_5\\Release\\binkw32.dll";
}

// ============================================================================
// 1. DLL Export Verification (no game dir needed)
// ============================================================================

static const char* EXPORT_NAMES[] = {
    "_BinkLogoAddress@0", "_BinkSetError@4", "_BinkGetError@0",
    "_BinkOpen@8", "_BinkOpenWithOptions@12", "_BinkDoFrame@4",
    "_BinkDoFramePlane@8", "_BinkNextFrame@4", "_BinkWait@4",
    "_BinkClose@4", "_BinkPause@8", "_BinkCopyToBuffer@28",
    "_BinkCopyToBufferRect@44", "_BinkGetRects@8", "_BinkGoto@12",
    "_BinkGetKeyFrame@12", "_BinkFreeGlobals@0", "_BinkGetPlatformInfo@8",
    "_BinkGetFrameBuffersInfo@8", "_BinkRegisterFrameBuffers@8",
    "_BinkSetVideoOnOff@8", "_BinkSetSoundOnOff@8",
    "_BinkSetVolume@8", "_BinkSetPan@12", "_BinkSetSpeakerVolumes@20",
    "_BinkService@4", "_BinkShouldSkip@4", "_BinkGetPalette@4",
    "_BinkControlBackgroundIO@8", "_BinkControlPlatformFeatures@8",
    "_BinkSetWillLoop@8", "_BinkOpenTrack@8", "_BinkCloseTrack@4",
    "_BinkGetTrackData@8", "_BinkGetTrackType@8",
    "_BinkGetTrackMaxSize@8", "_BinkGetTrackID@8",
    "_BinkGetSummary@8", "_BinkGetRealtime@12", "_BinkSetFileOffset@8",
    "_BinkSetSoundTrack@8", "_BinkSetSoundTrack@4",
    "_BinkSetIO@4", "_BinkSetFrameRate@8", "_BinkSetSimulate@4",
    "_BinkSetIOSize@4", "_BinkSetSoundSystem@8",
    "_BinkOpenDirectSound@4", "_BinkOpenWaveOut@4", "_BinkOpenMiles@4",
    "_BinkDX8SurfaceType@4", "_BinkDX9SurfaceType@4",
    "_BinkBufferOpen@16", "_BinkBufferSetHWND@8",
    "_BinkDDSurfaceType@4", "_BinkIsSoftwareCursor@8",
    "_BinkCheckCursor@20", "_BinkBufferSetDirectDraw@8",
    "_BinkBufferClose@4", "_BinkBufferLock@4", "_BinkBufferUnlock@4",
    "_BinkBufferSetResolution@12", "_BinkBufferCheckWinPos@12",
    "_BinkBufferSetOffset@12", "_BinkBufferBlit@12", "_BinkBufferSetScale@12",
    "_BinkBufferGetDescription@4", "_BinkBufferGetError@0",
    "_BinkBufferClear@8", "_BinkRestoreCursor@4",
    "_BinkStartAsyncThread@8", "_BinkDoFrameAsync@12",
    "_BinkDoFrameAsyncWait@8", "_BinkRequestStopAsyncThread@4",
    "_BinkWaitStopAsyncThread@4", "_BinkSetMixBins@16",
    "_BinkSetMixBinVolumes@20", "_ExpandBink@56", "_ExpandBundleSizes@8",
    "_RADSetMemory@8", "_BinkSetMemory@8", "_RADTimerRead@0",
    "_radmalloc@4", "_radfree@4", "_YUV_init@4",
    "_YUV_blit_16a1bpp@52", "_YUV_blit_16a1bpp_mask@52",
    "_YUV_blit_16a4bpp@52", "_YUV_blit_16a4bpp_mask@52",
    "_YUV_blit_16bpp@48", "_YUV_blit_16bpp_mask@48",
    "_YUV_blit_24bpp@48", "_YUV_blit_24bpp_mask@48",
    "_YUV_blit_24rbpp@48", "_YUV_blit_24rbpp_mask@48",
    "_YUV_blit_32abpp@52", "_YUV_blit_32abpp_mask@52",
    "_YUV_blit_32bpp@48", "_YUV_blit_32bpp_mask@48",
    "_YUV_blit_32rabpp@52", "_YUV_blit_32rabpp_mask@52",
    "_YUV_blit_32rbpp@48", "_YUV_blit_32rbpp_mask@48",
    "_YUV_blit_UYVY@48", "_YUV_blit_UYVY_mask@48",
    "_YUV_blit_YUY2@48", "_YUV_blit_YUY2_mask@48",
    "_YUV_blit_YV12@52"
};

TEST(Integration_DllExports, AllExportsResolved) {
    std::string dllPath = ProxyDllPath();
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL) << "Failed to load proxy DLL: " << dllPath;

    int found = 0;
    for (const char* name : EXPORT_NAMES) {
        FARPROC proc = GetProcAddress(hMod, name);
        if (proc) found++;
        else ADD_FAILURE() << "Missing export: " << name;
    }

    FreeLibrary(hMod);
    EXPECT_EQ(found, (int)(sizeof(EXPORT_NAMES) / sizeof(EXPORT_NAMES[0])));
}

TEST(Integration_DllExports, BinkSetMemoryByName) {
    std::string dllPath = ProxyDllPath();
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL);

    FARPROC proc = GetProcAddress(hMod, "_BinkSetMemory@8");
    EXPECT_NE(proc, (FARPROC)NULL);
    FreeLibrary(hMod);
}

// ============================================================================
// 2. Real Bink DLL Ordinal Resolution
// ============================================================================

TEST(Integration_RealDll, Group5_OrdinalsResolve) {
    std::string dllPath = RealDllPath("binkw32_1.0q.dll");
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL) << "Failed to load " << dllPath;

    int resolved = 0;
    for (int ordinal = 1; ordinal <= 83; ordinal++) {
        if (GetProcAddress(hMod, (LPCSTR)ordinal)) resolved++;
    }
    FreeLibrary(hMod);
    EXPECT_GE(resolved, 80);
}

TEST(Integration_RealDll, Group7_OrdinalsResolve) {
    std::string dllPath = RealDllPath("binkw32_1.9u.dll");
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL) << "Failed to load " << dllPath;

    int resolved = 0;
    for (int ordinal = 1; ordinal <= 73; ordinal++) {
        if (GetProcAddress(hMod, (LPCSTR)ordinal)) resolved++;
    }
    FreeLibrary(hMod);
    EXPECT_GE(resolved, 70);
}

// ============================================================================
// 3. CRC32 against known .mix entries
// ============================================================================

TEST(Integration_MixCrc32, KnownEntriesFromExpandmo) {
    struct { const char* name; uint32_t expectedCrc; } entries[] = {
        {"a04_f03e.bik", 0x92A0FBFC}, {"a12_f00e.bik", 0xD8A426D5},
        {"a10_f00e.bik", 0xDC51F6E8}, {"a09_f00e.bik", 0xE1CA02E2},
        {"a01_f00e.bik", 0xF21D4216}, {"a03_f00e.bik", 0xF6E8922B},
        {"a05_f00e.bik", 0xFBF6E26C}, {"a13_f01e.bik", 0xFC3A9E4E},
        {"a07_f00e.bik", 0xFF033251}, {"a08_f00e.bik", 0x0E0869DC},
        {"a06_f00e.bik", 0x10C1596F}, {"a04_f00e.bik", 0x14348952},
        {"a02_f00e.bik", 0x192AF915}, {"a00_f00e.bik", 0x1DDF2928},
        {"a11_f00e.bik", 0x33939DD6}, {"a13_f00e.bik", 0x37664DEB},
        {"a13_f02e.bik", 0x7AAEECE0},
    };
    for (const auto& e : entries) {
        EXPECT_EQ(MixCrc32(e.name), e.expectedCrc) << "CRC mismatch for " << e.name;
    }
}

TEST(Integration_MixCrc32, KnownEntriesFromExpandmo12) {
    struct { const char* name; uint32_t expectedCrc; } entries[] = {
        {"s11_f00e.bik", 0xD3B0EEB5}, {"s13_f00e.bik", 0xD7453E88},
        {"s08_f00e.bik", 0xEE2B1ABF}, {"s06_f00e.bik", 0xF0E22A0C},
        {"s04_f00e.bik", 0xF417FA31}, {"s02_f00e.bik", 0xF9098A76},
        {"s09_f00e.bik", 0x01E97181}, {"s01_f00e.bik", 0x123E3175},
        {"s03_f00e.bik", 0x16CBE148}, {"s05_f00e.bik", 0x1BD5910F},
        {"s13_f01e.bik", 0x1C19ED2D}, {"s07_f00e.bik", 0x1F204132},
        {"s12_f00e.bik", 0x388755B6}, {"s10_f00e.bik", 0x3C72858B},
    };
    for (const auto& e : entries) {
        EXPECT_EQ(MixCrc32(e.name), e.expectedCrc) << "CRC mismatch for " << e.name;
    }
}

// ============================================================================
// 4. Bink Header Parsing
// ============================================================================

TEST(Integration_BinkHeader, ParseMinimalBinkHeader) {
    uint8_t hdr[44] = {0};
    hdr[0] = 0x42; hdr[1] = 0x49; hdr[2] = 0x4B; hdr[3] = 0x66;  // BIKf
    hdr[8] = 0x64;  // frameCount = 100
    hdr[20] = 0x78; hdr[21] = 0x05;  // width = 1400
    hdr[24] = 0x38; hdr[25] = 0x04;  // height = 1080
    hdr[28] = 0x0F;  // frameRate = 15
    hdr[32] = 0x01;  // frameRateDiv = 1

    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\test_header.bik", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(hdr, 1, 44, f);
    fclose(f);

    BinkFileInfo info = ReadBinkHeaderFromPath(path);
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.width, 1400u);
    EXPECT_EQ(info.height, 1080u);
    DeleteFileA(path);
}

TEST(Integration_BinkHeader, InvalidMarkerRejected) {
    uint8_t hdr[44] = {0};
    hdr[20] = 0x78; hdr[21] = 0x05;

    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\test_invalid.bik", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(hdr, 1, 44, f);
    fclose(f);

    BinkFileInfo info = ReadBinkHeaderFromPath(path);
    EXPECT_FALSE(info.valid);
    DeleteFileA(path);
}

// ============================================================================
// 5. Config Integration — relative to project root
// ============================================================================

class IntegrationConfigTest : public ::testing::Test {
protected:
    char savedDllDir[MAX_PATH];

    void SetUp() override {
        memcpy(savedDllDir, g_dllDir, MAX_PATH);
    }

    void TearDown() override {
        lstrcpynA(g_dllDir, savedDllDir, MAX_PATH);
    }
};

TEST_F(IntegrationConfigTest, LoadRealConfig) {
    std::string projectDir = ProjectRoot() + "\\";
    lstrcpynA(g_dllDir, projectDir.c_str(), MAX_PATH);
    ResetAudioConfig();
    LoadAudioConfig();

    EXPECT_EQ(g_exceptionCount, 3);
    if (g_exceptionCount >= 1) EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
    if (g_exceptionCount >= 2) EXPECT_STREQ(g_exceptions[1].mixName, "movies02.mix");
    if (g_exceptionCount >= 3) EXPECT_STREQ(g_exceptions[2].mixName, "movmd03.mix");

    const char* result = FindWavForBik("westlogo.bik", "movies01.mix");
    EXPECT_NE(result, (const char*)NULL);
    if (result) EXPECT_STREQ(result, "BinkWAV\\RA2\\westlogo.wav");
}

TEST_F(IntegrationConfigTest, RealExceptionPriority) {
    std::string projectDir = ProjectRoot() + "\\";
    lstrcpynA(g_dllDir, projectDir.c_str(), MAX_PATH);
    ResetAudioConfig();
    LoadAudioConfig();

    const char* exceptionResult = FindWavForBik("a01_f00e.bik", "movies01.mix");
    EXPECT_NE(exceptionResult, (const char*)NULL);
    if (exceptionResult) EXPECT_STREQ(exceptionResult, "BinkWAV\\RA2\\a01_f00e.wav");

    const char* noMixResult = FindWavForBik("a01_f00e.bik", NULL);
    EXPECT_EQ(noMixResult, (const char*)NULL);
}

// ============================================================================
// 6. WAV Decode — uses GAME_DIR-relative paths
// ============================================================================

TEST(Integration_WavDecode, DecodeRealWavFile) {
    if (!HasGameDir()) {
        GTEST_SKIP() << "Game directory not set — skipping WAV decode test";
    }

    const char* candidates[] = {
        "BinkWAV\\RA2\\westlogo.wav",
        "BinkWAV\\RA2\\a01_f00e.wav",
        "BinkWAV\\RA2YR\\a01_f00e.wav",
    };

    BOOL foundAny = FALSE;
    for (const char* rel : candidates) {
        std::string fullPath = GamePath(rel);
        DecodedAudio audio = {0};
        if (DecodeAudioFile(fullPath.c_str(), &audio)) {
            foundAny = TRUE;
            EXPECT_GT(audio.pcmSize, 0u);
            EXPECT_NE(audio.pcmData, (char*)NULL);
            EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
            if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
            break;
        }
    }

    if (!foundAny) {
        GTEST_SKIP() << "No WAV files found in " << GameDir();
    }
}

// ============================================================================
// 7. Ordinal Table Cross-Validation
// ============================================================================

TEST(Integration_Ordinals, Group5TableMatchesRealDll) {
    std::string dllPath = RealDllPath("binkw32_1.0q.dll");
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL);

    for (int ordinal = 1; ordinal <= 53; ordinal++) {
        EXPECT_NE(GetProcAddress(hMod, (LPCSTR)ordinal), (FARPROC)NULL)
            << "Group 5 ordinal " << ordinal << " not found";
    }
    FreeLibrary(hMod);
}

TEST(Integration_Ordinals, Group7TableMatchesRealDll) {
    std::string dllPath = RealDllPath("binkw32_1.9u.dll");
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL);

    int keyOrdinals[] = {1, 16, 20, 25, 28, 35, 44, 49, 53, 58, 61, 67, 71, 73};
    for (int ordinal : keyOrdinals) {
        EXPECT_NE(GetProcAddress(hMod, (LPCSTR)ordinal), (FARPROC)NULL)
            << "Group 7 ordinal " << ordinal << " not found";
    }
    FreeLibrary(hMod);
}
