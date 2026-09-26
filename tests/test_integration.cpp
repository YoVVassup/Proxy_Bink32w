#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <utility>
#include <algorithm>

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
    return ProjectRootDir();
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
#ifdef PROXY_DLL_PATH
    {
        DWORD attr = GetFileAttributesA(PROXY_DLL_PATH);
        if (attr != INVALID_FILE_ATTRIBUTES) return PROXY_DLL_PATH;
    }
#endif
    std::string path5 = ProjectRoot() + "\\build\\GROUP_5\\Release\\binkw32.dll";
    DWORD attr = GetFileAttributesA(path5.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return path5;

    std::string path7 = ProjectRoot() + "\\build\\GROUP_7\\Release\\binkw32.dll";
    attr = GetFileAttributesA(path7.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return path7;

    return ProjectRoot() + "\\build\\GROUP_5\\Release\\binkw32.dll";
}

// ============================================================================
// Proxy DLL loading for tests
//
// A proxy DLL that cannot be loaded is a *build* problem (the group target was
// not configured or not built), not an environment problem: reporting green
// while no integration check ran at all is exactly what the audit flagged.
// BINK_TEST_ALLOW_NO_PROXY=1 turns these cases back into skips for setups that
// deliberately build without groups 5/7.
// ============================================================================

enum class ProxyDllStatus { Ok, Skipped, Failed };

static bool AllowNoProxyDll() {
    char buf[8];
    DWORD n = GetEnvironmentVariableA("BINK_TEST_ALLOW_NO_PROXY", buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) && buf[0] != '0' && buf[0] != '\0';
}

static HMODULE LoadProxyDllForTest(std::string& pathOut, ProxyDllStatus& status) {
    pathOut = ProxyDllPath();
    HMODULE h = LoadLibraryA(pathOut.c_str());
    if (h) {
        status = ProxyDllStatus::Ok;
        return h;
    }
    status = AllowNoProxyDll() ? ProxyDllStatus::Skipped : ProxyDllStatus::Failed;
    return nullptr;
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
    "_BinkSetVolume@8", "_BinkSetVolume@12", "_BinkSetPan@8", "_BinkSetPan@12",
    "_BinkSetSpeakerVolumes@20",
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
    "_BinkWaitStopAsyncThread@4", "_BinkSetMixBins@16", "_BinkSetMixBins@8",
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
    std::string dllPath;
    ProxyDllStatus st;
    HMODULE hMod = LoadProxyDllForTest(dllPath, st);
    if (st == ProxyDllStatus::Skipped) {
        GTEST_SKIP() << "proxy DLL not present: " << dllPath << " (BINK_TEST_ALLOW_NO_PROXY set)";
    }
    ASSERT_NE(hMod, (HMODULE)NULL) << "proxy DLL not loadable: " << dllPath
                                   << " (error " << GetLastError()
                                   << ") - build group 5/7 first, or set BINK_TEST_ALLOW_NO_PROXY=1 to skip";

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
    std::string dllPath;
    ProxyDllStatus st;
    HMODULE hMod = LoadProxyDllForTest(dllPath, st);
    if (st == ProxyDllStatus::Skipped) {
        GTEST_SKIP() << "proxy DLL not present: " << dllPath << " (BINK_TEST_ALLOW_NO_PROXY set)";
    }
    ASSERT_NE(hMod, (HMODULE)NULL) << "proxy DLL not loadable: " << dllPath
                                   << " (error " << GetLastError()
                                   << ") - build group 5/7 first, or set BINK_TEST_ALLOW_NO_PROXY=1 to skip";

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
    if (!hMod) {
        GTEST_SKIP() << "Cannot load real DLL: " << dllPath;
    }

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
    if (!hMod) {
        GTEST_SKIP() << "Cannot load real DLL: " << dllPath;
    }

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
    if (g_exceptionCount >= 1) {
        EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
        EXPECT_STREQ(g_exceptions[0].baseDir, "BinkWAV\\RA2");
    }
    if (g_exceptionCount >= 2) {
        EXPECT_STREQ(g_exceptions[1].mixName, "movies02.mix");
        EXPECT_STREQ(g_exceptions[1].baseDir, "BinkWAV\\RA2");
    }
    if (g_exceptionCount >= 3) {
        EXPECT_STREQ(g_exceptions[2].mixName, "movmd03.mix");
        EXPECT_STREQ(g_exceptions[2].baseDir, "BinkWAV\\RA2YR");
    }
}

TEST_F(IntegrationConfigTest, RealConfigBaseDirAuto) {
    std::string projectDir = ProjectRoot() + "\\";
    lstrcpynA(g_dllDir, projectDir.c_str(), MAX_PATH);
    ResetAudioConfig();
    LoadAudioConfig();

    // Create BinkWAV\RA2\westlogo.wav under project root (if not already present).
    std::string dir1 = ProjectPath("BinkWAV");
    std::string dir2 = ProjectPath("BinkWAV\\RA2");
    std::string wavPath = ProjectPath("BinkWAV\\RA2\\westlogo.wav");
    BOOL createdDirs = FALSE;
    BOOL createdFile = FALSE;
    if (GetFileAttributesA(wavPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA(dir1.c_str(), NULL);
        CreateDirectoryA(dir2.c_str(), NULL);
        FILE* f = NULL;
        fopen_s(&f, wavPath.c_str(), "wb");
        if (f) {
            fwrite("RIFF", 1, 4, f);
            fclose(f);
            createdFile = TRUE;
        }
        createdDirs = TRUE;
    }

    const char* result = FindWavForBik("westlogo.bik", "movies01.mix");
    EXPECT_NE(result, (const char*)NULL);
    if (result) EXPECT_STREQ(result, "BinkWAV\\RA2\\westlogo.wav");

    // No file on disk → auto-resolve fails, no [audio] entry → NULL
    const char* missing = FindWavForBik("no_such_stem.bik", "movies01.mix");
    EXPECT_EQ(missing, (const char*)NULL);

    if (createdFile) DeleteFileA(wavPath.c_str());
    if (createdDirs) {
        RemoveDirectoryA(dir2.c_str());
        RemoveDirectoryA(dir1.c_str());
    }
}

TEST_F(IntegrationConfigTest, RealExceptionPriority) {
    std::string projectDir = ProjectRoot() + "\\";
    lstrcpynA(g_dllDir, projectDir.c_str(), MAX_PATH);
    ResetAudioConfig();
    LoadAudioConfig();

    // base variant: auto path only when file exists; create it temporarily.
    std::string dir1 = ProjectPath("BinkWAV");
    std::string dir2 = ProjectPath("BinkWAV\\RA2");
    std::string wavPath = ProjectPath("BinkWAV\\RA2\\a01_f00e.wav");
    BOOL createdFile = FALSE;
    if (GetFileAttributesA(wavPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA(dir1.c_str(), NULL);
        CreateDirectoryA(dir2.c_str(), NULL);
        FILE* f = NULL;
        fopen_s(&f, wavPath.c_str(), "wb");
        if (f) {
            fwrite("RIFF", 1, 4, f);
            fclose(f);
            createdFile = TRUE;
        }
    }

    const char* exceptionResult = FindWavForBik("a01_f00e.bik", "movies01.mix");
    EXPECT_NE(exceptionResult, (const char*)NULL);
    if (exceptionResult) EXPECT_STREQ(exceptionResult, "BinkWAV\\RA2\\a01_f00e.wav");

    const char* noMixResult = FindWavForBik("a01_f00e.bik", NULL);
    EXPECT_NE(noMixResult, (const char*)NULL);
    if (noMixResult) EXPECT_STREQ(noMixResult, "BinkWAV\\RA2\\a01_f00e.wav");

    if (createdFile) {
        DeleteFileA(wavPath.c_str());
        RemoveDirectoryA(dir2.c_str());
        RemoveDirectoryA(dir1.c_str());
    }
}

// ============================================================================
// 6. WAV Decode — uses GAME_DIR-relative paths
// ============================================================================

TEST(Integration_WavDecode, DecodeRealWavFile) {
    const char* candidates[] = {
        "BinkWAV\\RA2\\westlogo.wav",
        "BinkWAV\\RA2\\a01_f00e.wav",
        "BinkWAV\\RA2YR\\a01_f00e.wav",
    };

    BOOL foundAny = FALSE;

    if (HasGameDir()) {
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
    }

    if (!foundAny) {
        std::string thirdPartyPath = ProjectRoot() + "\\third-party\\a04_f00e.wav";
        DecodedAudio audio = {0};
        if (DecodeAudioFile(thirdPartyPath.c_str(), &audio)) {
            foundAny = TRUE;
            EXPECT_GT(audio.pcmSize, 0u);
            EXPECT_NE(audio.pcmData, (char*)NULL);
            EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
            if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
        }
    }

    if (!foundAny) {
        GTEST_SKIP() << "No WAV files found in " << GameDir() << " or third-party/";
    }
}

// ============================================================================
// 7. Ordinal Table Cross-Validation
// ============================================================================

TEST(Integration_Ordinals, Group5TableMatchesRealDll) {
    std::string dllPath = RealDllPath("binkw32_1.0q.dll");
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    if (!hMod) {
        GTEST_SKIP() << "Cannot load real DLL: " << dllPath;
    }

    for (int ordinal = 1; ordinal <= 53; ordinal++) {
        EXPECT_NE(GetProcAddress(hMod, (LPCSTR)ordinal), (FARPROC)NULL)
            << "Group 5 ordinal " << ordinal << " not found";
    }
    FreeLibrary(hMod);
}

TEST(Integration_Ordinals, Group7TableMatchesRealDll) {
    std::string dllPath = RealDllPath("binkw32_1.9u.dll");
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    if (!hMod) {
        GTEST_SKIP() << "Cannot load real DLL: " << dllPath;
    }

    int keyOrdinals[] = {1, 16, 20, 25, 28, 35, 44, 49, 53, 58, 61, 67, 71, 73};
    for (int ordinal : keyOrdinals) {
        EXPECT_NE(GetProcAddress(hMod, (LPCSTR)ordinal), (FARPROC)NULL)
            << "Group 7 ordinal " << ordinal << " not found";
    }
    FreeLibrary(hMod);
}

// ----------------------------------------------------------------------------
// Name <-> ordinal cross-validation.
//
// The presence checks above pass even when the *mapping* is wrong (any ordinal
// 1..N resolves). These tests parse the two generated tables and compare them
// with the real binaries: src/exports.def against the known export list, and
// src/ordinals.inc against the actual name/ordinal pairs of a real DLL.
// ----------------------------------------------------------------------------

static std::string ReadTextFile(const std::string& path) {
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    if (!f) return "";
    std::string content;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    fclose(f);
    return content;
}

// "    _BinkOpen@8=_sBinkOpen@8 @4" -> ("_BinkOpen@8", 4)
static bool ParseExportsDef(std::vector<std::pair<std::string, int>>& out) {
    std::string text = ReadTextFile(ProjectPath("src\\exports.def"));
    if (text.empty()) return false;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        size_t at = line.rfind(" @");
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t') &&
            eq != std::string::npos && at != std::string::npos && at > eq) {
            size_t nameStart = line.find_first_not_of(" \t");
            std::string name = line.substr(nameStart, eq - nameStart);
            out.push_back(std::make_pair(name, atoi(line.c_str() + at + 2)));
        }
        lineStart = lineEnd + 1;
    }
    return !out.empty();
}

// g_ordinals_groupN[] = { OE(Name, ordinal), ... }
static bool LoadOrdinalTable(int group, std::vector<std::pair<std::string, int>>& out) {
    std::string text = ReadTextFile(ProjectPath("src\\ordinals.inc"));
    if (text.empty()) return false;
    char key[64];
    _snprintf_s(key, sizeof(key), _TRUNCATE, "g_ordinals_group%d[] = {", group);
    size_t start = text.find(key);
    if (start == std::string::npos) return false;
    size_t end = text.find("};", start);
    if (end == std::string::npos) return false;
    std::string body = text.substr(start, end - start);
    size_t pos = 0;
    while ((pos = body.find("OE(", pos)) != std::string::npos) {
        size_t nameStart = pos + 3;
        size_t comma = body.find(',', nameStart);
        size_t close = body.find(')', comma);
        if (comma == std::string::npos || close == std::string::npos) break;
        std::string name = body.substr(nameStart, comma - nameStart);
        size_t nb = name.find_first_not_of(" \t");
        if (nb != std::string::npos) name = name.substr(nb);
        out.push_back(std::make_pair(name, atoi(body.c_str() + comma + 1)));
        pos = close + 1;
    }
    return !out.empty();
}

// Real binkw32 DLLs export stdcall-decorated names (_BinkOpen@8), while
// src/ordinals.inc stores undecorated labels (BinkOpen) - strip the prefix and
// the @N suffix before comparing.
static std::string StripDecoration(const std::string& name) {
    std::string s = name;
    if (!s.empty() && s[0] == '_') s.erase(0, 1);
    size_t at = s.rfind('@');
    if (at != std::string::npos && at + 1 < s.size()) {
        bool digitsOnly = true;
        for (size_t i = at + 1; i < s.size(); i++) {
            if (s[i] < '0' || s[i] > '9') { digitsOnly = false; break; }
        }
        if (digitsOnly) s.erase(at);
    }
    return s;
}

// Name/ordinal pairs straight from the export directory of a loaded image.
static bool GetImageExports(HMODULE hMod, std::vector<std::pair<std::string, int>>& out) {
    BYTE* base = (BYTE*)hMod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0) return false;
    IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    DWORD* names = (DWORD*)(base + exp->AddressOfNames);
    WORD* ordinals = (WORD*)(base + exp->AddressOfNameOrdinals);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char* name = (const char*)(base + names[i]);
        int ordinal = (int)(exp->Base + ordinals[i]);
        out.push_back(std::make_pair(name, ordinal));
    }
    return !out.empty();
}

