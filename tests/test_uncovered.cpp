#include <gtest/gtest.h>
#include "test_helpers.h"
#include <cstdio>

// ============================================================================
// test_uncovered.cpp — Tests for TrackVideo, LogCallStack, EnsureInitialized,
//                      sBinkCopyToBuffer scaling, sBinkClose
// ============================================================================

extern void EnsureInitialized();
extern void LogCallStack(int skip);

// ============================================================================
// Mock BinkGetSummary
// ============================================================================

int g_mW = 0, g_mH = 0, g_mFR = 0, g_mFRD = 0;

void __stdcall MockSummary(void* handle, void* summary) {
    uint8_t* s = (uint8_t*)summary;
    memset(s, 0, 512);
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
    EXPECT_EQ(g_vids[0].tempBuf, (void*)NULL);
    EXPECT_EQ(g_vids[0].scaleLookupX, (int*)NULL);
    EXPECT_EQ(g_vids[0].wavPlayer, (WavPlayer*)NULL);
    EXPECT_STREQ(g_vids[0].wavPath, "");
}

TEST_F(TrackVideoTest, StoresFrameRate) {
    g_mFR = 60;
    g_mFRD = 2;
    TrackVideo((void*)0x1000, "test.bik", NULL);
    EXPECT_EQ(g_vidCount, 1);
}

// ============================================================================
// LogCallStack tests
// ============================================================================

TEST(LogCallStackTest, DoesNotCrash) {
    LogCallStack(0);
    LogCallStack(1);
    LogCallStack(2);
    SUCCEED();
}

TEST(LogCallStackTest, LargeSkip) {
    LogCallStack(100);
    SUCCEED();
}

// ============================================================================
// EnsureInitialized tests
// ============================================================================

TEST(EnsureInitializedTest, Idempotent) {
    EnsureInitialized();
    EnsureInitialized();
    EnsureInitialized();
    SUCCEED();
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
            if (g_vids[i].tempBuf) { VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE); g_vids[i].tempBuf = NULL; }
            if (g_vids[i].scaleLookupX) { free(g_vids[i].scaleLookupX); g_vids[i].scaleLookupX = NULL; }
            if (g_vids[i].scaleLookupY) { free(g_vids[i].scaleLookupY); g_vids[i].scaleLookupY = NULL; }
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
    EXPECT_NE(vi->tempBuf, (void*)NULL);
    EXPECT_NE(vi->scaleLookupX, (int*)NULL);
    EXPECT_NE(vi->scaleLookupY, (int*)NULL);
    EXPECT_GT(vi->scaleTableW, 0);
    EXPECT_GT(vi->scaleTableH, 0);
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
    EXPECT_EQ(vi->scaleTableW, 400);
    EXPECT_EQ(vi->scaleTableH, 300);

    for (int x = 0; x < 400; x++) {
        EXPECT_GE(vi->scaleLookupX[x], 0);
        EXPECT_LT(vi->scaleLookupX[x], 800);
    }
    for (int y = 0; y < 300; y++) {
        EXPECT_GE(vi->scaleLookupY[y], 0);
        EXPECT_LT(vi->scaleLookupY[y], 600);
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
    EXPECT_NE(vi->tempBuf, (void*)NULL);
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
    EXPECT_EQ(vi->tempBuf, (void*)NULL);
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
    void* firstBuf = vi->tempBuf;

    sBinkCopyToBuffer(
        (void*)0x1000, dstBuf, (void*)(intptr_t)dstPitch,
        (void*)480, (void*)0, (void*)0, (void*)2);

    EXPECT_EQ(vi->tempBuf, firstBuf);
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
    EXPECT_EQ(vi->tempBuf, (void*)NULL);
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
// ============================================================================

TEST(SoundTrackAdapterTest, BinkSetSoundTrack8DoesNotCrash) {
    sBinkSetSoundTrack8((void*)0x1000, (void*)0);
    SUCCEED();
}

TEST(SoundTrackAdapterTest, BinkSetSoundTrack4DoesNotCrash) {
    sBinkSetSoundTrack4((void*)0x1000);
    SUCCEED();
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

    sBinkPause(handle, (void*)1);
    SUCCEED();
}

TEST_F(SBinkPauseTest, UnknownHandle) {
    sBinkPause((void*)0x9999, (void*)1);
    SUCCEED();
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

    pBinkGoto = NULL;
    sBinkGoto(handle, (void*)5, NULL);
    SUCCEED();
}

TEST_F(SBinkGotoTest, UnknownHandle) {
    pBinkGoto = NULL;
    sBinkGoto((void*)0x9999, (void*)5, NULL);
    SUCCEED();
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
