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

WavPlayer* AllocPlayer() {
    for (int i = 0; i < g_playerCount; i++) {
        if (g_players[i].hWave == NULL && !g_players[i].csValid) {
            memset(&g_players[i], 0, sizeof(WavPlayer));
            InitializeCriticalSection(&g_players[i].cs);
            g_players[i].csValid = TRUE;
            return &g_players[i];
        }
    }
    if (g_playerCount < MAX_WAV_PLAYERS) {
        WavPlayer* pl = &g_players[g_playerCount++];
        memset(pl, 0, sizeof(WavPlayer));
        InitializeCriticalSection(&pl->cs);
        pl->csValid = TRUE;
        return pl;
    }
    return NULL;
}

// FreePlayer — Must NOT be called from the WaveOut callback thread.
// waveOutReset can trigger WOM_DONE callbacks; calling it from within
// a callback causes deadlock (the callback thread cannot re-enter).
void FreePlayer(WavPlayer* pl) {
    if (!pl) return;
    if (!pl->csValid) return;
    if (pl->hWave) {
        EnterCriticalSection(&pl->cs);
        pl->playing = FALSE;
        pl->paused = FALSE;
        LeaveCriticalSection(&pl->cs);
        waveOutReset(pl->hWave);
        EnterCriticalSection(&pl->cs);
        LeaveCriticalSection(&pl->cs);
        for (int i = 0; i < pl->preparedCount; i++) {
            if (pl->headers[i].lpData) {
                waveOutUnprepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
            }
        }
        for (int i = 0; i < 4; i++) {
            if (pl->buffers[i]) {
                VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
                pl->buffers[i] = NULL;
                memset(&pl->headers[i], 0, sizeof(WAVEHDR));
            }
        }
        waveOutClose(pl->hWave);
        pl->hWave = NULL;
    }
    if (pl->pcmData) {
        VirtualFree(pl->pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
    }
    DeleteCriticalSection(&pl->cs);
    pl->csValid = FALSE;
    memset(pl, 0, sizeof(WavPlayer));
}

static void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance,
                                   DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        WavPlayer* pl = (WavPlayer*)dwInstance;
        WAVEHDR* hdr = (WAVEHDR*)dwParam1;

        EnterCriticalSection(&pl->cs);
        if (pl->playing && !pl->paused) {
            DWORD chunkSize = pl->bufSize;
            DWORD remaining = pl->pcmSize - pl->pcmPos;

            if (remaining > 0) {
                DWORD toWrite = remaining < chunkSize ? remaining : chunkSize;
                memcpy(hdr->lpData, pl->pcmData + pl->pcmPos, toWrite);
                if (toWrite < chunkSize)
                    memset(hdr->lpData + toWrite, 0, chunkSize - toWrite);
                hdr->dwBufferLength = chunkSize;
                pl->pcmPos += toWrite;
                waveOutWrite(pl->hWave, hdr, sizeof(WAVEHDR));
            } else {
                pl->playing = FALSE;
            }
        }
        LeaveCriticalSection(&pl->cs);
    }
}

