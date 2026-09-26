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

// Config capacity limits (see "Known limitations" in README).
#define MAX_AUDIO_MAPS      256 // max entries in global [audio] section
#define MAX_EXCEPTION_MIXES 64  // max entries in [exception] (mix list)
#define MAX_MAPS_PER_MIX    256 // max explicit maps per per-mix section

// ExceptionEntry: groups audio maps by .mix archive name.
// Allows per-mix audio replacement rules in [exception] config section.
// baseDir: optional relative directory for auto .ogg/.wav lookup
//   (bik stem + .ogg first, then .wav). Empty = no auto-resolve.
// Syntax in [exception]: mixName  |  mixName|baseDir
struct ExceptionEntry {
    char mixName[MAX_PATH];
    char baseDir[MAX_PATH];
    AudioMap maps[MAX_MAPS_PER_MIX];
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
// Stores CRC32 hash, file offset, file size, and LMD-resolved filename.
struct MixEntry {
    uint32_t crc;
    uint32_t offset;
    uint32_t size;
    char name[128];
};

// MixArchive: parsed .mix archive. entries is heap-allocated (fileCount up to 65535).
// Cached in g_mixCache[8] to avoid re-parsing the same archive.
// bodyOffset: absolute file offset of entry data (after index / encrypted header).
struct MixArchive {
    char filePath[MAX_PATH];
    uint16_t fileCount;
    uint32_t bodyOffset;
    int encrypted;
    MixEntry* entries; // malloc'd, fileCount items; free on slot reuse
    int valid;
};

#define MIX_MAX_FILES 65535u

// WavPlayer: WaveOut-based audio player with 4-buffer streaming.
// Uses CRITICAL_SECTION for thread-safe callback handling.
// Supports play, pause, resume, and seek operations.
//
// Two locks, strict order devCs -> cs (never the reverse, never nested the
// other way):
//   devCs serializes the *external* waveOut* calls (Pause/Resume/Seek reset,
//        Stop/Free reset+close). It closes the close-after-snapshot race:
//        without it WavPlayerPause could snapshot `hWave` under `cs`, a
//        concurrent Stop/Free could close the device, and the subsequent
//        waveOutPause would hit a recycled handle owned by another player.
//   cs protects the payload (pcm*, playing, paused, headers, pending).
// WaveOutProc/RefillHeader take cs ONLY — they may run while a thread holds
// devCs across waveOutReset, and devCs is never taken from the callback
// (that would deadlock reset against the callback it is waiting for).
//
// Lifetime of `cs`/`devCs`: initialized once per pool slot and NEVER deleted.
// A WOM_DONE callback may still be dispatched after waveOutReset()/waveOutClose()
// returns; entering a deleted critical section would be undefined behaviour.
// Callbacks are made harmless instead: FreePlayer/WavPlayerStop clear `playing`
// under the lock first, and the callback only touches payload when it is set.
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
    WAVEHDR* pending[4]; // headers returned by WOM_DONE while paused
    int pendingCount;
    int bufSize;
    int preparedCount;
    BOOL csValid;   // `cs`/`devCs` have been initialized (permanent)
    BOOL inUse;     // slot currently handed out to a video
    CRITICAL_SECTION cs;
    CRITICAL_SECTION devCs;
};

// ScaleBufs: refcounted scratch buffers for the two-pass scaled blit
// (Bink renders into tempBuf at source size, the proxy scales down into the
// game's buffer). The video slot owns one reference; sBinkCopyToBuffer takes
// an extra one before dropping the track lock, so an in-flight blit survives
// UntrackVideo compacting g_vids[] underneath it. Every field below is
// guarded by `cs` — the track lock is NOT held during the blit (it would
// block every other sBink* entry point on the I/O). A thread may only enter
// `cs` while it holds a reference, so the last Unref can safely destroy it.
struct ScaleBufs {
    volatile LONG refs;
    CRITICAL_SECTION cs;
    void* tempBuf;
    int tempPitch;
    int tempHeight;
    int* lookupX;
    int* lookupY;
    int tableW;
    int tableH;
};

// VideoInfo: tracks an active Bink video handle.
// Index of a YUV blit in the proxy's g_yuvArity[] table. The real DLL
// decorates these blitters anywhere between @36 and @60 by generation, so the
// arity of the real export is probed at load and the forward adapts to it.
enum YuvArityId {
    YUV_A_16a1bpp, YUV_A_16a1bpp_mask, YUV_A_16a4bpp, YUV_A_16a4bpp_mask,
    YUV_A_16bpp, YUV_A_16bpp_mask, YUV_A_24bpp, YUV_A_24bpp_mask,
    YUV_A_24rbpp, YUV_A_24rbpp_mask, YUV_A_32abpp, YUV_A_32abpp_mask,
    YUV_A_32bpp, YUV_A_32bpp_mask, YUV_A_32rabpp, YUV_A_32rabpp_mask,
    YUV_A_32rbpp, YUV_A_32rbpp_mask, YUV_A_UYVY, YUV_A_UYVY_mask,
    YUV_A_YUY2, YUV_A_YUY2_mask, YUV_A_YV12,
    YUV_ARITY_COUNT
};

