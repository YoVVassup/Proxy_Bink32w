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
#include "test_helpers.h"

// We need to compile wav_player.cpp with mock macros.
// Instead of including .cpp directly (which would cause ODR issues with
// the main wav_player.o), we test the functions indirectly through their
// effects on the WavPlayer struct and mock state.

#include "binkw32_proxy.h"
#include "audio_decoder.h"
#include <cstring>
#include <thread>
#include <atomic>

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

// A 5-second WAV: long enough that the initial 4-buffer fill leaves data for
// later callbacks (the 100 ms test file is exhausted by the first fill).
static char g_bigWavPath[MAX_PATH];

static void CreateBigWav(int seconds) {
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    _snprintf_s(g_bigWavPath, sizeof(g_bigWavPath), _TRUNCATE,
                "%s\\bink32w_big.wav", tempDir);

    const int numSamples = 22050 * seconds;
    int16_t* samples = (int16_t*)calloc(numSamples, sizeof(int16_t));
    if (!samples) return;
    for (int i = 0; i < numSamples; i++) samples[i] = (int16_t)(i * 3);

    uint32_t dataSize = numSamples * (uint32_t)sizeof(int16_t);
    uint32_t riffSize = 4 + (8 + 16) + (8 + dataSize);
    FILE* f = NULL;
    fopen_s(&f, g_bigWavPath, "wb");
    if (f) {
        fwrite("RIFF", 1, 4, f);
        fwrite(&riffSize, 4, 1, f);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        uint32_t fmtSize = 16;
        fwrite(&fmtSize, 4, 1, f);
        uint16_t formatTag = 1, channels = 1, bits = 16, align = 2;
        uint32_t rate = 22050, avg = 44100;
        fwrite(&formatTag, 2, 1, f);
        fwrite(&channels, 2, 1, f);
        fwrite(&rate, 4, 1, f);
        fwrite(&avg, 4, 1, f);
        fwrite(&align, 2, 1, f);
        fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f);
        fwrite(&dataSize, 4, 1, f);
        fwrite(samples, 1, dataSize, f);
        fclose(f);
    }
    free(samples);
}

static void RemoveBigWav() {
    DeleteFileA(g_bigWavPath);
}

// ============================================================================
// Test fixture
// ============================================================================

class WavPlayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mockState.Reset();
        g_mockFailOpen = FALSE;
        g_mockFailPrepare = FALSE;
        g_mockFailWrite = FALSE;
        g_mockFailUnprepare = FALSE;
        g_mockFailClose = FALSE;
        g_mockFailCloseTimes = 0;
        g_mockDeferDone = FALSE;
        CreateTestWav();
        // Reset player pool. `csValid` is intentionally left alone: the
        // critical section is initialized once per slot and never deleted.
        for (int i = 0; i < MAX_WAV_PLAYERS; i++) {
            g_players[i].hWave = NULL;
            g_players[i].inUse = FALSE;
        }
        g_playerCount = 0;
    }

    void TearDown() override {
        g_mockDeferDone = FALSE;
        g_mockFailClose = FALSE;
        g_mockFailCloseTimes = 0;
        MockFlushDeferredDone();   // drop notifications held back by an aborted test
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
    // A real stream: open device + prepared headers + PCM. Without assertions
    // this test could not fail at all — it only proved FreePlayer survived a
    // fresh (device-less) slot.
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    g_mockState.Reset();

    FreePlayer(pl);

    EXPECT_TRUE(g_mockState.reset)
        << "FreePlayer must waveOutReset the device before tearing it down";
    EXPECT_TRUE(g_mockState.closed) << "the device must be closed";
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_EQ(pl->pcmData, (char*)NULL);
    EXPECT_EQ(pl->pcmSize, 0u);
    EXPECT_FALSE(pl->inUse) << "a released slot must return to the pool";
}