// Every table entry must resolve to the same name/ordinal pair in the real
// DLL; a presence-only check passes even when the mapping is wrong.
static void CheckTableAgainstRealDll(int group, const char* dllName) {
    std::vector<std::pair<std::string, int>> table;
    ASSERT_TRUE(LoadOrdinalTable(group, table))
        << "cannot parse src/ordinals.inc group " << group;

    std::string dllPath = RealDllPath(dllName);
    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    ASSERT_NE(hMod, (HMODULE)NULL) << "Cannot load real DLL: " << dllPath;

    std::vector<std::pair<std::string, int>> exports;
    bool parsed = GetImageExports(hMod, exports);
    FreeLibrary(hMod);
    ASSERT_TRUE(parsed) << "cannot parse export directory of " << dllPath;

    int missing = 0;
    int mismatched = 0;
    for (const auto& entry : table) {
        bool found = false;
        for (const auto& exp : exports) {
            if (StripDecoration(exp.first) == entry.first) {
                found = true;
                if (exp.second != entry.second) {
                    mismatched++;
                    ADD_FAILURE() << entry.first << ": src/ordinals.inc says ordinal "
                                  << entry.second << ", " << dllName << " exports it at "
                                  << exp.second;
                }
                break;
            }
        }
        if (!found) {
            missing++;
            ADD_FAILURE() << entry.first << " (group " << group
                          << ") is not exported by " << dllName;
        }
    }
    EXPECT_EQ(missing, 0) << missing << " table entries missing from " << dllName;
    EXPECT_EQ(mismatched, 0) << mismatched << " name/ordinal mismatches in " << dllName;
}

