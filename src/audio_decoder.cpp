#include "audio_decoder.h"
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

extern void LogF(const char* fmt, ...);

#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

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
    DWORD dataPos = 0;
    BOOL foundFmt = FALSE;
    BOOL foundData = FALSE;

    for (;;) {
        LONG pos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
        if (pos < 0) break;
        if ((DWORD)pos > fileSize || fileSize - (DWORD)pos < 8) break;

        char chunkId[4];
        DWORD chunkSize;
        if (!ReadFile(hFile, chunkId, 4, &read, NULL) || read != 4) break;
        if (!ReadFile(hFile, &chunkSize, 4, &read, NULL) || read != 4) break;

        if (memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            char fmtData[16];
            if (!ReadFile(hFile, fmtData, 16, &read, NULL) || read != 16) break;
            WORD formatTag = (WORD)((unsigned char)fmtData[0] | ((unsigned char)fmtData[1] << 8));
            channels = (WORD)((unsigned char)fmtData[2] | ((unsigned char)fmtData[3] << 8));
            sampleRate = (unsigned char)fmtData[4] | ((unsigned char)fmtData[5] << 8) |
                         ((unsigned char)fmtData[6] << 16) | ((unsigned char)fmtData[7] << 24);
            bitsPerSample = (WORD)((unsigned char)fmtData[14] | ((unsigned char)fmtData[15] << 8));
            DWORD consumed = 16;
            if (formatTag == WAVE_FORMAT_EXTENSIBLE) {
                // WAVEFORMATEXTENSIBLE = base 16 + cbSize(2) + validBits(2) +
                // channelMask(4) + SubFormat GUID(16) = 40 bytes. Only PCM
                // sub-format is playable here (IEEE float, ADPCM, ... are not);
                // a shorter chunk cannot hold the GUID at all.
                if (chunkSize < 40) {
                    LogF("Audio decode rejected: truncated WAVE_FORMAT_EXTENSIBLE fmt "
                         "chunk (%u bytes) in %s", (unsigned)chunkSize, path);
                    CloseHandle(hFile); return FALSE;
                }
                char extData[24];
                if (!ReadFile(hFile, extData, 24, &read, NULL) || read != 24) break;
                consumed = 40;
                WORD cbSize = (WORD)((unsigned char)extData[0] | ((unsigned char)extData[1] << 8));
                // KSDATAFORMAT_SUBTYPE_PCM = {00000001-0000-0010-8000-00AA00389B71}
                static const unsigned char kPcmGuid[16] = {
                    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
                if (cbSize < 22 || memcmp(extData + 8, kPcmGuid, sizeof(kPcmGuid)) != 0) {
                    LogF("Audio decode rejected: non-PCM WAVE_FORMAT_EXTENSIBLE sub-format "
                         "in %s (only PCM is supported)", path);
                    CloseHandle(hFile); return FALSE;
                }
            } else if (formatTag != WAVE_FORMAT_PCM) {
                LogF("Audio decode rejected: format tag 0x%04X in %s "
                     "(only PCM and PCM WAVE_FORMAT_EXTENSIBLE are supported)",
                     (unsigned)formatTag, path);
                CloseHandle(hFile); return FALSE;
            }
            foundFmt = TRUE;
            // Skip the fmt extension *including* the RIFF pad byte of an
            // odd chunkSize, and bail out instead of mis-parsing a truncated one.
            DWORD skipFmt = chunkSize - consumed + (chunkSize & 1);
            LONG fmtPos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
            if (fmtPos < 0 || (DWORD)fmtPos > fileSize ||
                skipFmt > fileSize - (DWORD)fmtPos) {
                CloseHandle(hFile); return FALSE;
            }
            if (skipFmt) {
                // Bound-checked above, so the target cannot be
                // INVALID_FILE_POSITION (0xFFFFFFFF) — a negative result can
                // only mean the seek itself failed, and continuing would parse
                // from the wrong offset instead of rejecting the file.
                LONG after = SetFilePointer(hFile, skipFmt, NULL, FILE_CURRENT);
                if (after < 0 || (DWORD)after > fileSize) {
                    CloseHandle(hFile);
                    return FALSE;
                }
            }
            // A valid RIFF may list `data` before `fmt `; the payload position
            // is already saved, so stop as soon as both halves are known.
            if (foundData) break;
        } else if (memcmp(chunkId, "data", 4) == 0) {
            LONG payload = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
            if (payload < 0 || (DWORD)payload > fileSize) break;
            DWORD remaining = fileSize - (DWORD)payload;
            dataSize = chunkSize < remaining ? chunkSize : remaining;
            dataPos = (DWORD)payload;
            foundData = TRUE;
            if (foundFmt) break;
            // `fmt ` is still ahead: remember the payload and jump over it
            // instead of rejecting a correctly ordered file.
            DWORD skip = dataSize + (dataSize & 1);
            if (skip > fileSize - dataPos) break;
            if (skip) {
                LONG seeked = SetFilePointer(hFile, skip, NULL, FILE_CURRENT);
                if (seeked < 0) break;
            }
        } else {
            if (chunkSize > 0x7FFFFFFF) break;
            LONG before = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
            if (before < 0) break;
            DWORD skip = chunkSize + (chunkSize & 1);
            LONG after = SetFilePointer(hFile, skip, NULL, FILE_CURRENT);
            // A legal zero-length chunk skips 0 bytes (after == before);
            // Only a backwards seek means the file position is broken.
            if (after < 0 || after < before) break;
        }
    }

    if (!foundFmt || !foundData || channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
        CloseHandle(hFile); return FALSE;
    }

    // Garbage guard only — the real playable limits are enforced in
    // ValidatePlayable() so the log names the actual cause. This one still
    // has to say why it refused: a silent FALSE here hides a file the guard
    // rejected for a reason no caller can see.
    if (channels > 8 || (bitsPerSample != 8 && bitsPerSample != 16)) {
        LogF("Audio decode rejected: %u channels, %u bits in %s "
             "(WAV decoder limit: 8 channels, 8/16-bit)",
             (unsigned)channels, (unsigned)bitsPerSample, path);
        CloseHandle(hFile); return FALSE;
    }

    // waveOut consumes whole frames: drop a trailing partial frame and refuse
    // a payload that cannot hold even one.
    WORD blockAlign = (WORD)((channels * bitsPerSample) / 8);
    if (blockAlign == 0 || dataSize < blockAlign) { CloseHandle(hFile); return FALSE; }
    dataSize -= dataSize % blockAlign;

    // The payload may sit before the fmt chunk — reposition explicitly.
    LONG dataStart = SetFilePointer(hFile, dataPos, NULL, FILE_BEGIN);
    if (dataStart < 0) { CloseHandle(hFile); return FALSE; }

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
    stb_vorbis_close(v);

    // A zero-length decode must not be reported as success — the player
    // would queue an empty buffer, never receive WOM_DONE and pin the slot.
    if (decoded <= 0) {
        LogF("stb_vorbis: no samples decoded from %s", path);
        VirtualFree(out->pcmData, 0, MEM_RELEASE);
        out->pcmData = NULL;
        out->pcmSize = 0;
        return FALSE;
    }
    out->pcmSize = (DWORD)((uint64_t)decoded * channels * 2);
    return TRUE;
}

