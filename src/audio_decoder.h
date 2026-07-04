#pragma once
// ============================================================================
// audio_decoder.h — Unified audio decoder interface
//
// Provides DecodeAudioFile() for transparent WAV/OGG decoding.
// Both formats are decoded to interleaved PCM suitable for WaveOut playback.
// ============================================================================

#include <windows.h>
#include <mmsystem.h>

// DecodedAudio: holds decoded PCM data and format information.
// PCM buffer is freed by FreePlayer in wav_player.cpp.
struct DecodedAudio {
    char* pcmData;       // Interleaved PCM data
    DWORD pcmSize;       // Size in bytes
    WAVEFORMATEX format; // PCM format (sample rate, channels, bits)
};

// DecodeAudioFile: Decodes a WAV or OGG file to PCM.
// Returns TRUE on success, FALSE on failure.
BOOL DecodeAudioFile(const char* path, DecodedAudio* out);
