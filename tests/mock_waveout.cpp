#include "mock_waveout.h"

// ============================================================================
// mock_waveout.cpp — Mock WaveOut implementations with extern "C" linkage
// ============================================================================

MockWaveOutState g_mockState;
static HWAVEOUT g_mockHandle = (HWAVEOUT)0x12345678;

extern "C" {

MMRESULT WINAPI mock_waveOutOpen(LPHWAVEOUT phwo, UINT_PTR uDeviceID,
    LPWAVEFORMATEX pwfx, DWORD_PTR dwCallback,
    DWORD_PTR dwInstance, DWORD fdwOpen) {
    g_mockState.opened = TRUE;
    if (phwo) *phwo = g_mockHandle;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutClose(HWAVEOUT hwo) {
    g_mockState.closed = TRUE;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutReset(HWAVEOUT hwo) {
    g_mockState.reset = TRUE;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutPause(HWAVEOUT hwo) {
    g_mockState.paused = TRUE;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutRestart(HWAVEOUT hwo) {
    g_mockState.restarted = TRUE;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.prepareCount++;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.unprepareCount++;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.writeCount++;
    return MMSYSERR_NOERROR;
}

} // extern "C"
