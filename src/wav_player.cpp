#include "binkw32_proxy.h"
#include "audio_decoder.h"
#include <mmsystem.h>

// ============================================================================
// wav_player.cpp — WaveOut audio player with 4-buffer streaming
//
// Provides thread-safe audio playback for .wav and .ogg replacement files.
// Uses Windows WaveOut API with callback-based buffer refilling.
//
// Lifecycle:
//   AllocPlayer() -> WavPlayerStart() -> [Pause/Resume/Seek] -> WavPlayerStop() -> FreePlayer()
//
// Thread safety:
//   - CRITICAL_SECTION protects shared state between main thread and WaveOut callback
//   - playing/paused flags are volatile for lock-free reads in callback
//
// Buffer management:
//   4 buffers, each ~0.5 seconds of audio (nAvgBytesPerSec / 2)
//   Callback refills buffers from PCM stream on WOM_DONE events
// ============================================================================

// ============================================================================
// Audio player (WaveOut, 4-buffer callback-based)
// Supports WAV and OGG via audio_decoder
// ============================================================================

WavPlayer g_players[MAX_WAV_PLAYERS];
int g_playerCount = 0;

// Serializes the external waveOut* calls of one slot against each other
// (see the lock order note in binkw32_proxy.h). Taken around the whole
// operation; `cs` may then be entered/leaved inside it. CRITICAL_SECTION is
// recursive, so Start -> Stop nesting is harmless.
namespace {
struct DevLock {
    WavPlayer* pl;
    explicit DevLock(WavPlayer* p) : pl(p) { EnterCriticalSection(&pl->devCs); }
    ~DevLock() { LeaveCriticalSection(&pl->devCs); }
};
}

// Clear every payload field while keeping `cs`/`csValid`/`inUse` intact.
static void ResetPlayerPayload(WavPlayer* pl) {
    pl->hWave = NULL;
    memset(&pl->format, 0, sizeof(pl->format));
    pl->pcmData = NULL;
    pl->pcmSize = 0;
    pl->pcmPos = 0;
    pl->playing = FALSE;
    pl->paused = FALSE;
    memset(pl->headers, 0, sizeof(pl->headers));
    memset(pl->buffers, 0, sizeof(pl->buffers));
    memset(pl->pending, 0, sizeof(pl->pending));
    pl->pendingCount = 0;
    pl->bufSize = 0;
    pl->preparedCount = 0;
}

WavPlayer* AllocPlayer() {
    TrackLock();
    WavPlayer* pl = NULL;
    for (int i = 0; i < g_playerCount; i++) {
        if (!g_players[i].inUse) {
            pl = &g_players[i];
            break;
        }
    }
    if (!pl && g_playerCount < MAX_WAV_PLAYERS)
        pl = &g_players[g_playerCount++];
    if (pl) {
        if (!pl->csValid) {
            InitializeCriticalSection(&pl->cs);
            InitializeCriticalSection(&pl->devCs);
            pl->csValid = TRUE;
        }
        ResetPlayerPayload(pl);
        pl->inUse = TRUE;
    }
    TrackUnlock();
    return pl;
}

// Copy the next chunk of PCM into `hdr` and hand it back to the device.
// Returns TRUE when data was consumed (the caller may keep `playing` set),
// FALSE when the stream is exhausted *or* the device refused the buffer.
//
// On a waveOutWrite failure pcmPos is NOT advanced: the device never took the
// chunk, so no WOM_DONE will ever return it. Returning FALSE makes the caller
// stop the stream instead of silently skipping the chunk and then spinning
// forever waiting for callbacks that can no longer arrive.
// Caller must hold pl->cs.
static BOOL RefillHeader(WavPlayer* pl, WAVEHDR* hdr) {
    if (!pl->pcmData || !pl->hWave || !hdr || !hdr->lpData || pl->bufSize <= 0)
        return FALSE;
    DWORD chunkSize = (DWORD)pl->bufSize;
    DWORD remaining = pl->pcmSize - pl->pcmPos;
    if (remaining == 0) return FALSE;

    DWORD toWrite = remaining < chunkSize ? remaining : chunkSize;
    memcpy(hdr->lpData, pl->pcmData + pl->pcmPos, toWrite);
    if (toWrite < chunkSize)
        memset(hdr->lpData + toWrite, 0, chunkSize - toWrite);
    hdr->dwBufferLength = chunkSize;

    MMRESULT wr = waveOutWrite(pl->hWave, hdr, sizeof(WAVEHDR));
    if (wr != MMSYSERR_NOERROR) {
        hdr->dwBufferLength = 0;
        LogF("waveOutWrite failed: %u (buffer not queued, playback stops)", wr);
        return FALSE;
    }
    pl->pcmPos += toWrite;
    return TRUE;
}