// FreePlayer runs once per slot (UntrackVideo/DllMain never retry), so a
// single refused waveOutClose used to leave HWAVEOUT open and the slot
// reserved forever — after MAX_WAV_PLAYERS the replacement audio died for the
// rest of the session. Bounded in-place retries recover as soon as the driver
// lets go.
TEST_F(WavPlayerTest, FreePlayerRetriesCloseUntilReleased) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    g_mockState.Reset();
    g_mockFailCloseTimes = 1;   // first waveOutClose is refused

    FreePlayer(pl);

    EXPECT_EQ(g_mockState.closeCount, 2)
        << "FreePlayer must retry the refused close instead of parking the slot";
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_EQ(pl->pcmData, (char*)NULL);
    EXPECT_FALSE(pl->inUse) << "the retry must release the slot";
    g_mockFailCloseTimes = 0;
}

TEST_F(WavPlayerTest, FreePlayerDoubleFree) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    FreePlayer(pl);
    // Second free should be a no-op (nothing left to release)
    FreePlayer(pl);
    // No crash = pass
}

TEST_F(WavPlayerTest, FreePlayerNull) {
    int before = g_playerCount;
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    FreePlayer(NULL);

    EXPECT_EQ(g_playerCount, before + 1) << "NULL must not free any slot";
    EXPECT_EQ(g_mockState.closed, FALSE) << "no waveOutClose without a player";
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
    g_mockState.Reset();
    WavPlayerStop(NULL);

    EXPECT_FALSE(g_mockState.reset) << "no waveOutReset without a player";
    EXPECT_EQ(g_mockState.prepareCount, 0);
    EXPECT_EQ(g_mockState.unprepareCount, 0);
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

// The mock must model the device's pause state (open -> running, pause ->
// paused, restart -> running), and g_mockState.Reset() may clear only counters
// — dropping the device state would hide exactly the pause leak the seek test
// looks for.
TEST_F(WavPlayerTest, MockModelsDevicePauseState) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    EXPECT_FALSE(g_mockState.devicePaused) << "a freshly opened device runs";

    WavPlayerPause(pl);
    EXPECT_TRUE(g_mockState.devicePaused);

    g_mockState.Reset();
    EXPECT_TRUE(g_mockState.devicePaused)
        << "Reset() clears counters, not the device's pause state";
    EXPECT_EQ(g_mockState.closeCount, 0) << "but it does clear counters";

    WavPlayerResume(pl);
    EXPECT_FALSE(g_mockState.devicePaused);
    EXPECT_TRUE(g_mockState.restarted);

    WavPlayerStop(pl);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    EXPECT_FALSE(g_mockState.devicePaused) << "a reopened device starts running";
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

// A Goto/seek while paused used to force `paused = FALSE` without telling the
// device: the waveOut stream stayed paused, and WavPlayerResume then saw
// paused == FALSE and skipped waveOutRestart — permanent silence after a seek
// during pause. The seek must keep the pause flag AND re-apply it to the
// device after waveOutReset.
TEST_F(WavPlayerTest, WavPlayerSeekPreservesPausedState) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    WavPlayerPause(pl);
    ASSERT_TRUE(pl->paused);
    ASSERT_TRUE(g_mockState.devicePaused);
    g_mockState.Reset();

    WavPlayerSeek(pl, 0);

    EXPECT_TRUE(pl->paused) << "seek must not unpause the player";
    EXPECT_TRUE(g_mockState.paused)
        << "the device must be re-paused after waveOutReset";
    EXPECT_TRUE(g_mockState.devicePaused);

    WavPlayerResume(pl);

    EXPECT_FALSE(pl->paused);
    EXPECT_TRUE(g_mockState.restarted)
        << "Resume must reach waveOutRestart once the player is paused again";
    EXPECT_FALSE(g_mockState.devicePaused);
}

TEST_F(WavPlayerTest, WavPlayerSeekSetsPlaying) {
    // `playing` may only come back TRUE when buffers were actually requeued,
    // so drive this through a real stream with PCM and prepared headers.
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    pl->playing = FALSE;

    WavPlayerSeek(pl, 0);

    EXPECT_TRUE(pl->playing);
}

