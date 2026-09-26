#include <gtest/gtest.h>
#include "test_helpers.h"
#include <cstdio>
#include <string>

// ============================================================================
// test_uncovered.cpp — Tests for TrackVideo, LogCallStack, EnsureInitialized,
//                      sBinkCopyToBuffer scaling, sBinkClose
// ============================================================================

extern BOOL EnsureInitialized();
extern void LogCallStack(int skip);

// ============================================================================
// Global init-state guard
//
// Every proxy stub calls EnsureInitialized() before forwarding.
// Tests drive the stubs directly with mock function pointers, so the deferred
// loader must not run behind their back: pin the state to "already initialized"
// for the whole run. Tests that exercise the loader itself flip it back to 0.
// ============================================================================

namespace {
class InitStateGuard : public ::testing::Environment {
public:
    void SetUp() override {
        saved = g_initState;
        g_initState = 2;
    }
    void TearDown() override { g_initState = saved; }
    LONG saved = 0;
};
::testing::Environment* const g_initStateGuard =
    ::testing::AddGlobalTestEnvironment(new InitStateGuard());
}

// ============================================================================
// Mock BinkGetSummary
// ============================================================================

int g_mW = 0, g_mH = 0, g_mFR = 0, g_mFRD = 0;

void __stdcall MockSummary(void* handle, void* summary) {
    uint8_t* s = (uint8_t*)summary;
    memset(s, 0, 128);
    *(uint32_t*)(s + 0) = g_mW;
    *(uint32_t*)(s + 4) = g_mH;
    *(uint32_t*)(s + 20) = g_mFR;
    *(uint32_t*)(s + 24) = g_mFRD;
}

// ============================================================================
// Mock BinkCopyToBuffer — fills destination with 0x1234 pattern
// ============================================================================

int g_copyCalled = 0;

intptr_t __stdcall MockCopyToBuffer(void* bink, void* dst, void* pitch,
                                     void* height, void* x, void* y, void* flags) {
    g_copyCalled++;
    if (dst && pitch && height) {
        int p = (int)(intptr_t)pitch;
        int h = (int)(intptr_t)height;
        uint16_t* buf = (uint16_t*)dst;
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < p / 2; col++) {
                buf[row * (p / 2) + col] = 0x1234;
            }
        }
    }
    return 1;
}

// ============================================================================
// TrackVideo tests
// ============================================================================

class TrackVideoTest : public ::testing::Test {
protected:
    void* savedSummary;
    int savedCount;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedCount = g_vidCount;
        pBinkGetSummary = (void*)MockSummary;
        g_vidCount = 0;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        pBinkGetSummary = savedSummary;
        g_vidCount = savedCount;
    }
};

TEST_F(TrackVideoTest, AddsNewEntry) {
    void* h = (void*)0x1000;
    TrackVideo(h, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 1);
    EXPECT_EQ(g_vids[0].handle, h);
    EXPECT_EQ(g_vids[0].width, 640u);
    EXPECT_EQ(g_vids[0].height, 480u);
}

TEST_F(TrackVideoTest, UpdatesExistingEntry) {
    void* h = (void*)0x1000;
    TrackVideo(h, "test.bik", NULL);
    g_mW = 1920;
    g_mH = 1080;
    TrackVideo(h, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 1);
    EXPECT_EQ(g_vids[0].width, 1920u);
    EXPECT_EQ(g_vids[0].height, 1080u);
}

TEST_F(TrackVideoTest, MultipleHandles) {
    void* h1 = (void*)0x1000;
    void* h2 = (void*)0x2000;
    void* h3 = (void*)0x3000;
    TrackVideo(h1, "a.bik", NULL);
    TrackVideo(h2, "b.bik", NULL);
    TrackVideo(h3, "c.bik", NULL);
    EXPECT_EQ(g_vidCount, 3);
}

TEST_F(TrackVideoTest, MaxCapacity) {
    for (int i = 0; i < 32; i++)
        TrackVideo((void*)(uintptr_t)(0x1000 + i * 0x100), "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 32);
    TrackVideo((void*)0x5000, "overflow.bik", NULL);
    EXPECT_EQ(g_vidCount, 32);
}

TEST_F(TrackVideoTest, NullHandleSkipped) {
    TrackVideo(NULL, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 0);
}

TEST_F(TrackVideoTest, NullSummaryFnSkipped) {
    pBinkGetSummary = NULL;
    TrackVideo((void*)0x1000, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 0);
}

TEST_F(TrackVideoTest, ZeroDimensionsSkipped) {
    g_mW = 0;
    g_mH = 0;
    TrackVideo((void*)0x1000, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 0);
}

TEST_F(TrackVideoTest, InitializesFields) {
    TrackVideo((void*)0x1000, "test.bik", NULL);
    ASSERT_NE(g_vids[0].scale, (ScaleBufs*)NULL);
    EXPECT_EQ(g_vids[0].scale->tempBuf, (void*)NULL);
    EXPECT_EQ(g_vids[0].scale->lookupX, (int*)NULL);
    EXPECT_EQ(g_vids[0].wavPlayer, (WavPlayer*)NULL);
    EXPECT_STREQ(g_vids[0].wavPath, "");
}

TEST_F(TrackVideoTest, StoresFrameRate) {
    g_mFR = 60;
    g_mFRD = 2;
    TrackVideo((void*)0x1000, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 1);
}

TEST_F(TrackVideoTest, SkipsWavPathWhenFileNotFound) {
    // Set up an audio mapping that points to a non-existent file
    int savedMapCount = g_audioMapCount;
    AudioMap savedMaps[MAX_AUDIO_MAPS];
    memcpy(savedMaps, g_audioMaps, sizeof(savedMaps));

    g_audioMapCount = 1;
    strncpy_s(g_audioMaps[0].bikName, sizeof(g_audioMaps[0].bikName), "test.bik", _TRUNCATE);
    strncpy_s(g_audioMaps[0].wavPath, sizeof(g_audioMaps[0].wavPath),
              "BinkWAV\\nonexistent_file.wav", _TRUNCATE);

    TrackVideo((void*)0x1000, "test.bik", NULL);

    EXPECT_EQ(g_vidCount, 1);
    EXPECT_STREQ(g_vids[0].wavPath, "") << "wavPath should be empty when replacement file not found";

    // Restore
    g_audioMapCount = savedMapCount;
    memcpy(g_audioMaps, savedMaps, sizeof(savedMaps));
}

TEST_F(TrackVideoTest, KeepsWavPathWhenFileExists) {
    // Use the real test WAV file from third-party
    int savedMapCount = g_audioMapCount;
    AudioMap savedMaps[MAX_AUDIO_MAPS];
    memcpy(savedMaps, g_audioMaps, sizeof(savedMaps));

    char savedDllDir[MAX_PATH];
    memcpy(savedDllDir, g_dllDir, MAX_PATH);

    // Set g_dllDir to project root so third-party/ path resolves
    _snprintf_s(g_dllDir, sizeof(g_dllDir), _TRUNCATE, "%s\\", ProjectRootDir());

    g_audioMapCount = 1;
    strncpy_s(g_audioMaps[0].bikName, sizeof(g_audioMaps[0].bikName), "test.bik", _TRUNCATE);
    strncpy_s(g_audioMaps[0].wavPath, sizeof(g_audioMaps[0].wavPath),
              "third-party\\a04_f00e.wav", _TRUNCATE);

    TrackVideo((void*)0x1000, "test.bik", NULL);

    EXPECT_EQ(g_vidCount, 1);
    EXPECT_STREQ(g_vids[0].wavPath, "third-party\\a04_f00e.wav")
        << "wavPath should be set when replacement file exists";

    // Restore
    g_audioMapCount = savedMapCount;
    memcpy(g_audioMaps, savedMaps, sizeof(savedMaps));
    lstrcpynA(g_dllDir, savedDllDir, MAX_PATH);
}

// ============================================================================
// LogCallStack tests
//
// LogCallStack() only reports through the logger, so each test routes the log
// into its own file under tests/data and counts the entries it produced -
// "the call returned" alone would never fail.
// ============================================================================

namespace {
class LogCapture {
public:
    LogCapture() {
        ShutdownLog();                       // close whatever the previous test left open
        savedEnabled = g_logEnabled;
        memcpy(savedDir, g_dllDir, MAX_PATH);
        _snprintf_s(dir, sizeof(dir), _TRUNCATE, "%s\\", TestDataDir());
        lstrcpynA(g_dllDir, dir, MAX_PATH);
        path = std::string(g_dllDir) + "binkw32_proxy.log";
        DeleteFileA(path.c_str());
        g_logEnabled = TRUE;
        InitLog();
    }
    ~LogCapture() {
        ShutdownLog();
        g_logEnabled = savedEnabled;
        lstrcpynA(g_dllDir, savedDir, MAX_PATH);
    }
    int Count(const char* needle) const {
        // CreateFile with read+write sharing: the logger keeps the file open
        // for writing, which the CRT fopen() sharing default rejects.
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return -1;
        std::string content;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n > 0) content.append(buf, n);
        CloseHandle(h);
        int count = 0;
        size_t pos = 0;
        while ((pos = content.find(needle, pos)) != std::string::npos) {
            count++;
            pos += strlen(needle);
        }
        return count;
    }
    std::string Path() const { return path; }

private:
    BOOL savedEnabled;
    char savedDir[MAX_PATH];
    char dir[MAX_PATH];
    std::string path;
};
}