// Stores dimensions, refcounted scale scratch (ScaleBufs), and associated WavPlayer.
//
// Audio replacement state machine (see sBinkDoFrame / ServiceVideoAudio):
//   wavPath set, !wavStarted, !wavFailed -> starting/retrying the player
//   wavStarted                           -> replacement owns the audio
//   wavFailed                            -> gave up; original Bink audio restored
// Muting (SetVolume/SetSoundOnOff) keys off `wavPath && !wavFailed`, so a
// failed replacement can never leave the movie silent.
struct VideoInfo {
    void* handle;
    uint32_t width;
    uint32_t height;
    ScaleBufs* scale;      // scale scratch, owned by this slot (refs >= 1)
    char wavPath[MAX_PATH];
    WavPlayer* wavPlayer;
    bool wavStarted;
    bool wavFailed;       // replacement abandoned -> original audio plays
    int  wavAttempts;     // failed WavPlayerStart calls so far
    bool pauseRequested;  // BinkPause(1) arrived before the player existed
    bool seekPending;     // BinkGoto happened before the player existed
    DWORD seekFrame;      // ...target Bink frame (valid if seekPending)
    DWORD seekFr;         // ...frame rate numerator from BinkGetSummary
    DWORD seekFrD;        // ...frame rate denominator
    BOOL soundOn;         // game's last BinkSetSoundOnOff request (default on)
    BOOL soundReqSet;     // the game has called BinkSetSoundOnOff at least once
    BOOL volumeSet;       // game asked for a volume at least once
    void* volumeTrack;    // ...the track id it was for (NULL if unknown)
    void* volumeValue;    // ...and the value it asked for (restore on failure)
    BOOL panSet;
    void* panValue0;
    void* panValue1;
};

// ============================================================================
// Shared globals
// ============================================================================

extern HANDLE g_log;           // Log file handle (INVALID_HANDLE_VALUE when closed)
extern BOOL g_logEnabled;      // Master switch for logging
extern char g_dllDir[MAX_PATH]; // Directory where this DLL resides

extern AudioMap g_audioMaps[MAX_AUDIO_MAPS]; // Global audio replacements from [audio]
extern int g_audioMapCount;
extern ExceptionEntry g_exceptions[MAX_EXCEPTION_MIXES]; // Per-mix exceptions from [exception]
extern int g_exceptionCount;
extern BOOL g_logWait;          // Log frequent calls (BinkWait/DoFrame/RADTimerRead/allocator) (from [log] wait=true)

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
// WarnTruncate: logs when a `_TRUNCATE` copy is about to drop characters
// (shared by config.cpp, wav_player.cpp and the proxy's path building).
void WarnTruncate(const char* what, const char* value, size_t cap);
void TrimRight(char* s);
void ShutdownLog();

static inline uint32_t ReadU32(const void* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t ReadU16(const void* p) { uint16_t v; memcpy(&v, p, 2); return v; }

// --- config.cpp ---
// LoadAudioConfig: Parses binkw32.cfg (single-pass, in-memory).
// ReadBinkHeader*: Reads Bink video header from file handle or path.
// MixCrc32: Computes CRC32 for .mix filename (RA2 convention).
// ParseMixFile: Parses .mix archive header and LMD (cached); decrypts flags&2.
// FindBikNameInMix: Resolves .bik filename from .mix by file position.
// FindWavForBik: Looks up .wav/.ogg replacement for a .bik file.
void LoadAudioConfig();
void ResetAudioConfig();
void FreeMixCache();
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
// ScaleBufs lifecycle: Create (refs = 1, owner takes it) -> Ref/Unref.
// The last Unref frees the buffers and the object itself.
ScaleBufs* ScaleBufsCreate(void);
void ScaleBufsRef(ScaleBufs* sb);
void ScaleBufsUnref(ScaleBufs* sb);
// FindVideoLocked: returns entry with track lock HELD (caller must TrackUnlock),
// or NULL when not tracked (no lock held). Use whenever the result is dereferenced.
VideoInfo* FindVideoLocked(void* h);
#ifdef BINK_TEST_BUILD
// test-only. Returns a raw pointer after releasing the track lock, so it
// goes stale as soon as UntrackVideo() compacts the array. The proxy itself
// only ever uses FindVideoLocked(); keeping this out of release builds removes
// the footgun rather than papering over it.
VideoInfo* FindVideo(void* h);
#endif
void TrackLock();
void TrackUnlock();
void LogCallStack(int skip);
int BppFromFlags(int flags);
BOOL ExtractNameFromCCFileClass(void* ccFile, char* out, int outSize);
#ifdef BINK_TEST_BUILD
// test-only. Resolves IHCore's ExtBink_GetCurrentBikName getter, trying the
// MSVC stdcall-decorated name first ("_ExtBink_GetCurrentBikName@0").
FARPROC ResolveExtBikNameGetter(HMODULE mod);
#endif
