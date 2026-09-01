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
//   - Error injection via g_mockFailOpen / g_mockFailNext
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
    int prepareCount;
    int unprepareCount;
    int writeCount;
    void Reset() {
        opened = closed = reset = paused = restarted = FALSE;
        prepareCount = unprepareCount = writeCount = 0;
    }
};

extern MockWaveOutState g_mockState;

// Error injection: set these before calling the function under test
extern BOOL g_mockFailOpen;      // waveOutOpen returns MMSYSERR_ERROR
extern BOOL g_mockFailPrepare;   // waveOutPrepareHeader returns MMSYSERR_ERROR
extern BOOL g_mockFailWrite;     // waveOutWrite returns MMSYSERR_ERROR

// Callback support: stored from waveOutOpen, invokable via FireWaveOutCallback
extern HWAVEOUT  g_mockCallbackHandle;   // handle passed to callback
extern void*     g_mockCallbackPtr;      // WaveOutProc function pointer
extern DWORD_PTR g_mockCallbackInstance;  // dwInstance from waveOutOpen

// Invoke the registered WaveOut callback with WOM_DONE message.
// hdr can be NULL (uses last written header) or a specific WAVEHDR*.
extern "C" void FireWaveOutCallback(WAVEHDR* hdr);

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