TEST(Integration_Ordinals, OrdinalsIncMatchesGroup5Dll) {
    CheckTableAgainstRealDll(5, "binkw32_1.0q.dll");
}

TEST(Integration_Ordinals, OrdinalsIncMatchesGroup7Dll) {
    CheckTableAgainstRealDll(7, "binkw32_1.9u.dll");
}

TEST(Integration_Ordinals, ExportsDefMatchesKnownExportList) {
    std::vector<std::pair<std::string, int>> entries;
    ASSERT_TRUE(ParseExportsDef(entries)) << "cannot parse src/exports.def";

    EXPECT_EQ(entries.size(), sizeof(EXPORT_NAMES) / sizeof(EXPORT_NAMES[0]))
        << "src/exports.def and the test export list must agree on the count";

    // Every listed name must be exported by the def, and vice versa.
    for (const char* name : EXPORT_NAMES) {
        bool found = false;
        for (const auto& e : entries) {
            if (e.first == name) { found = true; break; }
        }
        EXPECT_TRUE(found) << "EXPORT_NAMES entry missing from src/exports.def: " << name;
    }
    for (const auto& e : entries) {
        bool found = false;
        for (const char* name : EXPORT_NAMES) {
            if (e.first == name) { found = true; break; }
        }
        EXPECT_TRUE(found) << "src/exports.def entry missing from EXPORT_NAMES: " << e.first;
    }

    // Ordinals must stay dense and unique (1..N): RA2 imports by name, but the
    // def ordinals are what the proxy exposes to ordinal importers.
    std::vector<int> ordinals;
    for (const auto& e : entries) ordinals.push_back(e.second);
    std::sort(ordinals.begin(), ordinals.end());
    for (size_t i = 0; i < ordinals.size(); i++) {
        EXPECT_EQ(ordinals[i], (int)i + 1)
            << "src/exports.def ordinals must be exactly 1.." << ordinals.size();
    }
}