// Unprepare every prepared header and close the device.
//
// The caller must have cleared `playing` under `cs` and taken a `cs` barrier
// so no callback is inside WaveOutProc, and must NOT hold `cs` here: waveOut*
// may block waiting for driver threads that need to run WaveOutProc, which
// takes `cs` — holding it across the call would deadlock.
//
// A header the device refuses to release (WAVERR_STILLPLAYING) is retried a
// few times and, if it still fails, is left alive together with its buffer:
// VirtualFree'ing memory the device still points at is a use-after-free. The
// device is then kept open as well, so a later Stop/FreePlayer can retry
// instead of nulling `hWave` and leaking the handle silently (FreePlayer
// retries in place, see its bounded close loop).
// Returns TRUE when the device is fully closed (pl->hWave == NULL).
static BOOL ReleaseWaveDevice(WavPlayer* pl) {
    if (!pl->hWave) {
        // No device: buffers can only be orphaned leftovers of a failed open.
        for (int i = 0; i < 4; i++) {
            if (pl->buffers[i]) {
                VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
                pl->buffers[i] = NULL;
                memset(&pl->headers[i], 0, sizeof(WAVEHDR));
            }
        }
        pl->preparedCount = 0;
        return TRUE;
    }

    BOOL released = TRUE;
    for (int i = 0; i < pl->preparedCount; i++) {
        if (!pl->buffers[i]) continue;
        if (pl->headers[i].lpData) {
            // waveOutReset should already have returned the buffer; retry a
            // couple of times for drivers that catch up late. Keep it short:
            // this also runs under loader lock in the DllMain path.
            MMRESULT ur = MMSYSERR_NOERROR;
            for (int attempt = 0; attempt < 3; attempt++) {
                ur = waveOutUnprepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
                if (ur == MMSYSERR_NOERROR || ur != WAVERR_STILLPLAYING) break;
                Sleep(1);
            }
            if (ur != MMSYSERR_NOERROR) {
                LogF("waveOutUnprepareHeader failed for buffer %d: %u (buffer and device kept)",
                     i, ur);
                released = FALSE;
                continue;
            }
        }
        VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
        pl->buffers[i] = NULL;
        memset(&pl->headers[i], 0, sizeof(WAVEHDR));
    }
    for (int i = pl->preparedCount; i < 4; i++) {
        if (pl->buffers[i]) {
            VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
            pl->buffers[i] = NULL;
            memset(&pl->headers[i], 0, sizeof(WAVEHDR));
        }
    }
    if (!released) return FALSE;

    MMRESULT cr = waveOutClose(pl->hWave);
    if (cr != MMSYSERR_NOERROR) {
        LogF("waveOutClose failed: %u (device kept open for retry)", cr);
        return FALSE;
    }
    pl->hWave = NULL;
    pl->preparedCount = 0;
    return TRUE;
}

