#include "binkw32_proxy.h"
#include "audio_decoder.h"
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// Audio player (WaveOut, 4-buffer callback-based)
// Supports WAV and OGG via audio_decoder
// ============================================================================

WavPlayer g_players[MAX_WAV_PLAYERS];
int g_playerCount = 0;

WavPlayer* AllocPlayer() {
    if (g_playerCount < MAX_WAV_PLAYERS) {
        WavPlayer* pl = &g_players[g_playerCount++];
        memset(pl, 0, sizeof(WavPlayer));
        pl->hWave = NULL;
        InitializeCriticalSection(&pl->cs);
        return pl;
    }
    return NULL;
}

void FreePlayer(WavPlayer* pl) {
    if (!pl) return;
    if (pl->hWave) {
        EnterCriticalSection(&pl->cs);
        pl->playing = FALSE;
        pl->paused = FALSE;
        LeaveCriticalSection(&pl->cs);
        waveOutReset(pl->hWave);
        for (int i = 0; i < 4; i++) {
            if (pl->headers[i].lpData) {
                waveOutUnprepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
                VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
            }
        }
        waveOutClose(pl->hWave);
    }
    if (pl->pcmData) VirtualFree(pl->pcmData, 0, MEM_RELEASE);
    DeleteCriticalSection(&pl->cs);
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

void WavPlayerStop(WavPlayer* pl);

BOOL WavPlayerStart(WavPlayer* pl, const char* audioPath) {
    if (!pl || !audioPath) return FALSE;

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
    pl->playing = TRUE;
    pl->paused = FALSE;
    pl->bufIndex = 0;

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
        waveOutPrepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
    }

    for (int i = 0; i < 4; i++) {
        DWORD remaining = pl->pcmSize - pl->pcmPos;
        if (remaining == 0) break;
        DWORD toWrite = remaining < (DWORD)pl->bufSize ? remaining : (DWORD)pl->bufSize;
        memcpy(pl->buffers[i], pl->pcmData + pl->pcmPos, toWrite);
        pl->headers[i].dwBufferLength = pl->bufSize;
        pl->pcmPos += toWrite;
        waveOutWrite(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
    }

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
    for (int i = 0; i < 4; i++) {
        if (pl->headers[i].lpData) {
            waveOutUnprepareHeader(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
            VirtualFree(pl->buffers[i], 0, MEM_RELEASE);
            pl->buffers[i] = NULL;
            memset(&pl->headers[i], 0, sizeof(WAVEHDR));
        }
    }
    waveOutClose(pl->hWave);
    pl->hWave = NULL;
    if (pl->pcmData) { VirtualFree(pl->pcmData, 0, MEM_RELEASE); pl->pcmData = NULL; }
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
    DWORD byteOffset = sampleOffset * pl->format.nBlockAlign;
    if (byteOffset >= pl->pcmSize) byteOffset = pl->pcmSize;
    EnterCriticalSection(&pl->cs);
    pl->pcmPos = byteOffset;
    LeaveCriticalSection(&pl->cs);
    waveOutReset(pl->hWave);
    for (int i = 0; i < 4; i++) {
        DWORD remaining = pl->pcmSize - pl->pcmPos;
        if (remaining == 0) break;
        DWORD toWrite = remaining < (DWORD)pl->bufSize ? remaining : (DWORD)pl->bufSize;
        memcpy(pl->buffers[i], pl->pcmData + pl->pcmPos, toWrite);
        pl->headers[i].dwBufferLength = pl->bufSize;
        pl->pcmPos += toWrite;
        waveOutWrite(pl->hWave, &pl->headers[i], sizeof(WAVEHDR));
    }
}
