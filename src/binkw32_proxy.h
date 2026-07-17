#pragma once
// ============================================================================
// binkw32_proxy.h — Shared types, globals, and function declarations
//
// Central header for the Proxy_Bink32w project. Defines all shared data
// structures (AudioMap, ExceptionEntry, BinkFileInfo, MixArchive, WavPlayer,
// VideoInfo), extern globals, and function prototypes used across modules.
//
// Modules:
//   binkw32_proxy.cpp — DLL loader, video tracking, proxy exports
//   logging.cpp       — Log subsystem with rotation
//   config.cpp        — Config parsing, .mix archive parser, Bink header reader
//   audio_decoder.cpp — Unified WAV + OGG decoder (stb_vorbis)
//   wav_player.cpp    — WaveOut audio playback
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <mmsystem.h>

// ============================================================================
// Shared types
// ============================================================================

// AudioMap: maps a .bik filename to its .wav/.ogg replacement path.
// Used in [audio] section and [exception] sub-sections of binkw32.cfg.
struct AudioMap {
    char bikName[MAX_PATH];
    char wavPath[MAX_PATH];
};

// ExceptionEntry: groups audio maps by .mix archive name.
// Allows per-mix audio replacement rules in [exception] config section.
struct ExceptionEntry {
    char mixName[MAX_PATH];
    AudioMap maps[64];
    int mapCount;
};

// BinkFileInfo: parsed header from a .bik file.
// Used to detect video dimensions before BinkOpen completes.
struct BinkFileInfo {
    uint32_t width;
    uint32_t height;
    uint32_t frameCount;
    uint32_t frameRate;
    uint32_t frameRateDiv;
    BOOL valid;
};

// MixEntry: single entry in a .mix archive hash table.
// Stores CRC32 hash, file offset, and file size.
struct MixEntry {
    uint32_t crc;
    uint32_t offset;
    uint32_t size;
};

// MixArchive: parsed .mix archive with up to 256 file entries.
// Cached in g_mixCache[8] to avoid re-parsing the same archive.
struct MixArchive {
    char filePath[MAX_PATH];
    uint16_t fileCount;
    MixEntry entries[256];
    int valid;
};

// WavPlayer: WaveOut-based audio player with 4-buffer streaming.
// Uses CRITICAL_SECTION for thread-safe callback handling.
// Supports play, pause, resume, and seek operations.
struct WavPlayer {
    HWAVEOUT hWave;
    WAVEFORMATEX format;
    char* pcmData;
    DWORD pcmSize;
    DWORD pcmPos;
    volatile BOOL playing;
    volatile BOOL paused;
    WAVEHDR headers[4];
    char* buffers[4];
    int bufSize;
    int preparedCount;
    BOOL csValid;
    CRITICAL_SECTION cs;
};

// VideoInfo: tracks an active Bink video handle.
// Stores dimensions, temp buffer for scaling, and associated WavPlayer.
struct VideoInfo {
    void* handle;
    uint32_t width;
    uint32_t height;
    void* tempBuf;
    int tempPitch;
    int tempHeight;
    int* scaleLookupX;
    int* scaleLookupY;
    int scaleTableW;
    int scaleTableH;
    char wavPath[MAX_PATH];
    WavPlayer* wavPlayer;
    bool wavStarted;
};

// ============================================================================
// Shared globals
// ============================================================================

extern HANDLE g_log;           // Log file handle (INVALID_HANDLE_VALUE when closed)
extern BOOL g_logEnabled;      // Master switch for logging
extern char g_dllDir[MAX_PATH]; // Directory where this DLL resides

extern AudioMap g_audioMaps[64];      // Global audio replacements from [audio]
extern int g_audioMapCount;
extern ExceptionEntry g_exceptions[32]; // Per-mix exceptions from [exception]
extern int g_exceptionCount;
extern BOOL g_logWait;          // Log BinkWait calls (from [log] wait=true)

extern MixArchive g_mixCache[8];   // Parsed .mix archive cache
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

// --- logging.cpp ---
// InitLog: Creates or rotates the log file on startup.
// Log/LogF: Thread-safe logging with rotation check (10MB max).
// TrimRight: Removes trailing whitespace from a string.
void InitLog();
void Log(const char* msg);
void LogF(const char* fmt, ...);
void TrimRight(char* s);
void ShutdownLog();

static inline uint32_t ReadU32(const void* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t ReadU16(const void* p) { uint16_t v; memcpy(&v, p, 2); return v; }

// --- config.cpp ---
// LoadAudioConfig: Parses binkw32.cfg (single-pass, in-memory).
// ReadBinkHeader*: Reads Bink video header from file handle or path.
// MixCrc32: Computes CRC32 for .mix filename (RA2 convention).
// ParseMixFile: Parses .mix archive header and LMD (cached).
// FindBikNameInMix: Resolves .bik filename from .mix by file position.
// FindWavForBik: Looks up .wav/.ogg replacement for a .bik file.
void LoadAudioConfig();
void ResetAudioConfig();
BinkFileInfo ReadBinkHeaderFromFile(HANDLE hFile);
BinkFileInfo ReadBinkHeaderFromPath(const char* path);
uint32_t MixCrc32(const char* name);
MixArchive* ParseMixFile(const char* mixPath);
BOOL FindBikNameInMix(const char* mixPath, DWORD filePos, char* outName, int outNameSize);
const char* FindWavForBik(const char* bikPath, const char* mixName);

// --- wav_player.cpp ---
// WavPlayer lifecycle: AllocPlayer -> WavPlayerStart -> WavPlayerStop -> FreePlayer.
// WavPlayerPause/Resume/WavPlayerSeek: Playback control operations.
WavPlayer* AllocPlayer();
void FreePlayer(WavPlayer* pl);
BOOL WavPlayerStart(WavPlayer* pl, const char* wavPath);
void WavPlayerStop(WavPlayer* pl);
void WavPlayerPause(WavPlayer* pl);
void WavPlayerResume(WavPlayer* pl);
void WavPlayerSeek(WavPlayer* pl, DWORD sampleOffset);

// --- binkw32_proxy.cpp ---
// TrackVideo/UntrackVideo/FindVideo: Video handle tracking and audio replacement.
// LogCallStack: Logs call stack with module+RVA for debugging.
// ExtractFileName: Extracts filename from BinkOpen parameters.
// BppFromFlags: Extracts bits-per-pixel from BinkCopyToBuffer flags (RA2 uses bpp=2).
void TrackVideo(void* h, const char* bikPath, const char* mixName);
void UntrackVideo(void* h);
VideoInfo* FindVideo(void* h);
void LogCallStack(int skip);
int BppFromFlags(int flags);
BOOL ExtractNameFromCCFileClass(void* ccFile, char* out, int outSize);