// FreePlayer — Must NOT be called from the WaveOut callback thread.
// waveOutReset can trigger WOM_DONE callbacks; calling it from within
// a callback causes deadlock (the callback thread cannot re-enter).
//
// `cs` is never deleted and the struct is never memset as a whole.
// waveOutReset() may leave a WOM_DONE callback queued that runs after this
// function returns; such a callback re-enters `cs`, observes playing == FALSE
// and touches nothing else. Payload is only freed once `playing` is cleared
// under the lock, so an in-flight callback can never read freed memory.
void FreePlayer(WavPlayer* pl) {
    if (!pl) return;
    if (!pl->csValid) return;

    DevLock devLock(pl);
    HWAVEOUT h;
    EnterCriticalSection(&pl->cs);
    pl->playing = FALSE;
    pl->paused = FALSE;
    pl->pendingCount = 0;
    h = pl->hWave;
    LeaveCriticalSection(&pl->cs);

    if (h) {
        MMRESULT rr = waveOutReset(h);
        if (rr != MMSYSERR_NOERROR) LogF("waveOutReset failed: %u", rr);
        // Barrier: wait out a callback that is already inside `cs`.
        EnterCriticalSection(&pl->cs);
        LeaveCriticalSection(&pl->cs);
    }
    BOOL closed = ReleaseWaveDevice(pl);
    if (!closed) {
        // The driver can release a buffer slightly after the first reset/
        // unprepare pass. Retry briefly instead of parking the slot forever:
        // the caller (UntrackVideo, DllMain) frees each player exactly once,
        // so a single refused close would leak HWAVEOUT until process exit.
        for (int attempt = 0; attempt < 2 && !closed; attempt++) {
            Sleep(1);
            if (pl->hWave) {
                MMRESULT rr = waveOutReset(pl->hWave);
                if (rr != MMSYSERR_NOERROR) LogF("waveOutReset failed: %u", rr);
                // Barrier: wait out a callback that reset may already have started.
                EnterCriticalSection(&pl->cs);
                LeaveCriticalSection(&pl->cs);
            }
            closed = ReleaseWaveDevice(pl);
        }
    }
    if (pl->pcmData) {
        VirtualFree(pl->pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
    }
    pl->pcmSize = 0;
    pl->pcmPos = 0;
    pl->bufSize = 0;
    pl->pendingCount = 0;
    if (!closed) {
        // Device still holds a buffer/handle: keep the slot reserved so the
        // next AllocPlayer cannot recycle it and orphan HWAVEOUT. Name the
        // handle and the slot: the caller has no way to see either.
        LogF("FreePlayer: device not released (HWAVEOUT %p), slot %d stays reserved",
             (void*)pl->hWave, (int)(pl - g_players));
        return;
    }
    pl->preparedCount = 0;
    pl->inUse = FALSE;
}

static void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance,
                                   DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg != WOM_DONE) return;
    WavPlayer* pl = (WavPlayer*)dwInstance;
    WAVEHDR* hdr = (WAVEHDR*)dwParam1;
    if (!pl || !pl->csValid) return;

    EnterCriticalSection(&pl->cs);
    // Stale notification filter. waveOutReset (Stop/Seek) returns every queued
    // buffer and winmm posts their WOM_DONE asynchronously; if it arrives after
    // WavPlayerSeek already re-queued the same header, the header is queued
    // again (WHDR_INQUEUE set by waveOutWrite). Refilling it would overwrite a
    // buffer the device is playing, and waveOutWrite would fail with
    // WAVERR_STILLPLAYING -> RefillHeader FALSE -> playing=FALSE forever ->
    // silence for the rest of the clip. Genuine returns have WHDR_INQUEUE
    // cleared by the driver before the callback runs, so only the stale copy
    // is dropped; the re-queued buffer already holds the right data.
    if (hdr && (hdr->dwFlags & WHDR_INQUEUE)) {
        LeaveCriticalSection(&pl->cs);
        return;
    }
    if (pl->paused && pl->playing) {
        // Hold the returned buffer instead of dropping it. waveOutPause
        // stops the device from consuming anything new, and WavPlayerResume()
        // re-primes these from pcmPos — otherwise every pause/resume cycle
        // permanently shrinks the pool until playback deadlocks.
        BOOL dup = FALSE;
        for (int i = 0; i < pl->pendingCount; i++)
            if (pl->pending[i] == hdr) { dup = TRUE; break; }
        if (!dup && hdr && pl->pendingCount < 4)
            pl->pending[pl->pendingCount++] = hdr;
        LeaveCriticalSection(&pl->cs);
        return;
    }
    if (pl->playing && !RefillHeader(pl, hdr))
        pl->playing = FALSE;
    LeaveCriticalSection(&pl->cs);
}

