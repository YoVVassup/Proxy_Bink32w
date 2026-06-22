#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <mmsystem.h>
#include "video_scaler.h"

// ============================================================================
// Shared types
// ============================================================================

struct AudioMap {
    char bikName[MAX_PATH];
    char wavPath[MAX_PATH];
};

struct ExceptionEntry {
    char mixName[MAX_PATH];
    AudioMap maps[64];
    int mapCount;
};

struct BinkFileInfo {
    uint32_t width;
    uint32_t height;
    uint32_t frameCount;
    uint32_t frameRate;
    uint32_t frameRateDiv;
    BOOL valid;
};

struct MixEntry {
    uint32_t crc;
    uint32_t offset;
    uint32_t size;
};

struct MixArchive {
    char filePath[MAX_PATH];
    uint16_t fileCount;
    MixEntry entries[256];
    int valid;
};

struct WavPlayer {
    HWAVEOUT hWave;
    WAVEFORMATEX format;
    char* pcmData;
    DWORD pcmSize;
    DWORD pcmPos;
    volatile BOOL playing;
    volatile BOOL paused;
    int bufIndex;
    WAVEHDR headers[4];
    char* buffers[4];
    int bufSize;
    CRITICAL_SECTION cs;
};

struct VideoInfo {
    void* handle;
    uint32_t width;
    uint32_t height;
    void* tempBuf;
    int tempPitch;
    int tempHeight;
    char wavPath[MAX_PATH];
    WavPlayer* wavPlayer;
};

// ============================================================================
// Shared globals
// ============================================================================

extern HANDLE g_log;
extern BOOL g_logEnabled;
extern char g_dllDir[MAX_PATH];

extern AudioMap g_audioMaps[64];
extern int g_audioMapCount;
extern ExceptionEntry g_exceptions[32];
extern int g_exceptionCount;
extern BOOL g_audioConfigLoaded;
extern BOOL g_logWait;
extern ScaleMode g_scaleMode;

extern MixArchive g_mixCache[8];
extern int g_mixCacheCount;

#define MAX_WAV_PLAYERS 8
extern WavPlayer g_players[MAX_WAV_PLAYERS];
extern int g_playerCount;

#define MAX_TRACKED 32
extern VideoInfo g_vids[MAX_TRACKED];
extern int g_vidCount;

// ============================================================================
// Function declarations
// ============================================================================

// logging.cpp
void InitLog();
void Log(const char* msg);
void LogF(const char* fmt, ...);
void TrimRight(char* s);

inline uint32_t ReadU32(const void* p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline uint16_t ReadU16(const void* p) { uint16_t v; memcpy(&v, p, 2); return v; }

// config.cpp
void LoadAudioConfig();
BinkFileInfo ReadBinkHeaderFromFile(HANDLE hFile);
BinkFileInfo ReadBinkHeaderFromPath(const char* path);
uint32_t MixCrc32(const char* name);
MixArchive* ParseMixFile(const char* mixPath);
const char* FindBikNameInMix(const char* mixPath, DWORD filePos);
const char* FindWavForBik(const char* bikPath, const char* mixName);

// wav_player.cpp
WavPlayer* AllocPlayer();
void FreePlayer(WavPlayer* pl);
BOOL ParseWav(const char* path, WAVEFORMATEX* fmt, char** pcmOut, DWORD* pcmSizeOut);
BOOL WavPlayerStart(WavPlayer* pl, const char* wavPath);
void WavPlayerStop(WavPlayer* pl);
void WavPlayerPause(WavPlayer* pl);
void WavPlayerResume(WavPlayer* pl);
void WavPlayerSeek(WavPlayer* pl, DWORD sampleOffset);

// binkw32_proxy.cpp (video tracking + proxy exports)
void TrackVideo(void* h, const char* bikPath, const char* mixName);
void UntrackVideo(void* h);
VideoInfo* FindVideo(void* h);
void LogCallStack(int skip);
void ExtractFileName(void* a, DWORD flags, char* out, int outSize);
int BppFromFlags(int flags);