TEST_F(WavPlayerTest, WavPlayerSeekPastEndStopsPlaying) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    DWORD size = pl->pcmSize;
    ASSERT_GT(size, 0u);

    WavPlayerSeek(pl, size + 1000);  // far beyond the end (sample units)

    EXPECT_EQ(pl->pcmPos, size);
    EXPECT_FALSE(pl->playing)
        << "seek into the tail must not leave a forever-playing player";
}

TEST_F(WavPlayerTest, StaleResetCallbackAfterSeekKeepsPlaying) {
    // waveOutReset posts WOM_DONE for every queued buffer; winmm delivers them
    // asynchronously, so one can land AFTER WavPlayerSeek re-queued the same
    // headers. Without the WHDR_INQUEUE stale filter the callback rewrites an
    // already-queued header (WAVERR_STILLPLAYING), RefillHeader reports FALSE
    // and playing sticks at FALSE — silence for the rest of the clip.
    CreateBigWav(5);
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_bigWavPath));
    ASSERT_TRUE(pl->playing);

    g_mockDeferDone = TRUE;         // reset completions are delivered late
    WavPlayerSeek(pl, 0);
    ASSERT_TRUE(pl->playing) << "seek must requeue and restart the stream";
    DWORD posAfterSeek = pl->pcmPos;

    MockFlushDeferredDone();        // stale WOM_DONE arrives now

    EXPECT_TRUE(pl->playing)
        << "stale post-reset WOM_DONE must not kill the stream";
    EXPECT_EQ(pl->pcmPos, posAfterSeek)
        << "stale notification must not consume PCM a second time";
    g_mockDeferDone = FALSE;
    RemoveBigWav();
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

// ============================================================================
// Error injection tests
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStartFailsOnWaveOutOpenError) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    g_mockFailOpen = TRUE;

    BOOL result = WavPlayerStart(pl, g_testWavPath);

    EXPECT_FALSE(result);
    EXPECT_EQ(pl->pcmData, (char*)NULL);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
}

TEST_F(WavPlayerTest, WavPlayerStartFailsOnPrepareHeaderError) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    g_mockFailPrepare = TRUE;

    BOOL result = WavPlayerStart(pl, g_testWavPath);

    EXPECT_FALSE(result);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
}

// ============================================================================
// WaveOut callback tests (streaming refill)
// ============================================================================

TEST_F(WavPlayerTest, CallbackRefillsBuffer) {
    // Create a large WAV (5 seconds = 220500 bytes PCM) so the initial
    // 4-buffer fill (4 * 22050 = 88200 bytes) doesn't exhaust all data.
    // The callback should refill a buffer and advance pcmPos.
    char largePath[MAX_PATH];
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    _snprintf_s(largePath, sizeof(largePath), _TRUNCATE, "%s\\bink32w_large.wav", tempDir);

    const int numSamples = 22050 * 5; // 5 seconds at 22050 Hz
    int16_t* samples = (int16_t*)calloc(numSamples, sizeof(int16_t));
    for (int i = 0; i < numSamples; i++) samples[i] = (int16_t)(i * 3);

    uint32_t dataSize = numSamples * sizeof(int16_t);
    uint32_t riffSize = 4 + (8 + 16) + (8 + dataSize);
    FILE* f = NULL;
    fopen_s(&f, largePath, "wb");
    if (f) {
        fwrite("RIFF", 1, 4, f);
        fwrite(&riffSize, 4, 1, f);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        uint32_t fmtSize = 16;
        fwrite(&fmtSize, 4, 1, f);
        uint16_t formatTag = 1, channels = 1, bits = 16, align = 2;
        uint32_t rate = 22050, avg = 44100;
        fwrite(&formatTag, 2, 1, f);
        fwrite(&channels, 2, 1, f);
        fwrite(&rate, 4, 1, f);
        fwrite(&avg, 4, 1, f);
        fwrite(&align, 2, 1, f);
        fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f);
        fwrite(&dataSize, 4, 1, f);
        fwrite(samples, 1, dataSize, f);
        fclose(f);
    }
    free(samples);

    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    BOOL started = WavPlayerStart(pl, largePath);
    ASSERT_TRUE(started);
    ASSERT_TRUE(pl->playing);

    // After initial fill: pcmPos = min(4 * bufSize, pcmSize)
    // bufSize = 22050, 4*bufSize = 88200, pcmSize = 220500
    // So pcmPos = 88200, remaining = 132300
    DWORD pcmPosBefore = pl->pcmPos;
    ASSERT_LT(pcmPosBefore, pl->pcmSize) << "Should have remaining PCM data";
    g_mockState.Reset();

    FireWaveOutCallback(NULL);

    EXPECT_GT(pl->pcmPos, pcmPosBefore) << "pcmPos should advance after callback";
    EXPECT_GE(g_mockState.writeCount, 1) << "callback should call waveOutWrite";

    DeleteFileA(largePath);
}