TEST(LogCallStackTest, DoesNotCrash) {
    LogCapture log;
    LogCallStack(0);
    LogCallStack(1);
    LogCallStack(2);
    EXPECT_GE(log.Count("Call stack:"), 1) << "each captured stack must be logged";
}

TEST(LogCallStackTest, LargeSkip) {
    LogCapture log;
    LogCallStack(0);
    EXPECT_EQ(log.Count("Call stack:"), 1);

    // skip=100 exceeds the captured depth: frames == 0 -> early return,
    // nothing may be appended.
    LogCallStack(100);
    EXPECT_EQ(log.Count("Call stack:"), 1) << "over-skipped capture must not log";
}

// ============================================================================
// EnsureInitialized tests
// ============================================================================

TEST(EnsureInitializedTest, Idempotent) {
    LONG saved = g_initState;
    g_initState = 0;
    EnsureInitialized();
    LONG afterFirst = g_initState;
    EXPECT_NE(afterFirst, 0) << "first call must advance the state";
    EnsureInitialized();
    EnsureInitialized();
    EXPECT_EQ(g_initState, afterFirst) << "later calls must not change the state";
    g_initState = saved;
}

// Every stub must kick off the deferred loader, not just BinkOpen.
// sBinkGetError is used because it is argument-free and side-effect free even
// if the real DLL did get resolved.
extern "C" intptr_t __stdcall sBinkGetError();

TEST(EnsureInitializedTest, StubTriggersInit) {
    LONG saved = g_initState;
    g_initState = 0;
    sBinkGetError();
    EXPECT_NE(g_initState, 0) << "stubs must call EnsureInitialized before forwarding";
    g_initState = saved;
}

// ============================================================================
// sBinkCopyToBuffer scaling tests — call sBinkCopyToBuffer (proxy), not mock
// ============================================================================

class ScalingTest : public ::testing::Test {
protected:
    void* savedCopy;
    void* savedSummary;
    int savedCount;

    void SetUp() override {
        savedCopy = pBinkCopyToBuffer;
        savedSummary = pBinkGetSummary;
        savedCount = g_vidCount;
        pBinkCopyToBuffer = (void*)MockCopyToBuffer;
        pBinkGetSummary = (void*)MockSummary;
        g_vidCount = 0;
        g_copyCalled = 0;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].scale) { ScaleBufsUnref(g_vids[i].scale); g_vids[i].scale = NULL; }
        }
        pBinkCopyToBuffer = savedCopy;
        pBinkGetSummary = savedSummary;
        g_vidCount = savedCount;
    }
};

TEST_F(ScalingTest, NoScalingWhenSameSize) {
    g_mW = 640;
    g_mH = 480;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 640 * 2;
    uint16_t* dstBuf = (uint16_t*)calloc(1, dstPitch * 480);

    g_copyCalled = 0;
    intptr_t result = sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)0, (void*)0, (void*)2);

    EXPECT_EQ(result, 1);
    EXPECT_EQ(g_copyCalled, 1);
    free(dstBuf);
}

TEST_F(ScalingTest, ScalingWhenSmallerDestination) {
    g_mW = 1920;
    g_mH = 1080;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 640 * 2;
    uint16_t* dstBuf = (uint16_t*)calloc(1, dstPitch * 480);

    g_copyCalled = 0;
    intptr_t result = sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)0, (void*)0, (void*)2);

    EXPECT_EQ(result, 1);
    EXPECT_EQ(g_copyCalled, 1);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    ASSERT_NE(vi->scale, (ScaleBufs*)NULL);
    EXPECT_NE(vi->scale->tempBuf, (void*)NULL);
    EXPECT_NE(vi->scale->lookupX, (int*)NULL);
    EXPECT_NE(vi->scale->lookupY, (int*)NULL);
    EXPECT_GT(vi->scale->tableW, 0);
    EXPECT_GT(vi->scale->tableH, 0);
    free(dstBuf);
}

TEST_F(ScalingTest, ScalingLookupTablesCorrect) {
    g_mW = 800;
    g_mH = 600;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 400 * 2;
    uint16_t* dstBuf = (uint16_t*)calloc(1, dstPitch * 300);

    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)300, (void*)0, (void*)0, (void*)2);

    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    ASSERT_NE(vi->scale, (ScaleBufs*)NULL);
    EXPECT_EQ(vi->scale->tableW, 400);
    EXPECT_EQ(vi->scale->tableH, 300);

    for (int x = 0; x < 400; x++) {
        EXPECT_GE(vi->scale->lookupX[x], 0);
        EXPECT_LT(vi->scale->lookupX[x], 800);
    }
    for (int y = 0; y < 300; y++) {
        EXPECT_GE(vi->scale->lookupY[y], 0);
        EXPECT_LT(vi->scale->lookupY[y], 600);
    }
    free(dstBuf);
}

TEST_F(ScalingTest, ScalingWithOffset) {
    g_mW = 1920;
    g_mH = 1080;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 800 * 2;
    uint16_t* dstBuf = (uint16_t*)calloc(1, dstPitch * 600);

    g_copyCalled = 0;
    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)600, (void*)100, (void*)50, (void*)2);

    EXPECT_EQ(g_copyCalled, 1);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    ASSERT_NE(vi->scale, (ScaleBufs*)NULL);
    EXPECT_NE(vi->scale->tempBuf, (void*)NULL);
    free(dstBuf);
}

TEST_F(ScalingTest, NoScalingForNonRgb565) {
    g_mW = 1920;
    g_mH = 1080;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 640 * 3;
    uint8_t* dstBuf = (uint8_t*)calloc(1, dstPitch * 480);

    g_copyCalled = 0;
    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)0, (void*)0, (void*)0);

    EXPECT_EQ(g_copyCalled, 1);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    ASSERT_NE(vi->scale, (ScaleBufs*)NULL);
    EXPECT_EQ(vi->scale->tempBuf, (void*)NULL) << "non-RGB565 path must not allocate the scratch buffer";
    free(dstBuf);
}

TEST_F(ScalingTest, TempBufferCached) {
    g_mW = 1920;
    g_mH = 1080;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 640 * 2;
    uint16_t* dstBuf = (uint16_t*)calloc(1, dstPitch * 480);

    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)0, (void*)0, (void*)2);

    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    ASSERT_NE(vi->scale, (ScaleBufs*)NULL);
    void* firstBuf = vi->scale->tempBuf;
    ASSERT_NE(firstBuf, (void*)NULL);

    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)0, (void*)0, (void*)2);

    EXPECT_EQ(vi->scale->tempBuf, firstBuf);
    free(dstBuf);
}

