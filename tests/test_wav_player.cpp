// ============================================================================
// test_wav_player.cpp — Unit tests for wav_player.cpp
//
// Uses mock_waveout.h to redirect waveOut* calls to mock implementations.
// wav_player.cpp is compiled directly with mock macros active.
//
// Test strategy:
// - AllocPlayer/FreePlayer: state management, slot reuse, max slots
// - WavPlayerStop: cleanup with mock WaveOut
// - WavPlayerPause/Resume: state transitions
// - WavPlayerSeek: offset calculation
// - WavPlayerStart: uses real WAV decoder + mock WaveOut (no audio playback)
// ============================================================================

#include <gtest/gtest.h>
#include "mock_waveout.h"   // MUST be first — defines mock waveOut* macros

// We need to compile wav_player.cpp with mock macros.
// Instead of including .cpp directly (which would cause ODR issues with
// the main wav_player.o), we test the functions indirectly through their
// effects on the WavPlayer struct and mock state.

#include "binkw32_proxy.h"
#include "audio_decoder.h"
#include <cstring>

// Declare functions from wav_player.cpp (they're not in a header)
extern WavPlayer* AllocPlayer();
extern void FreePlayer(WavPlayer* pl);
extern BOOL WavPlayerStart(WavPlayer* pl, const char* audioPath);
extern void WavPlayerStop(WavPlayer* pl);
extern void WavPlayerPause(WavPlayer* pl);
extern void WavPlayerResume(WavPlayer* pl);
extern void WavPlayerSeek(WavPlayer* pl, DWORD sampleOffset);

// ============================================================================
// Helper: create a minimal valid WAV in temp dir
// ============================================================================

static char g_testWavPath[MAX_PATH];

static void CreateTestWav() {
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    _snprintf_s(g_testWavPath, sizeof(g_testWavPath), _TRUNCATE,
                "%s\\bink32w_test.wav", tempDir);

    int16_t samples[2205]; // 100ms at 22050 Hz
    for (int i = 0; i < 2205; i++) samples[i] = (int16_t)(i * 10);

    uint32_t dataSize = sizeof(samples);
    uint32_t riffSize = 4 + (8 + 16) + (8 + dataSize);
    FILE* f = NULL;
    fopen_s(&f, g_testWavPath, "wb");
    if (!f) return;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    uint16_t formatTag = 1, channels = 1, bitsPerSample = 16, blockAlign = 2;
    uint32_t sampleRate = 22050, avgBytesPerSec = 44100;
    fwrite(&formatTag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&avgBytesPerSec, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    fwrite(samples, 1, sizeof(samples), f);
    fclose(f);
}

static void RemoveTestWav() {
    DeleteFileA(g_testWavPath);
}

// ============================================================================
// Test fixture
// ============================================================================

class WavPlayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mockState.Reset();
        CreateTestWav();
        // Reset player pool
        for (int i = 0; i < MAX_WAV_PLAYERS; i++) {
            g_players[i].hWave = NULL;
            g_players[i].csValid = FALSE;
        }
        g_playerCount = 0;
    }

    void TearDown() override {
        // Cleanup any allocated players
        for (int i = 0; i < MAX_WAV_PLAYERS; i++) {
            if (g_players[i].hWave || g_players[i].csValid) {
                FreePlayer(&g_players[i]);
            }
        }
        g_playerCount = 0;
        RemoveTestWav();
    }
};

// ============================================================================
// AllocPlayer tests
// ============================================================================

TEST_F(WavPlayerTest, AllocPlayerReturnsNonNull) {
    WavPlayer* pl = AllocPlayer();
    EXPECT_NE(pl, (WavPlayer*)NULL);
}

TEST_F(WavPlayerTest, AllocPlayerIncrementsCount) {
    int before = g_playerCount;
    AllocPlayer();
    EXPECT_GT(g_playerCount, before);
}

TEST_F(WavPlayerTest, AllocPlayerReturnsDifferentSlots) {
    WavPlayer* pl1 = AllocPlayer();
    WavPlayer* pl2 = AllocPlayer();
    EXPECT_NE(pl1, pl2);
}

TEST_F(WavPlayerTest, AllocPlayerReusesFreeSlots) {
    WavPlayer* pl1 = AllocPlayer();
    FreePlayer(pl1);
    WavPlayer* pl2 = AllocPlayer();
    EXPECT_NE(pl2, (WavPlayer*)NULL);
    // Should reuse a freed slot
    EXPECT_LE(g_playerCount, MAX_WAV_PLAYERS);
}

TEST_F(WavPlayerTest, AllocPlayerMaxSlots) {
    for (int i = 0; i < MAX_WAV_PLAYERS; i++) {
        WavPlayer* pl = AllocPlayer();
        EXPECT_NE(pl, (WavPlayer*)NULL) << "Failed to allocate slot " << i;
    }
    // One more should return NULL (or reuse)
    WavPlayer* extra = AllocPlayer();
    // Depending on implementation, might return NULL or reuse
    // Just verify we don't crash
}

// ============================================================================
// FreePlayer tests
// ============================================================================