TEST_F(WavPlayerTest, CallbackStopsWhenDone) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    // Use very short audio to exhaust in one callback
    int16_t tinySamples[100];
    for (int i = 0; i < 100; i++) tinySamples[i] = (int16_t)(i * 10);

    char tinyPath[MAX_PATH];
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    _snprintf_s(tinyPath, sizeof(tinyPath), _TRUNCATE, "%s\\bink32w_tiny.wav", tempDir);

    uint32_t dataSize = sizeof(tinySamples);
    uint32_t riffSize = 4 + (8 + 16) + (8 + dataSize);
    FILE* f = NULL;
    fopen_s(&f, tinyPath, "wb");
    if (f) {
        fwrite("RIFF", 1, 4, f);
        fwrite(&riffSize, 4, 1, f);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        uint32_t fmtSize = 16;
        fwrite(&fmtSize, 4, 1, f);
        uint16_t formatTag = 1, channels = 1, bits = 16, align = 2;
        uint32_t rate = 22050, avg = 44100;
        fwrite(&formatTag, 2, 1, f);
        fwrite(&channels, 2, 1, f);
        fwrite(&rate, 4, 1, f);
        fwrite(&avg, 4, 1, f);
        fwrite(&align, 2, 1, f);
        fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f);
        fwrite(&dataSize, 4, 1, f);
        fwrite(tinySamples, 1, sizeof(tinySamples), f);
        fclose(f);
    }

    WavPlayer* pl2 = AllocPlayer();
    ASSERT_NE(pl2, (WavPlayer*)NULL);
    BOOL started = WavPlayerStart(pl2, tinyPath);
    ASSERT_TRUE(started);

    // Exhaust all PCM data via callbacks. Bounded: a regression that leaves
    // `playing` stuck TRUE would otherwise hang the whole suite instead of
    // failing it.
    int guard = 0;
    while (pl2->playing && guard < 1000) {
        FireWaveOutCallback(NULL);
        guard++;
    }

    EXPECT_FALSE(pl2->playing) << "playing should be FALSE after PCM exhausted";
    DeleteFileA(tinyPath);
}

TEST_F(WavPlayerTest, CallbackNoopWhenPaused) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    BOOL started = WavPlayerStart(pl, g_testWavPath);
    ASSERT_TRUE(started);

    WavPlayerPause(pl);
    DWORD pcmPosBefore = pl->pcmPos;
    g_mockState.Reset();

    FireWaveOutCallback(NULL);

    EXPECT_EQ(pl->pcmPos, pcmPosBefore) << "pcmPos should not change when paused";
    EXPECT_EQ(g_mockState.writeCount, 0) << "no waveOutWrite when paused";
}

// ============================================================================
// WavPlayerStart edge cases
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStartAbsolutePaths) {
    // Absolute path should not prepend g_dllDir
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    BOOL result = WavPlayerStart(pl, g_testWavPath);

    EXPECT_TRUE(result);
    EXPECT_TRUE(g_mockState.opened);
}

