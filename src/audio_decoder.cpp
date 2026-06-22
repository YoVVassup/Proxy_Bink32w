#include "audio_decoder.h"
#include <stdint.h>
#include <string.h>

#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"

static BOOL DecodeWav(const char* path, DecodedAudio* out) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < 12) { CloseHandle(hFile); return FALSE; }

    char riffHdr[12];
    DWORD read;
    ReadFile(hFile, riffHdr, 12, &read, NULL);
    if (read != 12) { CloseHandle(hFile); return FALSE; }

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
        ReadFile(hFile, chunkId, 4, &read, NULL);
        ReadFile(hFile, &chunkSize, 4, &read, NULL);

        if (memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            char fmtData[16];
            ReadFile(hFile, fmtData, 16, &read, NULL);
            channels = (WORD)((unsigned char)fmtData[2] | ((unsigned char)fmtData[3] << 8));
            sampleRate = (unsigned char)fmtData[4] | ((unsigned char)fmtData[5] << 8) |
                         ((unsigned char)fmtData[6] << 16) | ((unsigned char)fmtData[7] << 24);
            bitsPerSample = (WORD)((unsigned char)fmtData[14] | ((unsigned char)fmtData[15] << 8));
            foundFmt = TRUE;
            if (chunkSize > 16)
                SetFilePointer(hFile, chunkSize - 16, NULL, FILE_CURRENT);
        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            foundData = TRUE;
            break;
        } else {
            SetFilePointer(hFile, chunkSize, NULL, FILE_CURRENT);
        }
    }

    if (!foundFmt || !foundData || channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
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

    ReadFile(hFile, out->pcmData, dataSize, &read, NULL);
    CloseHandle(hFile);

    out->pcmSize = read;
    return TRUE;
}

static BOOL DecodeOgg(const char* path, DecodedAudio* out) {
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &error, NULL);
    if (!v) return FALSE;

    stb_vorbis_info info = stb_vorbis_get_info(v);

    int totalSamples = stb_vorbis_stream_length_in_samples(v);
    int channels = info.channels;
    int sampleRate = info.sample_rate;

    out->format.wFormatTag = WAVE_FORMAT_PCM;
    out->format.nChannels = (WORD)channels;
    out->format.nSamplesPerSec = sampleRate;
    out->format.wBitsPerSample = 16;
    out->format.nBlockAlign = (WORD)(channels * 2);
    out->format.nAvgBytesPerSec = sampleRate * out->format.nBlockAlign;
    out->format.cbSize = 0;

    DWORD pcmBytes = totalSamples * channels * 2;
    out->pcmData = (char*)VirtualAlloc(NULL, pcmBytes, MEM_COMMIT, PAGE_READWRITE);
    if (!out->pcmData) { stb_vorbis_close(v); return FALSE; }

    short* pcm16 = (short*)out->pcmData;
    int decoded = stb_vorbis_get_samples_short_interleaved(v, channels, pcm16, totalSamples * channels);
    out->pcmSize = decoded * channels * 2;

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
    return DecodeWav(path, out);
}

void FreeDecodedAudio(DecodedAudio* audio) {
    if (audio && audio->pcmData) {
        VirtualFree(audio->pcmData, 0, MEM_RELEASE);
        audio->pcmData = NULL;
        audio->pcmSize = 0;
    }
}
