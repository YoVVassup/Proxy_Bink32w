#pragma once
// ============================================================================
// mock_waveout.h — Mock WaveOut API for unit testing wav_player.cpp
//
// Uses preprocessor macros to redirect waveOut* calls to mock implementations.
// wav_player.cpp is compiled as a separate translation unit with these macros
// active, so all waveOut* calls become mock calls.
//
// Features:
//   - Tracks all WaveOut operations (open, close, reset, pause, restart, etc.)
//   - Stores callback pointer for FireCallback() invocation (WOM_DONE testing)
//   - Error injection via g_mockFailOpen/Prepare/Write/Unprepare/Close
//
// Usage in test file:
//   #include "mock_waveout.h"     // must be FIRST
//   #include "../src/wav_player.cpp"  // waves are redirected
// ============================================================================

#include <windows.h>
#include <mmsystem.h>

// Mock state tracker
struct MockWaveOutState {
    BOOL opened;
    BOOL closed;
    BOOL reset;
    BOOL paused;
    BOOL restarted;
    BOOL devicePaused;   // pause state of the modeled device (set by pause/restart
                         // and on open; Reset() deliberately keeps it — it models
                         // the device, not a per-test counter)
    int prepareCount;
    int unprepareCount;
    int writeCount;
    int closeCount;      // waveOutClose attempts, including injected failures
    void Reset() {
        opened = closed = reset = paused = restarted = FALSE;
        prepareCount = unprepareCount = writeCount = closeCount = 0;
    }
};

extern MockWaveOutState g_mockState;

// Error injection: set these before calling the function under test
extern BOOL g_mockFailOpen;      // waveOutOpen returns MMSYSERR_ERROR
extern BOOL g_mockFailPrepare;   // waveOutPrepareHeader returns MMSYSERR_ERROR
extern BOOL g_mockFailWrite;     // waveOutWrite returns MMSYSERR_ERROR
extern BOOL g_mockFailUnprepare; // waveOutUnprepareHeader returns WAVERR_STILLPLAYING
extern BOOL g_mockFailClose;     // waveOutClose returns MMSYSERR_ERROR (device stays open)
extern int  g_mockFailCloseTimes; // the next N waveOutClose calls fail, then recover

// Callback support: stored from waveOutOpen, invokable via FireWaveOutCallback
extern HWAVEOUT  g_mockCallbackHandle;   // handle passed to callback
extern void*     g_mockCallbackPtr;      // WaveOutProc function pointer
extern DWORD_PTR g_mockCallbackInstance;  // dwInstance from waveOutOpen

// When TRUE, waveOutReset returns the buffers (WHDR_INQUEUE cleared, WHDR_DONE
// set) but does NOT dispatch their WOM_DONE yet — the notifications stay
// queued until MockFlushDeferredDone(). Models the real driver, whose reset
// completions arrive asynchronously and may land *after* the player re-queued
// the same headers (WavPlayerSeek race).
extern BOOL g_mockDeferDone;

// Invoke the registered WaveOut callback with WOM_DONE message.
// hdr can be NULL (falls back to the last written header) or a specific
// WAVEHDR*. The pointer passed on is always the real header the player handed
// to waveOutWrite — never a copy, so the player may keep it in `pending`.
extern "C" void FireWaveOutCallback(WAVEHDR* hdr);

// Dispatch every WOM_DONE held back by g_mockDeferDone (flags are whatever the
// player's later waveOutWrite calls left there — i.e. stale notifications for
// headers that are queued again).
extern "C" void MockFlushDeferredDone(void);

extern "C" {
    MMRESULT WINAPI mock_waveOutOpen(LPHWAVEOUT phwo, UINT_PTR uDeviceID,
        LPWAVEFORMATEX pwfx, DWORD_PTR dwCallback,
        DWORD_PTR dwInstance, DWORD fdwOpen);
    MMRESULT WINAPI mock_waveOutClose(HWAVEOUT hwo);
    MMRESULT WINAPI mock_waveOutReset(HWAVEOUT hwo);
    MMRESULT WINAPI mock_waveOutPause(HWAVEOUT hwo);
    MMRESULT WINAPI mock_waveOutRestart(HWAVEOUT hwo);
    MMRESULT WINAPI mock_waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh);
    MMRESULT WINAPI mock_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh);
    MMRESULT WINAPI mock_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh);
}

// Redirect all waveOut* to mocks
#define waveOutOpen mock_waveOutOpen
#define waveOutClose mock_waveOutClose
#define waveOutReset mock_waveOutReset
#define waveOutPause mock_waveOutPause
#define waveOutRestart mock_waveOutRestart
#define waveOutPrepareHeader mock_waveOutPrepareHeader
#define waveOutUnprepareHeader mock_waveOutUnprepareHeader
#define waveOutWrite mock_waveOutWrite