TEST_F(ScalingTest, NegativeDestXYSkipsScaling) {
    g_mW = 1920;
    g_mH = 1080;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    int dstPitch = 640 * 2;
    uint16_t* dstBuf = (uint16_t*)calloc(1, dstPitch * 480);

    g_copyCalled = 0;
    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)(intptr_t)(-1), (void*)0, (void*)2);

    EXPECT_EQ(g_copyCalled, 1);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    ASSERT_NE(vi->scale, (ScaleBufs*)NULL);
    EXPECT_EQ(vi->scale->tempBuf, (void*)NULL) << "negative dest must not allocate the scratch buffer";
    free(dstBuf);
}

// ============================================================================
// sBinkClose — tests UntrackVideo integration via proxy
// ============================================================================

TEST(SBinkCloseTest, UntracksVideo) {
    void* savedSummary = pBinkGetSummary;
    void* savedClose = pBinkClose;
    int savedCount = g_vidCount;

    pBinkGetSummary = (void*)MockSummary;
    g_vidCount = 0;
    g_mW = 640;
    g_mH = 480;

    TrackVideo((void*)0x1000, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 1);

    pBinkClose = NULL;
    sBinkClose((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);

    pBinkClose = savedClose;
    pBinkGetSummary = savedSummary;
    g_vidCount = savedCount;
}

// ============================================================================
// BinkSetSoundTrack adapter tests
//
// The real DLL exports this function at two arities (@8 in generations with
// the track-list form, @4 with the single-value form). Forwarding the wrong
// one unbalances the caller's stack — the proxy crashed with 0xC0000005 until
// the arity was probed at load. Every combination of stub and real arity is
// covered here.
// ============================================================================

namespace {
int g_stCalls = 0;
void* g_stA = NULL;
void* g_stB = NULL;
void* g_stBValue = NULL;
bool g_stCaptureBValue = false;
void __stdcall MockSetSoundTrack8(void* a, void* b) {
    g_stCalls++;
    g_stA = a;
    g_stB = b;
    // Captured while the callee's frame is still live: the local the pointer
    // refers to is dead as soon as the stub returns, and gtest's own stream
    // machinery would overwrite it before EXPECT read it. Opt-in — other tests
    // pass sentinel values such as (void*)7 that must not be dereferenced.
    if (g_stCaptureBValue && b) g_stBValue = *(void**)b;
}
void __stdcall MockSetSoundTrack4(void* a) {
    g_stCalls++;
    g_stA = a;
    g_stB = (void*)0xDEAD;   // must never be written by a 1-arg forward
    g_stBValue = NULL;
}
}

class SoundTrackArityGuard {
public:
    SoundTrackArityGuard() : savedArity(g_soundTrackArity) {}
    ~SoundTrackArityGuard() {
        g_soundTrackArity = savedArity;
        g_stCaptureBValue = false;   // also on the ASSERT-abort path
    }
private:
    int savedArity;
};

TEST(SoundTrackAdapterTest, TwoArgRealReceivesBothArguments) {
    SoundTrackArityGuard guard;
    void* saved = pBinkSetSoundTrack;
    g_stCalls = 0;
    g_stA = g_stB = g_stBValue = NULL;
    pBinkSetSoundTrack = (void*)&MockSetSoundTrack8;
    g_soundTrackArity = 8;

    sBinkSetSoundTrack8((void*)0x1000, (void*)0x7);

    EXPECT_EQ(g_stCalls, 1);
    EXPECT_EQ(g_stA, (void*)0x1000);
    EXPECT_EQ(g_stB, (void*)0x7);

    pBinkSetSoundTrack = saved;
}

TEST(SoundTrackAdapterTest, SingleArgRealReceivesTheFirstRequestedTrack) {
    SoundTrackArityGuard guard;
    void* saved = pBinkSetSoundTrack;
    g_stCalls = 0;
    g_stA = g_stB = NULL;
    pBinkSetSoundTrack = (void*)&MockSetSoundTrack4;
    g_soundTrackArity = 4;
    // @8 carries (count, list); the single-value real must see list[0], not
    // the count — otherwise "play these 3 tracks" is stored as track 3.
    DWORD track = 7;
    void* list = &track;

    sBinkSetSoundTrack8((void*)3, list);

    ASSERT_EQ(g_stCalls, 1) << "single-argument real must still be called once";
    EXPECT_EQ(g_stA, (void*)7);
    EXPECT_EQ(g_stB, (void*)0xDEAD) << "no second argument may be written";

    pBinkSetSoundTrack = saved;
}

TEST(SoundTrackAdapterTest, FourByteStubToTwoArgRealPassesOneRealEntry) {
    SoundTrackArityGuard guard;
    void* saved = pBinkSetSoundTrack;
    g_stCalls = 0;
    g_stA = g_stB = g_stBValue = NULL;
    g_stCaptureBValue = true;
    pBinkSetSoundTrack = (void*)&MockSetSoundTrack8;
    g_soundTrackArity = 8;

    sBinkSetSoundTrack4((void*)0x1000);

    ASSERT_EQ(g_stCalls, 1);
    EXPECT_EQ(g_stA, (void*)1) << "count must be one real entry";
    EXPECT_TRUE(g_stB != NULL) << "the list pointer must never be NULL";
    EXPECT_EQ(g_stBValue, (void*)0x1000);

    g_stCaptureBValue = false;
    pBinkSetSoundTrack = saved;
}

TEST(SoundTrackAdapterTest, FourByteStubToFourByteRealPassesTheValue) {
    SoundTrackArityGuard guard;
    void* saved = pBinkSetSoundTrack;
    g_stCalls = 0;
    g_stA = g_stB = g_stBValue = NULL;
    pBinkSetSoundTrack = (void*)&MockSetSoundTrack4;
    g_soundTrackArity = 4;

    sBinkSetSoundTrack4((void*)0x1000);

    EXPECT_EQ(g_stCalls, 1);
    EXPECT_EQ(g_stA, (void*)0x1000);

    pBinkSetSoundTrack = saved;
}

TEST(SoundTrackAdapterTest, NullForwardPointerIsIgnored) {
    SoundTrackArityGuard guard;
    void* saved = pBinkSetSoundTrack;
    g_stCalls = 0;
    pBinkSetSoundTrack = NULL;

    sBinkSetSoundTrack8((void*)0x1000, (void*)0x7);
    sBinkSetSoundTrack4((void*)0x1000);

    EXPECT_EQ(g_stCalls, 0) << "no forward pointer must mean no call";

    pBinkSetSoundTrack = saved;
}

// ============================================================================
// YUV blit arity adaptation
//
// The same class of defect: our export is decorated @48, but the real DLL may
// carry @36..@60. The forward must push exactly what the callee pops; an
// unknown arity must be dropped rather than guessed.
// ============================================================================

namespace {
int g_yuvCalls = 0;
void* g_yuvA = NULL;
void* g_yuvI = NULL;
void* g_yuvL = NULL;
void __stdcall MockYuv12(void* a, void* b, void* c, void* d, void* e, void* f,
                         void* g, void* h, void* i, void* j, void* k, void* l) {
    g_yuvCalls++;
    g_yuvA = a;
    g_yuvI = i;
    g_yuvL = l;
}
void __stdcall MockYuv9(void* a, void* b, void* c, void* d, void* e, void* f,
                        void* g, void* h, void* i) {
    g_yuvCalls++;
    g_yuvA = a;
    g_yuvI = i;
    g_yuvL = (void*)0xDEAD;
}
}

TEST(YuvArityTest, ForwardsWithTheRealArity) {
    void* savedPtr = pYUV_blit_16bpp;
    int savedArity = g_yuvArity[YUV_A_16bpp];
    g_yuvCalls = 0;
    g_yuvA = g_yuvI = g_yuvL = NULL;
    pYUV_blit_16bpp = (void*)&MockYuv12;
    g_yuvArity[YUV_A_16bpp] = 48;

    sYUV_blit_16bpp((void*)1, (void*)2, (void*)3, (void*)4, (void*)5, (void*)6,
                    (void*)7, (void*)8, (void*)9, (void*)10, (void*)11, (void*)12);

    ASSERT_EQ(g_yuvCalls, 1);
    EXPECT_EQ(g_yuvA, (void*)1);
    EXPECT_EQ(g_yuvI, (void*)9);
    EXPECT_EQ(g_yuvL, (void*)12);

    pYUV_blit_16bpp = savedPtr;
    g_yuvArity[YUV_A_16bpp] = savedArity;
}

TEST(YuvArityTest, NarrowerRealDropsTheTrailingArguments) {
    void* savedPtr = pYUV_blit_16bpp;
    int savedArity = g_yuvArity[YUV_A_16bpp];
    g_yuvCalls = 0;
    g_yuvA = g_yuvI = g_yuvL = NULL;
    pYUV_blit_16bpp = (void*)&MockYuv9;
    g_yuvArity[YUV_A_16bpp] = 36;

    sYUV_blit_16bpp((void*)1, (void*)2, (void*)3, (void*)4, (void*)5, (void*)6,
                    (void*)7, (void*)8, (void*)9, (void*)10, (void*)11, (void*)12);

    ASSERT_EQ(g_yuvCalls, 1) << "a 9-argument real must be called with 9 arguments";
    EXPECT_EQ(g_yuvA, (void*)1);
    EXPECT_EQ(g_yuvI, (void*)9);
    EXPECT_EQ(g_yuvL, (void*)0xDEAD) << "arguments past the callee's arity must not be read";

    pYUV_blit_16bpp = savedPtr;
    g_yuvArity[YUV_A_16bpp] = savedArity;
}

TEST(YuvArityTest, UnknownArityIsDroppedInsteadOfGuessed) {
    void* savedPtr = pYUV_blit_16bpp;
    int savedArity = g_yuvArity[YUV_A_16bpp];
    g_yuvCalls = 0;
    pYUV_blit_16bpp = (void*)&MockYuv12;
    g_yuvArity[YUV_A_16bpp] = 0;

    sYUV_blit_16bpp((void*)1, (void*)2, (void*)3, (void*)4, (void*)5, (void*)6,
                    (void*)7, (void*)8, (void*)9, (void*)10, (void*)11, (void*)12);

    EXPECT_EQ(g_yuvCalls, 0) << "unknown arity must not be forwarded";

    pYUV_blit_16bpp = savedPtr;
    g_yuvArity[YUV_A_16bpp] = savedArity;
}

TEST(YuvArityTest, NullForwardPointerIsIgnored) {
    void* savedPtr = pYUV_blit_16bpp;
    int savedArity = g_yuvArity[YUV_A_16bpp];
    g_yuvCalls = 0;
    pYUV_blit_16bpp = NULL;

    sYUV_blit_16bpp((void*)1, (void*)2, (void*)3, (void*)4, (void*)5, (void*)6,
                    (void*)7, (void*)8, (void*)9, (void*)10, (void*)11, (void*)12);

    EXPECT_EQ(g_yuvCalls, 0);

    pYUV_blit_16bpp = savedPtr;
    g_yuvArity[YUV_A_16bpp] = savedArity;
}

// ============================================================================
// ExtractFileName tests
// ============================================================================

TEST(ExtractFileNameTest, PlainStringPath) {
    char out[MAX_PATH] = "";
    const char* path = "test_file.mix";
    ExtractFileName((void*)path, 0, out, sizeof(out));
    EXPECT_STREQ(out, "test_file.mix");
}

TEST(ExtractFileNameTest, NullPointerReturnsEmpty) {
    char out[MAX_PATH] = "pre-filled";
    ExtractFileName(NULL, 0, out, sizeof(out));
    EXPECT_STREQ(out, "");
}

TEST(ExtractFileNameTest, InternalFlag0x04000000ReturnsEmpty) {
    char out[MAX_PATH] = "pre-filled";
    ExtractFileName((void*)0x1234, 0x04000000, out, sizeof(out));
    EXPECT_STREQ(out, "");
}

TEST(ExtractFileNameTest, EmptyStringReturnsEmpty) {
    char out[MAX_PATH] = "";
    ExtractFileName((void*)"", 0, out, sizeof(out));
    EXPECT_STREQ(out, "");
}

TEST(ExtractFileNameTest, TruncatesLongPath) {
    char out[10] = "";
    const char* path = "very_long_filename_that_exceeds_buffer.txt";
    ExtractFileName((void*)path, 0, out, sizeof(out));
    EXPECT_LT(strlen(out), sizeof(out));
    EXPECT_STREQ(out, "very_long");
}

// ============================================================================
// Mock BinkSetVolume / BinkSetSoundOnOff — record calls for verification
// ============================================================================

int g_setVolumeCalled = 0;
void* g_setVolumeHandle = NULL;
void* g_setVolumeArg = NULL;

intptr_t __stdcall MockSetVolume(void* a, void* b) {
    g_setVolumeCalled++;
    g_setVolumeHandle = a;
    g_setVolumeArg = b;
    return 0;
}

int g_setSoundOnOffCalled = 0;
void* g_setSoundOnOffHandle = NULL;
void* g_setSoundOnOffArg = NULL;

void __stdcall MockSetSoundOnOff(void* a, void* b) {
    g_setSoundOnOffCalled++;
    g_setSoundOnOffHandle = a;
    g_setSoundOnOffArg = b;
}

// ============================================================================
// sBinkPause tests
// ============================================================================

class SBinkPauseTest : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedPause;
    int savedCount;
    WavPlayer savedPlayers[MAX_WAV_PLAYERS];
    int savedPlayerCount;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedPause = pBinkPause;
        savedCount = g_vidCount;
        memcpy(savedPlayers, g_players, sizeof(g_players));
        savedPlayerCount = g_playerCount;

        pBinkGetSummary = (void*)MockSummary;
        pBinkPause = NULL;
        g_vidCount = 0;
        g_playerCount = 0;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
        }
        for (int i = 0; i < g_playerCount; i++) {
            if (g_players[i].hWave) {
                FreePlayer(&g_players[i]);
            }
        }
        pBinkGetSummary = savedSummary;
        pBinkPause = savedPause;
        g_vidCount = savedCount;
        memcpy(g_players, savedPlayers, sizeof(g_players));
        g_playerCount = savedPlayerCount;
    }

    WavPlayer* SetupVideoWithPlayer(void* handle) {
        TrackVideo(handle, "test.bik", NULL);
        VideoInfo* vi = FindVideo(handle);
        if (!vi) return NULL;
        WavPlayer* pl = AllocPlayer();
        if (!pl) return NULL;
        vi->wavPlayer = pl;
        pl->hWave = (HWAVEOUT)0x12345678;
        pl->playing = TRUE;
        pl->paused = FALSE;
        pl->format.nSamplesPerSec = 22050;
        pl->format.wBitsPerSample = 16;
        pl->format.nChannels = 2;
        pl->format.nBlockAlign = 4;
        return pl;
    }
};