// Reject formats waveOut cannot open (or that would overflow its
// per-second arithmetic) with a message that names the real cause, instead of
// letting them surface as a bare WAVERR_BADFORMAT.
static BOOL ValidatePlayable(const char* path, DecodedAudio* out) {
    unsigned ch = out->format.nChannels;
    unsigned rate = out->format.nSamplesPerSec;

    if (ch < 1 || ch > 2) {
        LogF("Audio rejected: %u channels in %s (waveOut supports mono/stereo only); "
             "the original Bink audio will be used", ch, path);
    } else if (rate < 1000 || rate > 192000) {
        // Upper bound keeps nAvgBytesPerSec = rate * nBlockAlign inside DWORD
        // (max 192000 * 4 = 768000) and keeps bufSize sane.
        LogF("Audio rejected: %u Hz in %s (supported range 1000..192000 Hz); "
             "the original Bink audio will be used", rate, path);
    } else if (out->format.nBlockAlign == 0 || out->format.nAvgBytesPerSec == 0) {
        LogF("Audio rejected: inconsistent format in %s (rate=%u align=%u avg=%u); "
             "the original Bink audio will be used", path, rate,
             (unsigned)out->format.nBlockAlign,
             (unsigned)out->format.nAvgBytesPerSec);
    } else {
        return TRUE;
    }

    if (out->pcmData) VirtualFree(out->pcmData, 0, MEM_RELEASE);
    memset(out, 0, sizeof(DecodedAudio));
    return FALSE;
}

// Extension of the file *name*: a dot inside a directory component
// (C:\v1.2\track) must not be mistaken for an extension.
static const char* GetExtension(const char* path) {
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    const char* dot = strrchr(base, '.');
    return dot ? dot : "";
}

BOOL DecodeAudioFile(const char* path, DecodedAudio* out) {
    if (!path || !out) return FALSE;
    memset(out, 0, sizeof(DecodedAudio));

    const char* ext = GetExtension(path);
    BOOL ok;
    if (_stricmp(ext, ".ogg") == 0)      ok = DecodeOgg(path, out);
    else if (_stricmp(ext, ".wav") == 0) ok = DecodeWav(path, out);
    else return FALSE;
    // FALSE must mean "empty": callers free out->pcmData only when it is set.
    if (!ok) { memset(out, 0, sizeof(DecodedAudio)); return FALSE; }

    return ValidatePlayable(path, out);
}
