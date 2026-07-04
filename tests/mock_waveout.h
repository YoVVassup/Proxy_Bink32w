#pragma once
// ============================================================================
// mock_waveout.h — Mock WaveOut API for unit testing wav_player.cpp
//
// Uses preprocessor macros to redirect waveOut* calls to mock implementations.
// wav_player.cpp is compiled as a separate translation unit with these macros
// active, so all waveOut* calls become mock calls.
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