// ============================================================================
// DLL-side tracking observation
//
// The test EXE links src/binkw32_proxy.cpp as well, so its own g_vidCount
// copy says nothing about the *loaded* proxy DLL (the module keeps its own
// statics — an EXPECT on the EXE copy can never fail). The only state the DLL
// itself writes out is its log: TrackVideo logs "Tracked video:" / "Updated
// video:" exactly when the DLL's counter changes. These helpers capture just
// the part of <dll dir>\binkw32_proxy.log written by one call made through
// the loaded DLL.
// ============================================================================

static std::string ProxyLogPath(const std::string& dllPath) {
    // PROXY_DLL_PATH comes from $<TARGET_FILE>, which CMake may spell with
    // forward slashes — both separators have to be recognised or the helper
    // silently reads a relative "binkw32_proxy.log" in the working directory.
    size_t slash = dllPath.find_last_of("\\/");
    std::string dir = (slash == std::string::npos) ? std::string() : dllPath.substr(0, slash + 1);
    return dir + "binkw32_proxy.log";
}

static std::string ReadSharedTextFile(const std::string& path) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    std::string out;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(h, buf, (DWORD)sizeof(buf), &n, NULL) && n > 0) out.append(buf, n);
    CloseHandle(h);
    return out;
}

// Resets the log before the observed call: the DLL recreates the file on its
// first write (CREATE_ALWAYS), so the tail after the call belongs to this
// session alone. If the file is held open by a still-logging DLL instance the
// delete fails — the pre-call content is remembered and stripped instead.
class ProxyLogTail {
public:
    explicit ProxyLogTail(const std::string& dllPath) : m_path(ProxyLogPath(dllPath)) {
        m_before = ReadSharedTextFile(m_path);
        if (DeleteFileA(m_path.c_str())) m_before.clear();
    }