TEST_F(WavPlayerTest, WavPlayerStartNullPath) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    EXPECT_FALSE(WavPlayerStart(pl, NULL));
}

// ============================================================================
// waveOutWrite failure handling
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStartFailsWhenWriteFails) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    g_mockFailWrite = TRUE;

    BOOL result = WavPlayerStart(pl, g_testWavPath);

    EXPECT_FALSE(result);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_EQ(pl->pcmData, (char*)NULL);
    EXPECT_FALSE(pl->playing);
}

TEST_F(WavPlayerTest, CallbackStopsOnWriteFailure) {
    CreateBigWav(5);
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_bigWavPath));
    ASSERT_TRUE(pl->playing);
    DWORD posBefore = pl->pcmPos;

    g_mockFailWrite = TRUE;
    g_mockState.Reset();
    FireWaveOutCallback(NULL);

    EXPECT_FALSE(pl->playing)
        << "a refused waveOutWrite must stop the stream, not hang on WOM_DONE";
    EXPECT_EQ(pl->pcmPos, posBefore)
        << "PCM must not be skipped when the device refused the buffer";
    g_mockFailWrite = FALSE;
    RemoveBigWav();
}

// ============================================================================
// waveOutUnprepareHeader / waveOutClose failure handling
// ============================================================================

TEST_F(WavPlayerTest, StopKeepsBuffersWhenUnprepareFails) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    ASSERT_NE(pl->buffers[0], (char*)NULL);
    g_mockState.Reset();
    g_mockFailUnprepare = TRUE;

    WavPlayerStop(pl);

    EXPECT_GT(g_mockState.unprepareCount, 0);
    EXPECT_FALSE(g_mockState.closed)
        << "the device must not be closed while a header is still queued";
    EXPECT_NE(pl->hWave, (HWAVEOUT)NULL) << "handle must be kept for a retry";
    EXPECT_NE(pl->buffers[0], (char*)NULL)
        << "a buffer the device still owns must not be VirtualFree'd";

    // The device is released on a later attempt once the driver cooperates.
    g_mockFailUnprepare = FALSE;
    FreePlayer(pl);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_EQ(pl->buffers[0], (char*)NULL);
    EXPECT_FALSE(pl->inUse);
}

// A close the driver never accepts must NOT be papered over: nulling hWave
// would let AllocPlayer recycle a slot whose HWAVEOUT is still live (its
// callback would then write into the next occupant). FreePlayer keeps the
// handle and the reservation, says so in the log, and retries — this test
// pins the exact number of attempts (1 + 2 bounded retries).
TEST_F(WavPlayerTest, FreePlayerKeepsSlotWhenCloseFails) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    g_mockState.Reset();
    g_mockFailClose = TRUE;

    FreePlayer(pl);

    EXPECT_EQ(g_mockState.closeCount, 3)
        << "one close attempt plus two bounded retries";
    EXPECT_NE(pl->hWave, (HWAVEOUT)NULL)
        << "an unclosed device must keep its handle (nulling it would orphan it)";
    EXPECT_TRUE(pl->inUse) << "the slot must stay reserved while the device lives";

    // Once the driver lets go, the reserved slot is released.
    g_mockFailClose = FALSE;
    FreePlayer(pl);
    EXPECT_EQ(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_FALSE(pl->inUse);
    EXPECT_TRUE(g_mockState.closed);
}

// ============================================================================
// Double start / active slot handling
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStartReplacesActiveStream) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    g_mockState.Reset();

    BOOL second = WavPlayerStart(pl, g_testWavPath);

    EXPECT_TRUE(second);
    EXPECT_TRUE(g_mockState.closed)
        << "the previous device/buffers/PCM must be released before reuse";
    EXPECT_NE(pl->hWave, (HWAVEOUT)NULL);
    EXPECT_NE(pl->pcmData, (char*)NULL);
    EXPECT_TRUE(pl->playing);
}