namespace {
int g_pauseCalls = 0;
void* g_pauseA = NULL;
void* g_pauseB = NULL;
void __stdcall MockPause(void* a, void* b) {
    g_pauseCalls++;
    g_pauseA = a;
    g_pauseB = b;
}
}

TEST_F(SBinkPauseTest, PauseWithPlayer) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);
    EXPECT_FALSE(pl->paused);

    sBinkPause(handle, (void*)1);
    EXPECT_TRUE(pl->paused);
}

TEST_F(SBinkPauseTest, ResumeWithPlayer) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->paused = TRUE;

    sBinkPause(handle, (void*)0);
    EXPECT_FALSE(pl->paused);
}

TEST_F(SBinkPauseTest, PauseWithoutPlayer) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);
    int before = g_playerCount;

    sBinkPause(handle, (void*)1);

    VideoInfo* vi = FindVideo(handle);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_TRUE(vi->pauseRequested)
        << "pause request must be remembered until the player exists";
    EXPECT_EQ(g_playerCount, before) << "pause must not allocate a player";
}

TEST_F(SBinkPauseTest, UnknownHandle) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->paused = FALSE;

    g_pauseCalls = 0;
    g_pauseA = g_pauseB = NULL;
    pBinkPause = (void*)&MockPause;

    sBinkPause((void*)0x9999, (void*)1);

    EXPECT_EQ(g_pauseCalls, 1) << "unknown handle must still forward to the real DLL";
    EXPECT_EQ(g_pauseA, (void*)0x9999);
    EXPECT_EQ(g_pauseB, (void*)1);
    EXPECT_FALSE(pl->paused) << "unknown handle must not touch an existing player";
    EXPECT_EQ(g_playerCount, 1) << "unknown handle must not create a player";
}