    std::string Tail() const {
        std::string now = ReadSharedTextFile(m_path);
        if (!m_before.empty() && now.size() >= m_before.size() &&
            now.compare(0, m_before.size(), m_before) == 0) {
            return now.substr(m_before.size());
        }
        return now;
    }

    const std::string& Path() const { return m_path; }

private:
    std::string m_path;
    std::string m_before;
};

// ============================================================================
// 8. End-to-End Proxy Pipeline — BinkOpen → tracking → BinkClose
//
// Verifies the proxy intercepts BinkOpen, sets up video tracking, and
// cleans up on BinkClose. Requires the real Bink DLL (ordinal forwarding).
// ============================================================================

typedef intptr_t (__stdcall *BinkOpenFn)(const char*, DWORD);
typedef void     (__stdcall *BinkCloseFn)(void*);
typedef intptr_t (__stdcall *BinkGetErrorFn)();

TEST(Integration_ProxyPipeline, BinkOpenCloseTracking) {
    std::string dllPath;
    ProxyDllStatus st;
    HMODULE hProxy = LoadProxyDllForTest(dllPath, st);
    if (st == ProxyDllStatus::Skipped) {
        GTEST_SKIP() << "proxy DLL not present: " << dllPath << " (BINK_TEST_ALLOW_NO_PROXY set)";
    }
    ASSERT_NE(hProxy, (HMODULE)NULL) << "proxy DLL not loadable: " << dllPath
                                     << " (error " << GetLastError()
                                     << ") - build group 5/7 first, or set BINK_TEST_ALLOW_NO_PROXY=1 to skip";

    auto pOpen  = (BinkOpenFn)GetProcAddress(hProxy, "_BinkOpen@8");
    auto pClose = (BinkCloseFn)GetProcAddress(hProxy, "_BinkClose@4");
    ASSERT_NE(pOpen, (BinkOpenFn)NULL);
    ASSERT_NE(pClose, (BinkCloseFn)NULL);

    std::string bikPath = ProjectRoot() + "\\third-party\\s03_f00e.bik";
    DWORD attr = GetFileAttributesA(bikPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        FreeLibrary(hProxy);
        GTEST_SKIP() << "Test .bik not found: " << bikPath;
    }

    // The real Bink DLL is copied next to the proxy by the POST_BUILD step
    // (root CMakeLists.txt, from Real/) and the proxy looks for it next to the
    // EXE first, then next to itself — NULL for an existing .bik is a
    // build/regression problem, so it must go red, not skip. Skipping stays
    // only for the legitimate "group not built" case handled above by
    // LoadProxyDllForTest (BINK_TEST_ALLOW_NO_PROXY=1).
    ProxyLogTail probe(dllPath);
    intptr_t handle = pOpen(bikPath.c_str(), 0);
    if (handle == 0) {
        std::string err;
        BinkGetErrorFn pGetError = (BinkGetErrorFn)GetProcAddress(hProxy, "_BinkGetError@0");
        if (pGetError) {
            const char* e = (const char*)pGetError();
            if (e && e[0]) { err = "; BinkGetError: "; err += e; }
        }
        FreeLibrary(hProxy);
        FAIL() << "BinkOpen returned NULL for an existing test .bik: " << bikPath
               << " - the real Bink DLL must be present next to the proxy "
                  "(POST_BUILD copy from Real/); build the group first" << err;
    }

    // Positive control for the DLL-side observation used in
    // BinkOpenInvalidFileReturnsNull: a successful open must show up in the
    // DLL's own log, otherwise "nothing tracked after a failed open" would be
    // a blind (vacuously green) check.
    std::string tail = probe.Tail();
    EXPECT_TRUE(tail.find("Tracked video:") != std::string::npos ||
                tail.find("Updated video:") != std::string::npos)
        << "the proxy DLL opened the file but its log shows no tracking entry "
           "(log: " << probe.Path() << "), DLL log tail:\n" << tail;

    // Internal tracking lives inside the loaded DLL module; the counter itself
    // is covered by the log check above, and the handle is verified via the
    // exported BinkGetSummary, then the close cycle.
    typedef void (__stdcall *BinkGetSummaryFn)(void*, void*);
    BinkGetSummaryFn pSum = (BinkGetSummaryFn)GetProcAddress(hProxy, "_BinkGetSummary@8");
    if (pSum) {
        unsigned char summary[128] = {};
        pSum((void*)handle, summary);
        uint32_t width = 0;
        memcpy(&width, summary, sizeof(width));
        EXPECT_GT(width, 0u) << "BinkGetSummary reported zero width for a live handle";
    }

    pClose((void*)handle);

    FreeLibrary(hProxy);
}

