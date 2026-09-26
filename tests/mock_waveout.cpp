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
BOOL g_mockFailUnprepare = FALSE;
BOOL g_mockFailClose = FALSE;
int  g_mockFailCloseTimes = 0;

// Callback state
HWAVEOUT  g_mockCallbackHandle = NULL;
void*     g_mockCallbackPtr = NULL;
DWORD_PTR g_mockCallbackInstance = 0;

BOOL g_mockDeferDone = FALSE;

// Headers the device accepted via waveOutWrite and has not released yet.
// The *real* WAVEHDR* is tracked — never a copy: wav_player parks these
// pointers in WavPlayer::pending, so a stack copy would dangle the moment
// FireWaveOutCallback returned and pause/resume would refill garbage.
static WAVEHDR* g_mockWritten[4];
static int      g_mockWrittenCount = 0;
static WAVEHDR* g_mockLastWritten = NULL;

// Reset completions held back while g_mockDeferDone is set.
static WAVEHDR* g_mockDeferred[4];
static int      g_mockDeferredCount = 0;

// Model of the driver's return: the real waveOut* clears WHDR_INQUEUE and
// sets WHDR_DONE before WOM_DONE is delivered for a genuine completion.
static void MockReturnHeader(WAVEHDR* hdr) {
    if (!hdr) return;
    hdr->dwFlags &= ~WHDR_INQUEUE;
    hdr->dwFlags |= WHDR_DONE;
}

static void MockDispatchDone(WAVEHDR* hdr) {
    if (!g_mockCallbackPtr || !g_mockCallbackInstance || !hdr) return;
    typedef void (CALLBACK *WaveOutProcFn)(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
    WaveOutProcFn proc = (WaveOutProcFn)g_mockCallbackPtr;
    proc(g_mockCallbackHandle, WOM_DONE, g_mockCallbackInstance, (DWORD_PTR)hdr, 0);
}

static void MockForgetHeader(WAVEHDR* pwh) {
    for (int i = 0; i < g_mockWrittenCount; i++) {
        if (g_mockWritten[i] == pwh) {
            g_mockWritten[i] = g_mockWritten[--g_mockWrittenCount];
            return;
        }
    }
}

static void MockTrackHeader(WAVEHDR* pwh) {
    if (!pwh) return;
    for (int i = 0; i < g_mockWrittenCount; i++)
        if (g_mockWritten[i] == pwh) return;
    if (g_mockWrittenCount < 4) g_mockWritten[g_mockWrittenCount++] = pwh;
    g_mockLastWritten = pwh;
}

extern "C" {

MMRESULT WINAPI mock_waveOutOpen(LPHWAVEOUT phwo, UINT_PTR uDeviceID,
    LPWAVEFORMATEX pwfx, DWORD_PTR dwCallback,
    DWORD_PTR dwInstance, DWORD fdwOpen) {
    g_mockState.opened = TRUE;
    if (g_mockFailOpen) return MMSYSERR_ERROR;
    if (phwo) *phwo = g_mockHandle;
    g_mockState.devicePaused = FALSE;
    g_mockCallbackHandle = g_mockHandle;
    g_mockCallbackPtr = (void*)dwCallback;
    g_mockCallbackInstance = dwInstance;
    g_mockWrittenCount = 0;
    g_mockLastWritten = NULL;
    g_mockDeferredCount = 0;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutClose(HWAVEOUT hwo) {
    g_mockState.closeCount++;
    if (g_mockFailClose || g_mockFailCloseTimes > 0) {
        // Model a driver that refuses to close: the device stays alive with
        // its callback and queue intact, so the player must keep the handle
        // (and its slot) instead of pretending the device is gone.
        if (g_mockFailCloseTimes > 0) g_mockFailCloseTimes--;
        return MMSYSERR_ERROR;
    }
    g_mockState.closed = TRUE;
    g_mockCallbackHandle = NULL;
    g_mockCallbackPtr = NULL;
    g_mockCallbackInstance = 0;
    g_mockWrittenCount = 0;
    g_mockLastWritten = NULL;
    g_mockDeferredCount = 0;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutReset(HWAVEOUT hwo) {
    g_mockState.reset = TRUE;
    // Real drivers return every queued buffer as WOM_DONE during a reset.
    // Dispatching them here exercises the barrier/pending paths in wav_player;
    // with g_mockDeferDone the notifications stay queued instead (stale-
    // delivery scenario used by WavPlayerSeek race tests).
    WAVEHDR* queued[4];
    int n = g_mockWrittenCount;
    for (int i = 0; i < n; i++) queued[i] = g_mockWritten[i];
    g_mockWrittenCount = 0;
    g_mockLastWritten = NULL;
    for (int i = 0; i < n; i++) MockReturnHeader(queued[i]);
    for (int i = 0; i < n; i++) {
        if (g_mockDeferDone && g_mockDeferredCount < 4)
            g_mockDeferred[g_mockDeferredCount++] = queued[i];
        else
            MockDispatchDone(queued[i]);
    }
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutPause(HWAVEOUT hwo) {
    g_mockState.paused = TRUE;
    g_mockState.devicePaused = TRUE;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutRestart(HWAVEOUT hwo) {
    g_mockState.restarted = TRUE;
    g_mockState.devicePaused = FALSE;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.prepareCount++;
    if (g_mockFailPrepare) return MMSYSERR_ERROR;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.unprepareCount++;
    if (g_mockFailUnprepare) return WAVERR_STILLPLAYING;
    MockForgetHeader(pwh);
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mock_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    g_mockState.writeCount++;
    if (g_mockFailWrite) return MMSYSERR_ERROR;
    // Real waveOutWrite refuses a header that is already queued and sets
    // WHDR_INQUEUE on acceptance; model both so the stale-WOM_DONE filter in
    // WaveOutProc can be exercised end-to-end.
    if (pwh && (pwh->dwFlags & WHDR_INQUEUE)) return WAVERR_STILLPLAYING;
    if (pwh) pwh->dwFlags |= WHDR_INQUEUE;
    MockTrackHeader(pwh);
    return MMSYSERR_NOERROR;
}

} // extern "C"

void FireWaveOutCallback(WAVEHDR* hdr) {
    if (!g_mockCallbackPtr || !g_mockCallbackInstance) return;
    // Hand the player its own header — the same pointer waveOutWrite took.
    WAVEHDR* real = hdr ? hdr : g_mockLastWritten;
    if (!real) return;
    MockReturnHeader(real);   // genuine completion: INQUEUE cleared first
    MockDispatchDone(real);
}

extern "C" void MockFlushDeferredDone(void) {
    WAVEHDR* queued[4];
    int n = g_mockDeferredCount;
    for (int i = 0; i < n; i++) queued[i] = g_mockDeferred[i];
    g_mockDeferredCount = 0;
    for (int i = 0; i < n; i++) MockDispatchDone(queued[i]);
}