TEST_F(SBinkPauseTest, PauseResumeCycle) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);

    sBinkPause(handle, (void*)1);
    EXPECT_TRUE(pl->paused);

    sBinkPause(handle, (void*)0);
    EXPECT_FALSE(pl->paused);

    sBinkPause(handle, (void*)1);
    EXPECT_TRUE(pl->paused);

    sBinkPause(handle, (void*)0);
    EXPECT_FALSE(pl->paused);
}

TEST_F(SBinkPauseTest, ResumeDoesNotUnmuteSilencedPlayer) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);

    void* savedSound = pBinkSetSoundOnOff;
    pBinkSetSoundOnOff = (void*)&MockSetSoundOnOff;
    g_setSoundOnOffCalled = 0;

    // The game silenced the movie: the replacement player must stop.
    sBinkSetSoundOnOff(handle, (void*)0);
    EXPECT_TRUE(pl->paused) << "sound off must pause the replacement player";

    // ...and an explicit resume must not undo that silence.
    sBinkPause(handle, (void*)0);
    EXPECT_TRUE(pl->paused)
        << "BinkPause(0) must not resume a player the game silenced";

    pBinkSetSoundOnOff = savedSound;
}

TEST_F(SBinkPauseTest, SoundOnDoesNotResumePausedVideo) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);

    sBinkPause(handle, (void*)1);
    ASSERT_TRUE(pl->paused);

    void* savedSound = pBinkSetSoundOnOff;
    pBinkSetSoundOnOff = (void*)&MockSetSoundOnOff;
    g_setSoundOnOffCalled = 0;

    sBinkSetSoundOnOff(handle, (void*)1);

    EXPECT_TRUE(pl->paused)
        << "SetSoundOnOff(1) must not resume a video the game paused";
    EXPECT_EQ(g_setSoundOnOffCalled, 1);

    pBinkSetSoundOnOff = savedSound;
}

// ============================================================================
// sBinkGoto tests
// ============================================================================

class SBinkGotoTest : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedGoto;
    void* savedGetSummary;
    int savedCount;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedGoto = pBinkGoto;
        savedGetSummary = pBinkGetSummary;
        savedCount = g_vidCount;

        pBinkGoto = NULL;
        pBinkGetSummary = (void*)MockSummary;
        g_vidCount = 0;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
        }
        pBinkGetSummary = savedGetSummary;
        pBinkGoto = savedGoto;
        g_vidCount = savedCount;
    }

    WavPlayer* SetupVideoWithPlayer(void* handle) {
        TrackVideo(handle, "test.bik", NULL);
        VideoInfo* vi = FindVideo(handle);
        if (!vi) return NULL;
        WavPlayer* pl = AllocPlayer();
        if (!pl) return NULL;
        vi->wavPlayer = pl;
        pl->hWave = (HWAVEOUT)0x12345678;
        pl->playing = TRUE;
        pl->paused = FALSE;
        pl->pcmSize = 22050 * 4 * 10;
        pl->pcmPos = 0;
        pl->format.nSamplesPerSec = 22050;
        pl->format.wBitsPerSample = 16;
        pl->format.nChannels = 2;
        pl->format.nBlockAlign = 4;
        return pl;
    }
};

namespace {
int g_gotoCalls = 0;
void* g_gotoA = NULL;
void* g_gotoB = NULL;
void* g_gotoC = NULL;
void __stdcall MockGoto(void* a, void* b, void* c) {
    g_gotoCalls++;
    g_gotoA = a;
    g_gotoB = b;
    g_gotoC = c;
}
}

TEST_F(SBinkGotoTest, SeekToFrame0) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);

    pBinkGoto = NULL;
    sBinkGoto(handle, (void*)0, NULL);
    EXPECT_EQ(pl->pcmPos, (DWORD)0);
}

TEST_F(SBinkGotoTest, SeekToFrame10) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);

    sBinkGoto(handle, (void*)10, NULL);
    // frame=10, fps=30/1, samplesPerSec=22050
    // sampleOffset = 10 * 22050 * 1 / 30 = 7350
    // byteOffset = 7350 * 4 = 29400
    DWORD expectedByte = 7350 * pl->format.nBlockAlign;
    EXPECT_EQ(pl->pcmPos, expectedByte);
}

TEST_F(SBinkGotoTest, SeekWithoutPlayer) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);
    int before = g_playerCount;

    pBinkGoto = NULL;
    sBinkGoto(handle, (void*)5, NULL);

    VideoInfo* vi = FindVideo(handle);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->wavPlayer, (WavPlayer*)NULL) << "seek must not create a player";
    EXPECT_EQ(g_playerCount, before);
}

TEST_F(SBinkGotoTest, UnknownHandle) {
    int before = g_playerCount;
    g_gotoCalls = 0;
    g_gotoA = g_gotoB = g_gotoC = NULL;
    pBinkGoto = (void*)&MockGoto;

    sBinkGoto((void*)0x9999, (void*)5, NULL);

    EXPECT_EQ(g_gotoCalls, 1) << "unknown handle must still forward to the real DLL";
    EXPECT_EQ(g_gotoA, (void*)0x9999);
    EXPECT_EQ(g_gotoB, (void*)5);
    EXPECT_EQ(g_playerCount, before) << "unknown handle must not allocate a player";
}

TEST_F(SBinkGotoTest, NullSummaryFnSkipsSeek) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);
    DWORD savedPos = pl->pcmPos;

    void* savedFn = pBinkGetSummary;
    pBinkGetSummary = NULL;
    sBinkGoto(handle, (void*)10, NULL);
    pBinkGetSummary = savedFn;

    EXPECT_EQ(pl->pcmPos, savedPos);
}

TEST_F(SBinkGotoTest, ZeroFrameRateSkipsSeek) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);
    DWORD savedPos = pl->pcmPos;

    g_mFR = 0;
    sBinkGoto(handle, (void*)10, NULL);
    EXPECT_EQ(pl->pcmPos, savedPos);
}

TEST_F(SBinkGotoTest, SeekClampsToMax) {
    void* handle = (void*)0x1000;
    WavPlayer* pl = SetupVideoWithPlayer(handle);
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->pcmSize = 100;

    sBinkGoto(handle, (void*)1000000, NULL);
    EXPECT_LE(pl->pcmPos, pl->pcmSize);
}

// ============================================================================
// sBinkSetVolume2 tests
// ============================================================================

class SBinkSetVolume2Test : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedVolume;
    int savedCount;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedVolume = pBinkSetVolume;
        savedCount = g_vidCount;

        pBinkGetSummary = (void*)MockSummary;
        pBinkSetVolume = (void*)MockSetVolume;
        g_vidCount = 0;
        g_setVolumeCalled = 0;
        g_setVolumeHandle = NULL;
        g_setVolumeArg = NULL;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
        }
        pBinkGetSummary = savedSummary;
        pBinkSetVolume = savedVolume;
        g_vidCount = savedCount;
    }

    WavPlayer* SetupVideoWithPlayer(void* handle) {
        TrackVideo(handle, "test.bik", NULL);
        VideoInfo* vi = FindVideo(handle);
        if (!vi) return NULL;
        strncpy_s(vi->wavPath, sizeof(vi->wavPath), "test.wav", _TRUNCATE);
        WavPlayer* pl = AllocPlayer();
        if (!pl) return NULL;
        vi->wavPlayer = pl;
        pl->hWave = (HWAVEOUT)0x12345678;
        pl->playing = TRUE;
        return pl;
    }
};