BOOL WavPlayerStart(WavPlayer* pl, const char* audioPath) {
    if (!pl || !audioPath || !audioPath[0]) return FALSE;
    if (!pl->csValid) return FALSE;

    // A slot that is still holding a stream (open device, prepared buffers or
    // PCM) must be torn down first: overwriting hWave/buffers/pcmData here
    // would leak the previous HWAVEOUT, its 4 buffers and its PCM buffer.
    if (pl->hWave || pl->pcmData) {
        LogF("WavPlayerStart: slot still active, stopping previous stream");
        WavPlayerStop(pl);
    }

    char fullPath[MAX_PATH];
    if (audioPath[1] == ':' || (audioPath[0] == '\\' && audioPath[1] == '\\')) {
        WarnTruncate("audio replacement path", audioPath, sizeof(fullPath));
        strncpy_s(fullPath, sizeof(fullPath), audioPath, _TRUNCATE);
    } else {
        if (strlen(g_dllDir) + strlen(audioPath) >= sizeof(fullPath))
            LogF("WARNING: audio replacement path longer than %u chars, truncated: %s%s",
                 (unsigned)(sizeof(fullPath) - 1), g_dllDir, audioPath);
        _snprintf_s(fullPath, sizeof(fullPath), _TRUNCATE, "%s%s", g_dllDir, audioPath);
    }

    DecodedAudio decoded;
    if (!DecodeAudioFile(fullPath, &decoded)) {
        LogF("Audio decode failed: %s", fullPath);
        return FALSE;
    }

    // Every waveOut* call below runs under devCs, as the lock-order contract
    // in binkw32_proxy.h requires: without it a concurrent Stop/Free/Pause
    // (each takes DevLock first) could close the device between waveOutOpen,
    // waveOutPrepareHeader and the priming waveOutWrite - the very race devCs
    // exists to close. `cs` is still taken only inside this scope, so the
    // order stays devCs -> cs, and DevLock is recursive, which the
    // WavPlayerStop() calls below rely on. The decode above stays outside the
    // lock on purpose: it is the slow part and touches no device state.
    DevLock devLock(pl);

    // Written under `cs`: a callback left over from the previous stream may
    // still be inside WaveOutProc reading these fields.
    EnterCriticalSection(&pl->cs);
    pl->format = decoded.format;
    pl->pcmData = decoded.pcmData;
    pl->pcmSize = decoded.pcmSize;
    pl->pcmPos = 0;
    pl->playing = FALSE;
    pl->paused = FALSE;
    pl->pendingCount = 0;
    LeaveCriticalSection(&pl->cs);

    // Validate before touching the device, and keep every chunk a whole
    // number of frames — waveOut rejects buffers that split nBlockAlign.
    if (pl->format.nAvgBytesPerSec == 0 || pl->format.nBlockAlign == 0) {
        LogF("Invalid audio format: nAvgBytesPerSec=%u nBlockAlign=%u",
             pl->format.nAvgBytesPerSec, pl->format.nBlockAlign);
        VirtualFree(decoded.pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
        pl->pcmSize = 0;
        return FALSE;
    }
    pl->bufSize = pl->format.nAvgBytesPerSec / 2;
    if (pl->bufSize < 4096) pl->bufSize = 4096;
    pl->bufSize -= pl->bufSize % pl->format.nBlockAlign;
    if (pl->bufSize < pl->format.nBlockAlign) pl->bufSize = pl->format.nBlockAlign;

    MMRESULT res = waveOutOpen(&pl->hWave, WAVE_MAPPER, &pl->format, (DWORD_PTR)WaveOutProc,
                               (DWORD_PTR)pl, CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        LogF("waveOutOpen failed: %u (rate=%u ch=%u bits=%u)",
             res, pl->format.nSamplesPerSec, pl->format.nChannels,
             pl->format.wBitsPerSample);
        VirtualFree(decoded.pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
        pl->pcmSize = 0;
        return FALSE;
    }

    pl->preparedCount = 0;
    for (int i = 0; i < 4; i++) {
        pl->buffers[i] = (char*)VirtualAlloc(NULL, pl->bufSize, MEM_COMMIT, PAGE_READWRITE);
        if (!pl->buffers[i]) {
            LogF("VirtualAlloc failed for audio buffer %d", i);
            WavPlayerStop(pl);
            return FALSE;
        }
        memset(&pl->headers[i], 0, sizeof(WAVEHDR));
        pl->headers[i].lpData = pl->buffers[i];
        pl->headers[i].dwBufferLength = pl->bufSize;
        MMRESULT prepRes = waveOutPrepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
        if (prepRes != MMSYSERR_NOERROR) {
            LogF("waveOutPrepareHeader failed for buffer %d: %u", i, prepRes);
            pl->headers[i].lpData = NULL;
            WavPlayerStop(pl);
            return FALSE;
        }
        pl->preparedCount++;
    }

    EnterCriticalSection(&pl->cs);
    int queued = 0;
    for (int i = 0; i < 4; i++) {
        if (!RefillHeader(pl, &pl->headers[i])) break;
        queued++;
    }
    // Nothing queued means no WOM_DONE will ever arrive: starting the stream
    // anyway would leave `playing` pinned TRUE with silence.
    pl->playing = (queued > 0);
    LeaveCriticalSection(&pl->cs);

    if (!pl->playing) {
        LogF("Audio playback could not queue a buffer: %s", fullPath);
        WavPlayerStop(pl);
        return FALSE;
    }

    LogF("Audio playback started: %s (%u Hz, %u bit, %u ch)",
         fullPath, pl->format.nSamplesPerSec, pl->format.wBitsPerSample, pl->format.nChannels);
    return TRUE;
}
void WavPlayerStop(WavPlayer* pl) {
    if (!pl || !pl->csValid) return;

    DevLock devLock(pl);
    HWAVEOUT h;
    EnterCriticalSection(&pl->cs);
    pl->playing = FALSE;
    pl->paused = FALSE;
    pl->pendingCount = 0;
    h = pl->hWave;
    LeaveCriticalSection(&pl->cs);

    if (h) {
        MMRESULT rr = waveOutReset(h);
        if (rr != MMSYSERR_NOERROR) LogF("waveOutReset failed: %u", rr);
        // Barrier: wait out a callback that is already inside `cs`.
        EnterCriticalSection(&pl->cs);
        LeaveCriticalSection(&pl->cs);
    }
    ReleaseWaveDevice(pl);
    if (pl->pcmData) {
        VirtualFree(pl->pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
    }
    pl->pcmSize = 0;
    pl->pcmPos = 0;
    LogF("Audio playback stopped");
}

void WavPlayerPause(WavPlayer* pl) {
    if (!pl || !pl->csValid) return;
    // `devCs` is taken FIRST (lock order devCs -> cs): it keeps the snapshot,
    // the `pl->paused` flip and waveOutPause indivisible against a concurrent
    // Stop/Free that would close the device. `cs` only guards the payload.
    DevLock devLock(pl);
    HWAVEOUT h;
    BOOL doPause = FALSE;
    EnterCriticalSection(&pl->cs);
    h = pl->hWave;
    if (h && !pl->paused) {
        pl->paused = TRUE;
        doPause = TRUE;
    }
    LeaveCriticalSection(&pl->cs);
    if (!doPause) return;
    MMRESULT r = waveOutPause(h);
    if (r != MMSYSERR_NOERROR) LogF("waveOutPause failed: %u", r);
}

void WavPlayerResume(WavPlayer* pl) {
    if (!pl || !pl->csValid) return;
    DevLock devLock(pl);
    HWAVEOUT h;
    BOOL doResume = FALSE;
    EnterCriticalSection(&pl->cs);
    h = pl->hWave;
    if (h && pl->paused) {
        pl->paused = FALSE;
        doResume = TRUE;
    }
    LeaveCriticalSection(&pl->cs);
    if (!doResume) return;
    MMRESULT r = waveOutRestart(h);
    if (r != MMSYSERR_NOERROR) LogF("waveOutRestart failed: %u", r);
    // Buffers returned by WOM_DONE while the device was paused are parked in
    // `pending`; put them back in circulation or the pool shrinks on every
    // pause/resume cycle. Re-entering `cs` re-reads `hWave`: a Stop that ran
    // meanwhile makes RefillHeader bail out harmlessly.
    EnterCriticalSection(&pl->cs);
    int parked = pl->pendingCount;
    int requeued = 0;
    for (int i = 0; i < parked; i++)
        if (RefillHeader(pl, pl->pending[i])) requeued++;
    pl->pendingCount = 0;
    // Same rule Start and Seek already apply: `playing` promises that another
    // WOM_DONE is coming. If every parked buffer came back FALSE (stream
    // already consumed to pcmSize, or the device refused the write) nothing
    // is queued and no callback will ever arrive, so leaving `playing` set
    // makes the slot look alive forever while producing silence.
    if (parked > 0 && requeued == 0 && pl->playing) {
        pl->playing = FALSE;
        LogF("WavPlayerResume: none of the %d parked buffer(s) could be "
             "re-queued, playback ended", parked);
    }
    LeaveCriticalSection(&pl->cs);
}

void WavPlayerSeek(WavPlayer* pl, DWORD sampleOffset) {
    if (!pl || !pl->csValid) return;
    DevLock devLock(pl);
    HWAVEOUT h;
    BOOL wasPaused;
    EnterCriticalSection(&pl->cs);
    uint64_t byteOffset64 = (uint64_t)sampleOffset * pl->format.nBlockAlign;
    if (byteOffset64 >= pl->pcmSize) byteOffset64 = pl->pcmSize;
    pl->pcmPos = (DWORD)byteOffset64;
    pl->playing = FALSE;
    pl->pendingCount = 0;
    h = pl->hWave;
    wasPaused = pl->paused;
    LeaveCriticalSection(&pl->cs);
    if (!h) return;
    MMRESULT rr = waveOutReset(h);
    if (rr != MMSYSERR_NOERROR) LogF("waveOutReset failed: %u", rr);
    // Barrier (mirrors WavPlayerStop): wait out a callback that reset may have
    // already started inside `cs` before we re-queue, so it can only observe
    // playing == FALSE. Callbacks still sitting in winmm's queue are handled
    // by the WHDR_INQUEUE stale filter in WaveOutProc.
    EnterCriticalSection(&pl->cs);
    LeaveCriticalSection(&pl->cs);
    // Re-apply the device pause state that reset may have disturbed. Skipping
    // this left a Goto during pause permanently silent: `paused` was forced to
    // FALSE by Seek while the device stayed paused, and WavPlayerResume then
    // found nothing to waveOutRestart. Both calls are no-ops in their idle
    // direction, so this is safe whatever reset did to the pause state.
    if (wasPaused) {
        MMRESULT pr = waveOutPause(h);
        if (pr != MMSYSERR_NOERROR) LogF("waveOutPause failed: %u", pr);
    } else {
        MMRESULT pr = waveOutRestart(h);
        if (pr != MMSYSERR_NOERROR) LogF("waveOutRestart failed: %u", pr);
    }
    EnterCriticalSection(&pl->cs);
    pl->pendingCount = 0;
    int queued = 0;
    for (int i = 0; i < pl->preparedCount; i++) {
        if (!RefillHeader(pl, &pl->headers[i])) break;
        queued++;
    }
    // Seeking into the tail (or past the end) may leave nothing to queue;
    // claiming `playing` then would pin the player as "playing" forever.
    pl->playing = (queued > 0);
    // Restore the pause flag the caller had: a seek must not silently unpause
    // the player (the device was re-paused above when it was paused).
    pl->paused = wasPaused;
    LeaveCriticalSection(&pl->cs);
}