// ============================================================================
// Pause/resume pending-buffer bookkeeping
// ============================================================================

TEST_F(WavPlayerTest, PauseParksRealHeaderAndResumeRequeuesIt) {
    CreateBigWav(5);
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_bigWavPath));
    ASSERT_TRUE(pl->playing);

    WavPlayerPause(pl);
    DWORD posBefore = pl->pcmPos;
    g_mockState.Reset();

    FireWaveOutCallback(NULL);  // device hands a buffer back while paused

    ASSERT_EQ(pl->pendingCount, 1);
    EXPECT_EQ(pl->pcmPos, posBefore);
    EXPECT_EQ(g_mockState.writeCount, 0);
    WAVEHDR* parked = pl->pending[0];
    EXPECT_TRUE(parked == &pl->headers[0] || parked == &pl->headers[1] ||
                parked == &pl->headers[2] || parked == &pl->headers[3])
        << "pending must hold the player's own WAVEHDR, not a temporary copy";

    WavPlayerResume(pl);

    EXPECT_EQ(pl->pendingCount, 0);
    EXPECT_GT(pl->pcmPos, posBefore)
        << "the parked header must be refilled from pcmPos on resume";
    EXPECT_GE(g_mockState.writeCount, 1);
    RemoveBigWav();
}

// Resume must apply the same rule Start and Seek already apply: `playing`
// promises that another WOM_DONE is coming. The 100 ms fixture WAV is
// exhausted by the initial fill, so the buffer parked during the pause cannot
// be re-queued — before the fix `playing` stayed TRUE forever on a stream that
// had already ended, and the slot looked alive while producing silence.
TEST_F(WavPlayerTest, ResumeAtEndOfStreamClearsPlaying) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);
    ASSERT_TRUE(WavPlayerStart(pl, g_testWavPath));
    ASSERT_TRUE(pl->playing);
    ASSERT_EQ(pl->pcmPos, pl->pcmSize)
        << "fixture WAV must be fully consumed by the initial fill";

    WavPlayerPause(pl);
    ASSERT_TRUE(pl->paused);

    g_mockState.Reset();
    FireWaveOutCallback(NULL);   // device hands a buffer back while paused

    ASSERT_EQ(pl->pendingCount, 1);
    EXPECT_EQ(g_mockState.writeCount, 0) << "no refill while paused";

    WavPlayerResume(pl);

    EXPECT_EQ(pl->pendingCount, 0);
    EXPECT_EQ(g_mockState.writeCount, 0) << "nothing can be queued at end of stream";
    EXPECT_FALSE(pl->playing)
        << "a Resume that re-queues nothing must not leave `playing` pinned";

    WavPlayerStop(pl);
}

// ============================================================================
// Lock-order test (Agent.md 16.4): WavPlayerStart must serialize on devCs
// before it touches waveOutOpen/PrepareHeader/Write, exactly like
// Stop/Free/Pause/Resume/Seek do. Without that, a concurrent Stop could close
// the device between the open and the priming write - the race devCs exists
// to close. The thread below blocks on devCs held by this thread, so a
// regression makes it finish inside the sleep and the test fails.
// ============================================================================

TEST_F(WavPlayerTest, WavPlayerStartTakesDevLock) {
    WavPlayer* pl = AllocPlayer();
    ASSERT_NE(pl, (WavPlayer*)NULL);

    EnterCriticalSection(&pl->devCs);

    std::atomic<bool> done(false);
    std::atomic<bool> started(false);
    std::thread worker([&] {
        started = (WavPlayerStart(pl, g_testWavPath) != FALSE);
        done = true;
    });

    Sleep(200);
    EXPECT_FALSE(done.load())
        << "WavPlayerStart must block on devCs, not race a concurrent Stop/Free";

    LeaveCriticalSection(&pl->devCs);
    worker.join();

    EXPECT_TRUE(done.load());
    EXPECT_TRUE(started.load())
        << "Start must still succeed once devCs is released";

    WavPlayerStop(pl);
}
