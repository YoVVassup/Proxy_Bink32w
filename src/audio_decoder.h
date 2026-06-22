#pragma once
#include <windows.h>
#include <mmsystem.h>

struct DecodedAudio {
    char* pcmData;
    DWORD pcmSize;
    WAVEFORMATEX format;
};

BOOL DecodeAudioFile(const char* path, DecodedAudio* out);
void FreeDecodedAudio(DecodedAudio* audio);