TEST_F(SBinkSetVolume2Test, MutesWithPlayer) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetVolume2(handle, (void*)0x5999);
    EXPECT_EQ(g_setVolumeCalled, 1);
    EXPECT_EQ(g_setVolumeHandle, handle);
    EXPECT_EQ(g_setVolumeArg, (void*)0);
}

TEST_F(SBinkSetVolume2Test, ForwardsWithoutPlayer) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);

    sBinkSetVolume2(handle, (void*)0x5999);
    EXPECT_EQ(g_setVolumeCalled, 1);
    EXPECT_EQ(g_setVolumeHandle, handle);
    EXPECT_EQ(g_setVolumeArg, (void*)0x5999);
}

TEST_F(SBinkSetVolume2Test, UnknownHandleForwards) {
    sBinkSetVolume2((void*)0x9999, (void*)0x5999);
    EXPECT_EQ(g_setVolumeCalled, 1);
    EXPECT_EQ(g_setVolumeArg, (void*)0x5999);
}

TEST_F(SBinkSetVolume2Test, MutesAlwaysZero) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetVolume2(handle, (void*)0xFFFF);
    EXPECT_EQ(g_setVolumeArg, (void*)0);
}

TEST_F(SBinkSetVolume2Test, LongApiForwardsVolumeValue) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);

    // Long API: (bnk, trackid=100, volume=0x5999). The track id only exists
    // in the long real DLL, so the short real DLL must see the volume.
    sBinkSetVolume3(handle, (void*)100, (void*)0x5999);
    EXPECT_EQ(g_setVolumeCalled, 1);
    EXPECT_EQ(g_setVolumeHandle, handle);
    EXPECT_EQ(g_setVolumeArg, (void*)0x5999);
}

TEST_F(SBinkSetVolume2Test, LongApiMutesWithPlayer) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetVolume3(handle, (void*)100, (void*)0x5999);
    EXPECT_EQ(g_setVolumeCalled, 1);
    EXPECT_EQ(g_setVolumeArg, (void*)0);
}

// ============================================================================
// sBinkSetSoundOnOff tests
// ============================================================================

class SBinkSetSoundOnOffTest : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedSoundOnOff;
    int savedCount;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedSoundOnOff = pBinkSetSoundOnOff;
        savedCount = g_vidCount;

        pBinkGetSummary = (void*)MockSummary;
        pBinkSetSoundOnOff = (void*)MockSetSoundOnOff;
        g_vidCount = 0;
        g_setSoundOnOffCalled = 0;
        g_setSoundOnOffHandle = NULL;
        g_setSoundOnOffArg = NULL;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
        }
        pBinkGetSummary = savedSummary;
        pBinkSetSoundOnOff = savedSoundOnOff;
        g_vidCount = savedCount;
    }

    WavPlayer* SetupVideoWithPlayer(void* handle) {
        TrackVideo(handle, "test.bik", NULL);
        VideoInfo* vi = FindVideo(handle);
        if (!vi) return NULL;
        strncpy_s(vi->wavPath, sizeof(vi->wavPath), "test.wav", _TRUNCATE);
        WavPlayer* pl = AllocPlayer();
        if (!pl) return NULL;
        vi->wavPlayer = pl;
        pl->hWave = (HWAVEOUT)0x12345678;
        pl->playing = TRUE;
        return pl;
    }
};

TEST_F(SBinkSetSoundOnOffTest, MutesOnWithPlayer) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetSoundOnOff(handle, (void*)1);
    EXPECT_EQ(g_setSoundOnOffCalled, 1);
    EXPECT_EQ(g_setSoundOnOffHandle, handle);
    EXPECT_EQ(g_setSoundOnOffArg, (void*)0);
}

TEST_F(SBinkSetSoundOnOffTest, ForwardsOffWithPlayer) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetSoundOnOff(handle, (void*)0);
    EXPECT_EQ(g_setSoundOnOffCalled, 1);
    EXPECT_EQ(g_setSoundOnOffHandle, handle);
    EXPECT_EQ(g_setSoundOnOffArg, (void*)0);
}

TEST_F(SBinkSetSoundOnOffTest, ForwardsWithoutPlayer) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);

    sBinkSetSoundOnOff(handle, (void*)1);
    EXPECT_EQ(g_setSoundOnOffCalled, 1);
    EXPECT_EQ(g_setSoundOnOffArg, (void*)1);
}

TEST_F(SBinkSetSoundOnOffTest, UnknownHandleForwards) {
    sBinkSetSoundOnOff((void*)0x9999, (void*)1);
    EXPECT_EQ(g_setSoundOnOffCalled, 1);
    EXPECT_EQ(g_setSoundOnOffArg, (void*)1);
}

TEST_F(SBinkSetSoundOnOffTest, MutesAlwaysZero) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetSoundOnOff(handle, (void*)1);
    EXPECT_EQ(g_setSoundOnOffArg, (void*)0);
}

// ============================================================================
// sBinkSetPan tests
// ============================================================================

int g_setPanCalled = 0;
void* g_setPanHandle = NULL;
void* g_setPanArg1 = NULL;
void* g_setPanArg2 = NULL;

#ifdef BINK_HAS_PAN_12
void __stdcall MockSetPan(void* a, void* b, void* c) {
    g_setPanCalled++;
    g_setPanHandle = a;
    g_setPanArg1 = b;
    g_setPanArg2 = c;
}
#else
void __stdcall MockSetPan(void* a, void* b) {
    g_setPanCalled++;
    g_setPanHandle = a;
    g_setPanArg1 = b;
    g_setPanArg2 = NULL;
}
#endif

class SBinkSetPanTest : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedPan;
    int savedCount;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedPan = pBinkSetPan;
        savedCount = g_vidCount;

        pBinkGetSummary = (void*)MockSummary;
        pBinkSetPan = (void*)MockSetPan;
        g_vidCount = 0;
        g_setPanCalled = 0;
        g_setPanHandle = NULL;
        g_setPanArg1 = NULL;
        g_setPanArg2 = NULL;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
        }
        pBinkGetSummary = savedSummary;
        pBinkSetPan = savedPan;
        g_vidCount = savedCount;
    }

    WavPlayer* SetupVideoWithPlayer(void* handle) {
        TrackVideo(handle, "test.bik", NULL);
        VideoInfo* vi = FindVideo(handle);
        if (!vi) return NULL;
        strncpy_s(vi->wavPath, sizeof(vi->wavPath), "test.wav", _TRUNCATE);
        WavPlayer* pl = AllocPlayer();
        if (!pl) return NULL;
        vi->wavPlayer = pl;
        pl->hWave = (HWAVEOUT)0x12345678;
        pl->playing = TRUE;
        return pl;
    }
};

TEST_F(SBinkSetPanTest, MutesWithReplacement) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetPan(handle, (void*)100, (void*)200);
    EXPECT_EQ(g_setPanCalled, 0);
}

TEST_F(SBinkSetPanTest, ForwardsWithoutReplacement) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);

    // Long API: (bnk, trackid=100, pan=200). The track id only exists in the
    // long real DLL, so the short real DLL must see the pan value.
    sBinkSetPan(handle, (void*)100, (void*)200);
    EXPECT_EQ(g_setPanCalled, 1);
    EXPECT_EQ(g_setPanHandle, handle);
    EXPECT_EQ(g_setPanArg1, (void*)200);
}

TEST_F(SBinkSetPanTest, UnknownHandleForwards) {
    sBinkSetPan((void*)0x9999, (void*)100, (void*)200);
    EXPECT_EQ(g_setPanCalled, 1);
    EXPECT_EQ(g_setPanArg1, (void*)200);
}

TEST_F(SBinkSetPanTest, ForwardsNullArgs) {
    sBinkSetPan(NULL, NULL, NULL);
    EXPECT_EQ(g_setPanCalled, 1);
}