BOOL WavPlayerStart(WavPlayer* pl, const char* audioPath) {
    if (!pl || !audioPath || !audioPath[0]) return FALSE;

    char fullPath[MAX_PATH];
    if (audioPath[1] == ':' || (audioPath[0] == '\\' && audioPath[1] == '\\')) {
        strncpy_s(fullPath, sizeof(fullPath), audioPath, _TRUNCATE);
    } else {
        _snprintf_s(fullPath, sizeof(fullPath), _TRUNCATE, "%s%s", g_dllDir, audioPath);
    }

    DecodedAudio decoded;
    if (!DecodeAudioFile(fullPath, &decoded)) {
        LogF("Audio decode failed: %s", fullPath);
        return FALSE;
    }

    pl->format = decoded.format;
    pl->pcmData = decoded.pcmData;
    pl->pcmSize = decoded.pcmSize;
    pl->pcmPos = 0;
    pl->playing = FALSE;
    pl->paused = FALSE;

    MMRESULT res = waveOutOpen(&pl->hWave, WAVE_MAPPER, &pl->format, (DWORD_PTR)WaveOutProc,
                               (DWORD_PTR)pl, CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        LogF("waveOutOpen failed: %u", res);
        VirtualFree(decoded.pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
        return FALSE;
    }

    pl->bufSize = pl->format.nAvgBytesPerSec / 2;
    if (pl->bufSize < 4096) pl->bufSize = 4096;
    if (pl->format.nAvgBytesPerSec == 0 || pl->format.nBlockAlign == 0) {
        LogF("Invalid audio format: nAvgBytesPerSec=%u nBlockAlign=%u",
             pl->format.nAvgBytesPerSec, pl->format.nBlockAlign);
        WavPlayerStop(pl);
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
    for (int i = 0; i < 4; i++) {
        DWORD remaining = pl->pcmSize - pl->pcmPos;
        if (remaining == 0) break;
        DWORD toWrite = remaining < (DWORD)pl->bufSize ? remaining : (DWORD)pl->bufSize;
        memcpy(pl->buffers[i], pl->pcmData + pl->pcmPos, toWrite);
        if (toWrite < (DWORD)pl->bufSize)
            memset(pl->buffers[i] + toWrite, 0, (DWORD)pl->bufSize - toWrite);
        pl->headers[i].dwBufferLength = pl->bufSize;
        pl->pcmPos += toWrite;
        waveOutWrite(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
    }
    pl->playing = TRUE;
    LeaveCriticalSection(&pl->cs);

    LogF("Audio playback started: %s (%u Hz, %u bit, %u ch)",
         fullPath, pl->format.nSamplesPerSec, pl->format.wBitsPerSample, pl->format.nChannels);
    return TRUE;
}

void WavPlayerStop(WavPlayer* pl) {
    if (!pl || !pl->hWave) return;
    EnterCriticalSection(&pl->cs);
    pl->playing = FALSE;
    pl->paused = FALSE;
    LeaveCriticalSection(&pl->cs);
    waveOutReset(pl->hWave);
    EnterCriticalSection(&pl->cs);
    LeaveCriticalSection(&pl->cs);
    for (int i = 0; i < pl->preparedCount; i++) {
        if (pl->headers[i].lpData) {
            waveOutUnprepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
            VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
            pl->buffers[i] = NULL;
            memset(&pl->headers[i], 0, sizeof(WAVEHDR));
        }
    }
    for (int i = pl->preparedCount; i < 4; i++) {
        if (pl->buffers[i]) {
            VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
            pl->buffers[i] = NULL;
        }
    }
    pl->preparedCount = 0;
    waveOutClose(pl->hWave);
    pl->hWave = NULL;
    if (pl->pcmData) {
        VirtualFree(pl->pcmData, 0, MEM_RELEASE);
        pl->pcmData = NULL;
    }
    LogF("Audio playback stopped");
}

void WavPlayerPause(WavPlayer* pl) {
    if (!pl || !pl->hWave) return;
    EnterCriticalSection(&pl->cs);
    if (!pl->paused) {
        pl->paused = TRUE;
        waveOutPause(pl->hWave);
    }
    LeaveCriticalSection(&pl->cs);
}

void WavPlayerResume(WavPlayer* pl) {
    if (!pl || !pl->hWave) return;
    EnterCriticalSection(&pl->cs);
    if (pl->paused) {
        pl->paused = FALSE;
        waveOutRestart(pl->hWave);
    }
    LeaveCriticalSection(&pl->cs);
}

void WavPlayerSeek(WavPlayer* pl, DWORD sampleOffset) {
    if (!pl || !pl->hWave) return;
    uint64_t byteOffset64 = (uint64_t)sampleOffset * pl->format.nBlockAlign;
    if (byteOffset64 >= pl->pcmSize) byteOffset64 = pl->pcmSize;
    EnterCriticalSection(&pl->cs);
    pl->pcmPos = (DWORD)byteOffset64;
    pl->playing = FALSE;
    LeaveCriticalSection(&pl->cs);
    waveOutReset(pl->hWave);
    EnterCriticalSection(&pl->cs);
    for (int i = 0; i < pl->preparedCount; i++) {
        DWORD remaining = pl->pcmSize - pl->pcmPos;
        if (remaining == 0) break;
        DWORD toWrite = remaining < (DWORD)pl->bufSize ? remaining : (DWORD)pl->bufSize;
        memcpy(pl->buffers[i], pl->pcmData + pl->pcmPos, toWrite);
        if (toWrite < (DWORD)pl->bufSize)
            memset(pl->buffers[i] + toWrite, 0, (DWORD)pl->bufSize - toWrite);
        pl->headers[i].dwBufferLength = pl->bufSize;
        pl->pcmPos += toWrite;
        waveOutWrite(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
    }
    pl->playing = TRUE;
    pl->paused = FALSE;
    LeaveCriticalSection(&pl->cs);
}
