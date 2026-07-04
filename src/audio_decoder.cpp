#include "audio_decoder.h"
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

extern void LogF(const char* fmt, ...);

// ============================================================================
// audio_decoder.cpp — Unified WAV + OGG decoder
//
// Provides a single entry point (DecodeAudioFile) that handles both WAV and
// OGG formats transparently. WAV uses chunk-based RIFF parsing, OGG uses
// stb_vorbis v1.22 (public domain single-header library).
//
// Flow: file extension -> format detection -> decode to PCM -> return buffer
// Both formats output interleaved PCM data suitable for WaveOut playback.
//
// stb_vorbis.c is included here with STB_VORBIS_IMPLEMENTATION to compile
// the decoder into this translation unit.
// ============================================================================

#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"

static BOOL DecodeWav(const char* path, DecodedAudio* out) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize < 12) { CloseHandle(hFile); return FALSE; }

    char riffHdr[12];
    DWORD read;
    if (!ReadFile(hFile, riffHdr, 12, &read, NULL) || read != 12) { CloseHandle(hFile); return FALSE; }

    if (memcmp(riffHdr, "RIFF", 4) != 0 || memcmp(riffHdr + 8, "WAVE", 4) != 0) {
        CloseHandle(hFile); return FALSE;
    }

    WORD channels = 0;
    DWORD sampleRate = 0;
    WORD bitsPerSample = 0;
    DWORD dataSize = 0;
    BOOL foundFmt = FALSE;
    BOOL foundData = FALSE;

    while (SetFilePointer(hFile, 0, NULL, FILE_CURRENT) < fileSize - 8) {
        char chunkId[4];
        DWORD chunkSize;
        if (!ReadFile(hFile, chunkId, 4, &read, NULL) || read != 4) break;
        if (!ReadFile(hFile, &chunkSize, 4, &read, NULL) || read != 4) break;

        if (memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            char fmtData[16];
            if (!ReadFile(hFile, fmtData, 16, &read, NULL) || read != 16) break;
            WORD formatTag = (WORD)((unsigned char)fmtData[0] | ((unsigned char)fmtData[1] << 8));
            if (formatTag != WAVE_FORMAT_PCM) {
                CloseHandle(hFile); return FALSE;
            }
            channels = (WORD)((unsigned char)fmtData[2] | ((unsigned char)fmtData[3] << 8));
            sampleRate = (unsigned char)fmtData[4] | ((unsigned char)fmtData[5] << 8) |
                         ((unsigned char)fmtData[6] << 16) | ((unsigned char)fmtData[7] << 24);
            bitsPerSample = (WORD)((unsigned char)fmtData[14] | ((unsigned char)fmtData[15] << 8));
            foundFmt = TRUE;
            DWORD skipFmt = chunkSize - 16;
            if (chunkSize > 16 && skipFmt <= fileSize - (DWORD)SetFilePointer(hFile, 0, NULL, FILE_CURRENT))
                SetFilePointer(hFile, skipFmt, NULL, FILE_CURRENT);
        } else if (memcmp(chunkId, "data", 4) == 0) {
            DWORD remaining = fileSize - (DWORD)SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
            dataSize = chunkSize < remaining ? chunkSize : remaining;
            foundData = TRUE;
            break;
        } else {
            if (chunkSize > 0x7FFFFFFF) break;
            DWORD skip = chunkSize + (chunkSize & 1);
            SetFilePointer(hFile, skip, NULL, FILE_CURRENT);
        }
    }

    if (!foundFmt || !foundData || channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
        CloseHandle(hFile); return FALSE;
    }

    if (channels > 8 || (bitsPerSample != 8 && bitsPerSample != 16)) {
        CloseHandle(hFile); return FALSE;
    }

    out->format.wFormatTag = WAVE_FORMAT_PCM;
    out->format.nChannels = channels;
    out->format.nSamplesPerSec = sampleRate;
    out->format.wBitsPerSample = bitsPerSample;
    out->format.nBlockAlign = (channels * bitsPerSample) / 8;
    out->format.nAvgBytesPerSec = sampleRate * out->format.nBlockAlign;
    out->format.cbSize = 0;

    out->pcmData = (char*)VirtualAlloc(NULL, dataSize, MEM_COMMIT, PAGE_READWRITE);
    if (!out->pcmData) { CloseHandle(hFile); return FALSE; }

    if (!ReadFile(hFile, out->pcmData, dataSize, &read, NULL) || read == 0) {
        VirtualFree(out->pcmData, 0, MEM_RELEASE);
        out->pcmData = NULL;
        CloseHandle(hFile); return FALSE;
    }
    CloseHandle(hFile);

    out->pcmSize = read;
    return TRUE;
}

static BOOL DecodeOgg(const char* path, DecodedAudio* out) {
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &error, NULL);
    if (!v) {
        LogF("stb_vorbis_open_filename failed: %s (error %d)", path, error);
        return FALSE;
    }

    stb_vorbis_info info = stb_vorbis_get_info(v);

    int totalSamples = stb_vorbis_stream_length_in_samples(v);
    if (totalSamples <= 0) {
        LogF("stb_vorbis: invalid stream length %d for %s", totalSamples, path);
        stb_vorbis_close(v);
        return FALSE;
    }
    int channels = info.channels;
    int sampleRate = info.sample_rate;

    out->format.wFormatTag = WAVE_FORMAT_PCM;
    out->format.nChannels = (WORD)channels;
    out->format.nSamplesPerSec = sampleRate;
    out->format.wBitsPerSample = 16;
    out->format.nBlockAlign = (WORD)(channels * 2);
    out->format.nAvgBytesPerSec = sampleRate * out->format.nBlockAlign;
    out->format.cbSize = 0;

    uint64_t pcmBytes64 = (uint64_t)totalSamples * channels * 2;
    if (pcmBytes64 == 0 || pcmBytes64 > 0x7FFFFFFF) {
        LogF("stb_vorbis: invalid pcm size %llu for %s", pcmBytes64, path);
        stb_vorbis_close(v);
        return FALSE;
    }
    DWORD pcmBytes = (DWORD)pcmBytes64;
    out->pcmData = (char*)VirtualAlloc(NULL, pcmBytes, MEM_COMMIT, PAGE_READWRITE);
    if (!out->pcmData) { stb_vorbis_close(v); return FALSE; }

    short* pcm16 = (short*)out->pcmData;
    int decoded = stb_vorbis_get_samples_short_interleaved(v, channels, pcm16, totalSamples * channels);
    if (decoded < 0) decoded = 0;
    out->pcmSize = (DWORD)((uint64_t)decoded * channels * 2);

    stb_vorbis_close(v);
    return TRUE;
}

static const char* GetExtension(const char* path) {
    const char* dot = strrchr(path, '.');
    return dot ? dot : "";
}

BOOL DecodeAudioFile(const char* path, DecodedAudio* out) {
    if (!path || !out) return FALSE;
    memset(out, 0, sizeof(DecodedAudio));

    const char* ext = GetExtension(path);
    if (_stricmp(ext, ".ogg") == 0)
        return DecodeOgg(path, out);
    if (_stricmp(ext, ".wav") == 0)
        return DecodeWav(path, out);
    return FALSE;
}