TEST_F(SBinkSetPanTest, ShortApiForwardsPanValue) {
    void* handle = (void*)0x1000;
    TrackVideo(handle, "test.bik", NULL);

    // Short API: (bnk, pan=200).
    sBinkSetPan2(handle, (void*)200);
    EXPECT_EQ(g_setPanCalled, 1);
    EXPECT_EQ(g_setPanHandle, handle);
    EXPECT_EQ(g_setPanArg1, (void*)200);
}

TEST_F(SBinkSetPanTest, ShortApiMutesWithReplacement) {
    void* handle = (void*)0x1000;
    SetupVideoWithPlayer(handle);

    sBinkSetPan2(handle, (void*)200);
    EXPECT_EQ(g_setPanCalled, 0);
}

// ============================================================================
// sBinkSetMixBins tests
//
// The test build is group 5, whose real DLL takes the long
// (bnk, trackid, mix_bins, total) form.
// ============================================================================

int g_setMixBinsCalled = 0;
void* g_mixBinsHandle = NULL;
void* g_mixBinsTrack = NULL;
void* g_mixBinsBins = NULL;
void* g_mixBinsTotal = NULL;

void __stdcall MockSetMixBins(void* a, void* b, void* c, void* d) {
    g_setMixBinsCalled++;
    g_mixBinsHandle = a;
    g_mixBinsTrack = b;
    g_mixBinsBins = c;
    g_mixBinsTotal = d;
}

class SBinkSetMixBinsTest : public ::testing::Test {
protected:
    void* savedMixBins;
    int savedCount;

    void SetUp() override {
        savedMixBins = pBinkSetMixBins;
        savedCount = g_vidCount;
        pBinkSetMixBins = (void*)MockSetMixBins;
        g_vidCount = 0;
        g_setMixBinsCalled = 0;
        g_mixBinsHandle = NULL;
        g_mixBinsTrack = NULL;
        g_mixBinsBins = NULL;
        g_mixBinsTotal = NULL;
        g_mW = 640;
        g_mH = 480;
        g_mFR = 30;
        g_mFRD = 1;
    }

    void TearDown() override {
        pBinkSetMixBins = savedMixBins;
        g_vidCount = savedCount;
    }
};

TEST_F(SBinkSetMixBinsTest, LongApiForwardsAllArgs) {
    void* handle = (void*)0x1000;
    sBinkSetMixBins(handle, (void*)100, (void*)0xAAAA, (void*)0xBBBB);
    EXPECT_EQ(g_setMixBinsCalled, 1);
    EXPECT_EQ(g_mixBinsHandle, handle);
    EXPECT_EQ(g_mixBinsTrack, (void*)100);
    EXPECT_EQ(g_mixBinsBins, (void*)0xAAAA);
    EXPECT_EQ(g_mixBinsTotal, (void*)0xBBBB);
}

TEST_F(SBinkSetMixBinsTest, ShortApiOmitsTrackAndCount) {
    void* handle = (void*)0x1000;
    sBinkSetMixBins2(handle, (void*)0xAAAA);
    EXPECT_EQ(g_setMixBinsCalled, 1);
    EXPECT_EQ(g_mixBinsHandle, handle);
    EXPECT_EQ(g_mixBinsTrack, (void*)0);   // the short API has no track id
    EXPECT_EQ(g_mixBinsBins, (void*)0xAAAA);
    EXPECT_EQ(g_mixBinsTotal, (void*)0);   // ...and no entry count
}

// ============================================================================
// sBinkSetWillLoop tests
// ============================================================================

int g_setWillLoopCalled = 0;
void* g_setWillLoopHandle = NULL;
void* g_setWillLoopArg = NULL;

void __stdcall MockSetWillLoop(void* a, void* b) {
    g_setWillLoopCalled++;
    g_setWillLoopHandle = a;
    g_setWillLoopArg = b;
}

class SBinkSetWillLoopTest : public ::testing::Test {
protected:
    void* savedWillLoop;

    void SetUp() override {
        savedWillLoop = pBinkSetWillLoop;
        pBinkSetWillLoop = (void*)MockSetWillLoop;
        g_setWillLoopCalled = 0;
        g_setWillLoopHandle = NULL;
        g_setWillLoopArg = NULL;
    }

    void TearDown() override {
        pBinkSetWillLoop = savedWillLoop;
    }
};

TEST_F(SBinkSetWillLoopTest, ForwardsToReal) {
    sBinkSetWillLoop((void*)0x1000, (void*)1);
    EXPECT_EQ(g_setWillLoopCalled, 1);
    EXPECT_EQ(g_setWillLoopHandle, (void*)0x1000);
    EXPECT_EQ(g_setWillLoopArg, (void*)1);
}

TEST_F(SBinkSetWillLoopTest, ForwardsZeroArg) {
    sBinkSetWillLoop((void*)0x2000, (void*)0);
    EXPECT_EQ(g_setWillLoopCalled, 1);
    EXPECT_EQ(g_setWillLoopArg, (void*)0);
}

TEST_F(SBinkSetWillLoopTest, NullPtrFunction) {
    pBinkSetWillLoop = NULL;
    sBinkSetWillLoop((void*)0x1000, (void*)1);
    EXPECT_EQ(g_setWillLoopCalled, 0);
}

// ============================================================================
// sBinkWait tests
// ============================================================================

int g_waitCalled = 0;
void* g_waitHandle = NULL;
intptr_t g_waitReturnVal = 0;

intptr_t __stdcall MockWait(void* a) {
    g_waitCalled++;
    g_waitHandle = a;
    return g_waitReturnVal;
}

class SBinkWaitTest : public ::testing::Test {
protected:
    void* savedWait;

    void SetUp() override {
        savedWait = pBinkWait;
        pBinkWait = (void*)MockWait;
        g_waitCalled = 0;
        g_waitHandle = NULL;
        g_waitReturnVal = 0;
    }

    void TearDown() override {
        pBinkWait = savedWait;
    }
};

TEST_F(SBinkWaitTest, ForwardsToReal) {
    g_waitReturnVal = 0;
    intptr_t r = sBinkWait((void*)0x1000);
    EXPECT_EQ(g_waitCalled, 1);
    EXPECT_EQ(g_waitHandle, (void*)0x1000);
    EXPECT_EQ(r, (intptr_t)0);
}

TEST_F(SBinkWaitTest, ReturnsNonZero) {
    g_waitReturnVal = 1;
    intptr_t r = sBinkWait((void*)0x2000);
    EXPECT_EQ(r, (intptr_t)1);
}

TEST_F(SBinkWaitTest, NullPtrFunction) {
    pBinkWait = NULL;
    intptr_t r = sBinkWait((void*)0x1000);
    EXPECT_EQ(g_waitCalled, 0);
    EXPECT_EQ(r, (intptr_t)0);
}

TEST_F(SBinkWaitTest, MultipleCalls) {
    g_waitReturnVal = 0;
    sBinkWait((void*)0x1000);
    sBinkWait((void*)0x1000);
    sBinkWait((void*)0x1000);
    EXPECT_EQ(g_waitCalled, 3);
}

// ============================================================================
// sBinkDoFrame — audio trigger tests
// ============================================================================

int g_doFrameCalled = 0;

void __stdcall MockDoFrame(void* a) {
    g_doFrameCalled++;
}

class SBinkDoFrameTest : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedDoFrame;
    void* savedOpen;
    int savedCount;
    int savedPlayerCount;
    WavPlayer savedPlayers[MAX_WAV_PLAYERS];

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedDoFrame = pBinkDoFrame;
        savedOpen = pBinkOpen;
        savedCount = g_vidCount;
        savedPlayerCount = g_playerCount;
        memcpy(savedPlayers, g_players, sizeof(g_players));

        pBinkGetSummary = (void*)MockSummary;
        pBinkDoFrame = (void*)MockDoFrame;
        g_vidCount = 0;
        g_playerCount = 0;
        g_doFrameCalled = 0;
        g_mW = 640;
        g_mH = 480;
    }

    void TearDown() override {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                FreePlayer(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
        }
        pBinkGetSummary = savedSummary;
        pBinkDoFrame = savedDoFrame;
        pBinkOpen = savedOpen;
        g_vidCount = savedCount;
        g_playerCount = savedPlayerCount;
        memcpy(g_players, savedPlayers, sizeof(g_players));
    }
};