TEST(Integration_ProxyPipeline, BinkOpenInvalidFileReturnsNull) {
    std::string dllPath;
    ProxyDllStatus st;
    HMODULE hProxy = LoadProxyDllForTest(dllPath, st);
    if (st == ProxyDllStatus::Skipped) {
        GTEST_SKIP() << "proxy DLL not present: " << dllPath << " (BINK_TEST_ALLOW_NO_PROXY set)";
    }
    ASSERT_NE(hProxy, (HMODULE)NULL) << "proxy DLL not loadable: " << dllPath
                                     << " (error " << GetLastError()
                                     << ") - build group 5/7 first, or set BINK_TEST_ALLOW_NO_PROXY=1 to skip";

    auto pOpen = (BinkOpenFn)GetProcAddress(hProxy, "_BinkOpen@8");
    ASSERT_NE(pOpen, (BinkOpenFn)NULL);

    // Resets the proxy log so everything written below is from OUR call.
    ProxyLogTail probe(dllPath);

    // Non-existent file should return NULL
    intptr_t handle = pOpen("Z:\\nonexistent.bik", 0);
    EXPECT_EQ(handle, (intptr_t)0) << "BinkOpen should return NULL for missing file";

    // State check through the loaded DLL. Its g_vidCount lives inside the
    // module — the EXE-side copy this test asserted before was a different
    // variable entirely and could never catch a regression. TrackVideo logs
    // "Tracked video:" / "Updated video:" exactly when it increments the DLL's
    // counter, so the absence of those lines after OUR call proves nothing was
    // tracked.
    std::string tail = probe.Tail();
    EXPECT_NE(tail.find("BinkOpen"), std::string::npos)
        << "no trace of our call in the proxy log " << probe.Path()
        << " — logging disabled ([log] in binkw32.cfg)? DLL-side tracking state "
           "cannot be observed otherwise, refusing to pass green";
    EXPECT_EQ(tail.find("Tracked video:"), std::string::npos)
        << "the loaded proxy DLL tracked a video after a FAILED BinkOpen; "
           "DLL log tail:\n" << tail;
    EXPECT_EQ(tail.find("Updated video:"), std::string::npos)
        << "the loaded proxy DLL re-tracked a video after a FAILED BinkOpen; "
           "DLL log tail:\n" << tail;

    FreeLibrary(hProxy);
}