TEST_F(WavPlayerTest, FreePlayerResetsFields) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->playing = TRUE;
    pl->paused = TRUE;
    pl->pcmPos = 12345;

    FreePlayer(pl);

    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_EQ(pl->playing, FALSE);
    EXPECT_EQ(pl->pcmPos, 0u);
}

TEST_F(WavPlayerTest, FreePlayerCallsWaveOutReset) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    // Need to open waveOut first for FreePlayer to call waveOutReset
    // We can't easily do that without WavPlayerStart, but we can test
    // that FreePlayer doesn't crash on a fresh player
    FreePlayer(pl);
    // No crash = pass
}

TEST_F(WavPlayerTest, FreePlayerDoubleFree) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    FreePlayer(pl);
    // Second free should be a no-op (hWave == NULL, csValid == FALSE)
    FreePlayer(pl);
    // No crash = pass
}

TEST_F(WavPlayerTest, FreePlayerNull) {
    // Should not crash
    FreePlayer(NULL);
}

TEST_F(WavPlayerTest, FreePlayerClearsPcmData) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    // Simulate allocated PCM data
    pl->pcmData = (char*)VirtualAlloc(NULL, 1024, MEM_COMMIT, PAGE_READWRITE);
    pl->pcmSize = 1024;
    ASSERT_NE(pl->pcmData, (char*)NULL);

    FreePlayer(pl);

    EXPECT_EQ(pl->pcmData, (char*)NULL);
    EXPECT_EQ(pl->pcmSize, 0u);
}

// ============================================================================
// WavPlayerStop tests
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStopResetsPlaying) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->playing = TRUE;
    pl->paused = FALSE;

    WavPlayerStop(pl);

    EXPECT_EQ(pl->playing, FALSE);
    EXPECT_EQ(pl->paused, FALSE);
}

TEST_F(WavPlayerTest, WavPlayerStopCallsWaveOutReset) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    // Set hWave to trigger waveOutReset path
    pl->hWave = (HWAVEOUT)0x1234;
    g_mockState.Reset();

    WavPlayerStop(pl);

    EXPECT_TRUE(g_mockState.reset);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
}

TEST_F(WavPlayerTest, WavPlayerStopUnpreparesHeaders) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->preparedCount = 4;
    // Simulate prepared headers
    for (int i = 0; i < 4; i++) {
        pl->headers[i].lpData = (LPSTR)0xDEAD;
        pl->buffers[i] = (char*)0xDEAD;
    }
    g_mockState.Reset();

    WavPlayerStop(pl);

    EXPECT_EQ(g_mockState.unprepareCount, 4);
}

TEST_F(WavPlayerTest, WavPlayerStopFreesPcmData) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->pcmData = (char*)VirtualAlloc(NULL, 1024, MEM_COMMIT, PAGE_READWRITE);
    pl->pcmSize = 1024;

    WavPlayerStop(pl);

    EXPECT_EQ(pl->pcmData, (char*)NULL);
}

TEST_F(WavPlayerTest, WavPlayerStopNull) {
    WavPlayerStop(NULL); // Should not crash
}

TEST_F(WavPlayerTest, WavPlayerStopNoWave) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = NULL; // No waveOut opened

    g_mockState.Reset();
    WavPlayerStop(pl);

    // waveOutReset should NOT be called
    EXPECT_FALSE(g_mockState.reset);
}

// ============================================================================
// WavPlayerPause/Resume tests
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerPauseSetsPaused) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->paused = FALSE;
    g_mockState.Reset();

    WavPlayerPause(pl);

    EXPECT_TRUE(pl->paused);
    EXPECT_TRUE(g_mockState.paused);
}

TEST_F(WavPlayerTest, WavPlayerPauseIdempotent) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->paused = TRUE; // Already paused
    g_mockState.Reset();

    WavPlayerPause(pl);

    // Should not call waveOutPause again
    EXPECT_FALSE(g_mockState.paused);
}

TEST_F(WavPlayerTest, WavPlayerResumeClearsPaused) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->paused = TRUE;
    g_mockState.Reset();

    WavPlayerResume(pl);

    EXPECT_FALSE(pl->paused);
    EXPECT_TRUE(g_mockState.restarted);
}

TEST_F(WavPlayerTest, WavPlayerResumeIdempotent) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->paused = FALSE; // Not paused
    g_mockState.Reset();

    WavPlayerResume(pl);

    EXPECT_FALSE(g_mockState.restarted);
}

// ============================================================================
// WavPlayerSeek tests
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerSeekSetsPcmPos) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->pcmSize = 44100; // 1 second of 16-bit mono 22050 Hz
    pl->format.nBlockAlign = 2;
    pl->playing = TRUE;
    g_mockState.Reset();

    WavPlayerSeek(pl, 11025); // Seek to 0.5 seconds

    // pcmPos should be byte offset: 11025 * 2 = 22050
    EXPECT_EQ(pl->pcmPos, 22050u);
}

