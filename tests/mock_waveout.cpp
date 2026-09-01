#include "mock_waveout.h"

// ============================================================================
// mock_waveout.cpp — Mock WaveOut implementations with extern "C" linkage
// ============================================================================

MockWaveOutState g_mockState;
static HWAVEOUT g_mockHandle = (HWAVEOUT)0x12345678;

// Error injection
BOOL g_mockFailOpen = FALSE;
BOOL g_mockFailPrepare = FALSE;
BOOL g_mockFailWrite = FALSE;

// Callback state
HWAVEOUT  g_mockCallbackHandle = NULL;
void*     g_mockCallbackPtr = NULL;
DWORD_PTR g_mockCallbackInstance = 0;
static WAVEHDR g_mockLastWrittenHdr = {};
static BOOL g_mockHasLastHdr = FALSE;

extern "C" {

MMRESULT WINAPI mock_waveOutOpen(LPHWAVEOUT phwo, UINT_PTR uDeviceID,
    LPWAVEFORMATEX pwfx, DWORD_PTR dwCallback,
    DWORD_PTR dwInstance, DWORD fdwOpen) {
    g_mockState.opened = TRUE;
    if (g_mockFailOpen) return MMSYSERR_ERROR;
    if (phwo) *phwo = g_mockHandle;
    g_mockCallbackHandle = g_mockHandle;
    g_mockCallbackPtr = (void*)dwCallback;
    g_mockCallbackInstance = dwInstance;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutClose(HWAVEOUT hwo) {
    g_mockState.closed = TRUE;
    g_mockCallbackHandle = NULL;
    g_mockCallbackPtr = NULL;
    g_mockCallbackInstance = 0;
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
    if (g_mockFailPrepare) return MMSYSERR_ERROR;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.unprepareCount++;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.writeCount++;
    if (g_mockFailWrite) return MMSYSERR_ERROR;
    if (pwh) {
        g_mockLastWrittenHdr = *pwh;
        g_mockHasLastHdr = TRUE;
    }
    return MMSYSERR_NOERROR;
}

} // extern "C"

void FireWaveOutCallback(WAVEHDR* hdr) {
    if (!g_mockCallbackPtr || !g_mockCallbackInstance) return;
    typedef void (CALLBACK *WaveOutProcFn)(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
    WaveOutProcFn proc = (WaveOutProcFn)g_mockCallbackPtr;
    WAVEHDR localHdr = {};
    if (hdr) {
        localHdr = *hdr;
    } else if (g_mockHasLastHdr) {
        localHdr = g_mockLastWrittenHdr;
    } else {
        return;
    }
    proc(g_mockCallbackHandle, WOM_DONE, g_mockCallbackInstance, (DWORD_PTR)&localHdr, 0);
}