TEST_F(SBinkDoFrameTest, ForwardsToRealDoFrame) {
    TrackVideo((void*)0x1000, "test.bik", NULL);
    g_doFrameCalled = 0;

    sBinkDoFrame((void*)0x1000);

    EXPECT_EQ(g_doFrameCalled, 1);
}

TEST_F(SBinkDoFrameTest, NoAudioWithoutWavPath) {
    TrackVideo((void*)0x1000, "test.bik", NULL);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_STREQ(vi->wavPath, "");

    sBinkDoFrame((void*)0x1000);

    EXPECT_EQ(vi->wavPlayer, (WavPlayer*)NULL);
    EXPECT_FALSE(vi->wavStarted);
}

TEST_F(SBinkDoFrameTest, SkipsAudioWhenAlreadyStarted) {
    TrackVideo((void*)0x1000, "test.bik", NULL);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    strncpy_s(vi->wavPath, sizeof(vi->wavPath), "test.wav", _TRUNCATE);
    vi->wavStarted = true;

    int playerCountBefore = g_playerCount;
    sBinkDoFrame((void*)0x1000);

    EXPECT_EQ(g_playerCount, playerCountBefore) << "No new player should be allocated";
}

TEST_F(SBinkDoFrameTest, UnknownHandleStillForwards) {
    g_doFrameCalled = 0;

    sBinkDoFrame((void*)0x9999);

    EXPECT_EQ(g_doFrameCalled, 1);
}

TEST_F(SBinkDoFrameTest, NullPtrFunctionDoesNotCrash) {
    g_doFrameCalled = 0;
    pBinkDoFrame = NULL;
    TrackVideo((void*)0x1000, "test.bik", NULL);

    sBinkDoFrame((void*)0x1000);

    EXPECT_EQ(g_doFrameCalled, 0) << "NULL forward pointer must not be called";
    EXPECT_NE(FindVideo((void*)0x1000), (VideoInfo*)NULL)
        << "tracking must survive a frame with no forward target";
}

// The device refused a buffer: WaveOutProc cleared `playing` while PCM is left
// (pcmPos < pcmSize). The proxy must abandon the replacement instead of
// leaving the movie silent for the rest of its runtime.
TEST_F(SBinkDoFrameTest, DeadPlayerFallsBackToOriginalAudio) {
    TrackVideo((void*)0x1000, "test.bik", NULL);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    strncpy_s(vi->wavPath, sizeof(vi->wavPath), "test.wav", _TRUNCATE);
    vi->wavStarted = true;   // skip the start attempt, we attach the player below
    vi->soundReqSet = TRUE;
    vi->soundOn = TRUE;

    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    vi->wavPlayer = pl;
    pl->hWave = (HWAVEOUT)0x12345678;
    pl->playing = FALSE;     // stream died
    pl->paused = FALSE;
    pl->pcmSize = 4096;
    pl->pcmPos = 1024;       // ...with data still unplayed

    void* savedSound = pBinkSetSoundOnOff;
    pBinkSetSoundOnOff = (void*)&MockSetSoundOnOff;
    g_setSoundOnOffCalled = 0;

    sBinkDoFrame((void*)0x1000);

    vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->wavPlayer, (WavPlayer*)NULL) << "dead player must be abandoned";
    EXPECT_TRUE(vi->wavFailed) << "fallback must be permanent for this video";
    EXPECT_EQ(g_setSoundOnOffCalled, 1) << "original sound state must be pushed back";
    EXPECT_EQ(g_setSoundOnOffArg, (void*)1);

    pBinkSetSoundOnOff = savedSound;
}

// A clip that simply finished has pcmPos == pcmSize: that is not a failure.
TEST_F(SBinkDoFrameTest, FinishedClipKeepsPlayer) {
    TrackVideo((void*)0x1000, "test.bik", NULL);
    VideoInfo* vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    strncpy_s(vi->wavPath, sizeof(vi->wavPath), "test.wav", _TRUNCATE);
    vi->wavStarted = true;

    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    vi->wavPlayer = pl;
    pl->hWave = (HWAVEOUT)0x12345678;
    pl->playing = FALSE;
    pl->paused = FALSE;
    pl->pcmSize = 4096;
    pl->pcmPos = 4096;       // fully played

    sBinkDoFrame((void*)0x1000);

    vi = FindVideo((void*)0x1000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->wavPlayer, pl) << "end of clip must not be treated as a failure";
    EXPECT_FALSE(vi->wavFailed);
}

// ============================================================================
// sBinkOpenWithOptions tests
// ============================================================================

int g_openWithOptionsCalled = 0;
void* g_openWithOptionsRet = NULL;

intptr_t __stdcall MockOpenWithOptions(void* a, void* b, void* c) {
    g_openWithOptionsCalled++;
    return (intptr_t)g_openWithOptionsRet;
}

class SBinkOpenWithOptionsTest : public ::testing::Test {
protected:
    void* savedSummary;
    void* savedOpenWithOptions;
    int savedCount;
    LONG savedInitState;

    void SetUp() override {
        savedSummary = pBinkGetSummary;
        savedOpenWithOptions = pBinkOpenWithOptions;
        savedCount = g_vidCount;
        savedInitState = g_initState;

        pBinkGetSummary = (void*)MockSummary;
        pBinkOpenWithOptions = (void*)MockOpenWithOptions;
        g_vidCount = 0;
        g_openWithOptionsCalled = 0;
        g_openWithOptionsRet = (void*)0x1000;
        g_mW = 640;
        g_mH = 480;
        g_initState = 2; // Bypass EnsureInitialized
    }

    void TearDown() override {
        pBinkGetSummary = savedSummary;
        pBinkOpenWithOptions = savedOpenWithOptions;
        g_vidCount = savedCount;
        g_initState = savedInitState;
    }
};

TEST_F(SBinkOpenWithOptionsTest, TracksOnSuccess) {
    g_openWithOptionsRet = (void*)0x2000;

    intptr_t result = sBinkOpenWithOptions((void*)"test.bik", (void*)0, (void*)0);

    EXPECT_EQ(result, (intptr_t)0x2000);
    EXPECT_EQ(g_vidCount, 1);
    EXPECT_EQ(g_vids[0].handle, (void*)0x2000);
}

TEST_F(SBinkOpenWithOptionsTest, NoTrackOnNull) {
    g_openWithOptionsRet = NULL;

    intptr_t result = sBinkOpenWithOptions((void*)"test.bik", (void*)0, (void*)0);

    EXPECT_EQ(result, (intptr_t)0);
    EXPECT_EQ(g_vidCount, 0);
}

TEST_F(SBinkOpenWithOptionsTest, ForwardsAllArgs) {
    g_openWithOptionsRet = (void*)0x3000;

    sBinkOpenWithOptions((void*)"test.bik", (void*)0x100, (void*)0x200);

    EXPECT_EQ(g_openWithOptionsCalled, 1);
}

TEST_F(SBinkOpenWithOptionsTest, NullFirstArgNoTrack) {
    // sBinkOpenWithOptions only tracks when both r (return) and a (first arg) are non-NULL
    g_openWithOptionsRet = (void*)0x4000;

    sBinkOpenWithOptions(NULL, (void*)0, (void*)0);

    // a is NULL → TrackVideo not called even though handle is valid
    EXPECT_EQ(g_vidCount, 0);
}

TEST_F(SBinkOpenWithOptionsTest, NullPtrFunctionReturnsZero) {
    pBinkOpenWithOptions = NULL;

    intptr_t result = sBinkOpenWithOptions((void*)"test.bik", (void*)0, (void*)0);

    EXPECT_EQ(result, (intptr_t)0);
    EXPECT_EQ(g_openWithOptionsCalled, 0);
}