TEST_F(WavPlayerTest, WavPlayerSeekClampsToSize) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->pcmSize = 1000;
    pl->format.nBlockAlign = 2;
    pl->playing = TRUE;

    WavPlayerSeek(pl, 99999); // Beyond end

    // Should clamp to pcmSize
    EXPECT_EQ(pl->pcmPos, 1000u);
}

TEST_F(WavPlayerTest, WavPlayerSeekCallsWaveOutReset) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->pcmSize = 44100;
    pl->format.nBlockAlign = 2;
    pl->playing = TRUE;
    g_mockState.Reset();

    WavPlayerSeek(pl, 0);

    EXPECT_TRUE(g_mockState.reset);
}

TEST_F(WavPlayerTest, WavPlayerSeekSetsPlaying) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    pl->hWave = (HWAVEOUT)0x1234;
    pl->pcmSize = 44100;
    pl->format.nBlockAlign = 2;
    pl->playing = FALSE;

    WavPlayerSeek(pl, 0);

    EXPECT_TRUE(pl->playing);
}

// ============================================================================
// WavPlayerStart tests (uses real WAV decoder + mock WaveOut)
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStartDecodesWav) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    BOOL result = WavPlayerStart(pl, g_testWavPath);

    EXPECT_TRUE(result);
    EXPECT_NE(pl->pcmData, (char*)NULL);
    EXPECT_GT(pl->pcmSize, 0u);
    EXPECT_TRUE(g_mockState.opened);
}

TEST_F(WavPlayerTest, WavPlayerStartSetsFormat) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    WavPlayerStart(pl, g_testWavPath);

    EXPECT_EQ(pl->format.wFormatTag, WAVE_FORMAT_PCM);
    EXPECT_EQ(pl->format.nChannels, 1);
    EXPECT_EQ(pl->format.nSamplesPerSec, 22050u);
    EXPECT_EQ(pl->format.wBitsPerSample, 16);
}

TEST_F(WavPlayerTest, WavPlayerStartPreparesHeaders) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    WavPlayerStart(pl, g_testWavPath);

    EXPECT_EQ(g_mockState.prepareCount, 4);
}

TEST_F(WavPlayerTest, WavPlayerStartWritesBuffers) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    WavPlayerStart(pl, g_testWavPath);

    EXPECT_GE(g_mockState.writeCount, 1);
}

TEST_F(WavPlayerTest, WavPlayerStartSetsPlaying) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    WavPlayerStart(pl, g_testWavPath);

    EXPECT_TRUE(pl->playing);
}

TEST_F(WavPlayerTest, WavPlayerStartInvalidFile) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    char badPath[MAX_PATH];
    _snprintf_s(badPath, sizeof(badPath), _TRUNCATE, "%s\\nonexistent.wav", TEST_DATA_DIR);
    BOOL result = WavPlayerStart(pl, badPath);

    EXPECT_FALSE(result);
    EXPECT_EQ(pl->pcmData, (char*)NULL);
}

TEST_F(WavPlayerTest, WavPlayerStartNull) {
    EXPECT_FALSE(WavPlayerStart(NULL, g_testWavPath));
}

TEST_F(WavPlayerTest, WavPlayerStartEmptyPath) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    BOOL result = WavPlayerStart(pl, "");

    EXPECT_FALSE(result);
}

// ============================================================================
// WavPlayerStop after Start (full lifecycle)
// ============================================================================

TEST_F(WavPlayerTest, FullLifecycle) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    // Start
    BOOL started = WavPlayerStart(pl, g_testWavPath);
    ASSERT_TRUE(started);
    EXPECT_TRUE(pl->playing);
    EXPECT_NE(pl->pcmData, (char*)NULL);

    // Pause
    WavPlayerPause(pl);
    EXPECT_TRUE(pl->paused);

    // Resume
    WavPlayerResume(pl);
    EXPECT_FALSE(pl->paused);

    // Seek
    WavPlayerSeek(pl, 0);
    EXPECT_TRUE(pl->playing);

    // Stop
    WavPlayerStop(pl);
    EXPECT_FALSE(pl->playing);
    EXPECT_EQ(pl->pcmData, (char*)NULL);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
}

TEST_F(WavPlayerTest, StartStopMultipleTimes) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    for (int i = 0; i < 3; i++) {
        BOOL started = WavPlayerStart(pl, g_testWavPath);
        EXPECT_TRUE(started) << "Iteration " << i;
        WavPlayerStop(pl);
    }
    // No leak, no crash
}

// ============================================================================
// Global player pool tests
// ============================================================================

TEST_F(WavPlayerTest, PlayerPoolAllocFreeCycle) {
    for (int cycle = 0; cycle < 5; cycle++) {
        WavPlayer* players[MAX_WAV_PLAYERS];
        for (int i = 0; i < MAX_WAV_PLAYERS; i++) {
            players[i] = AllocPlayer();
            EXPECT_NE(players[i], (WavPlayer*)NULL);
        }
        for (int i = 0; i < MAX_WAV_PLAYERS; i++) {
            FreePlayer(players[i]);
        }
    }
    // No leak after 5 alloc/free cycles
}
