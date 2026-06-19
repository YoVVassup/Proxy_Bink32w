#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// Proxy_Bink32w v1.1.0 — Bink Video API Proxy DLL
//
// Drop-in binkw32.dll replacement that intercepts Bink video API calls.
// Features: audio replacement (.bik -> .wav), .mix archive parsing,
//           aspect-ratio fit scaling, call stack logging.
// ============================================================================

// Handle to the real Bink DLL loaded at runtime
static HMODULE g_hR = NULL;

// Function pointers to real Bink DLL functions, resolved by ordinal at load time
#define D(n) static void* p##n = NULL;
D(BinkLogoAddress) D(BinkSetError) D(BinkGetError) D(BinkOpen)
D(BinkOpenWithOptions) D(BinkDoFrame) D(BinkDoFramePlane) D(BinkNextFrame)
D(BinkWait) D(BinkClose) D(BinkPause) D(BinkCopyToBuffer)
D(BinkCopyToBufferRect) D(BinkGetRects) D(BinkGoto) D(BinkGetKeyFrame)
D(BinkFreeGlobals) D(BinkGetPlatformInfo)
D(BinkGetFrameBuffersInfo) D(BinkRegisterFrameBuffers)
D(BinkSetVideoOnOff) D(BinkSetSoundOnOff)
D(BinkSetVolume) D(BinkSetPan) D(BinkSetSpeakerVolumes)
D(BinkService) D(BinkShouldSkip) D(BinkGetPalette)
D(BinkControlBackgroundIO) D(BinkControlPlatformFeatures)
D(BinkSetWillLoop) D(BinkOpenTrack) D(BinkCloseTrack)
D(BinkGetTrackData) D(BinkGetTrackType)
D(BinkGetTrackMaxSize) D(BinkGetTrackID)
D(BinkGetSummary) D(BinkGetRealtime)
D(BinkSetFileOffset) D(BinkSetSoundTrack8)
D(BinkSetIO) D(BinkSetFrameRate) D(BinkSetSimulate)
D(BinkSetIOSize) D(BinkSetSoundSystem) D(BinkSetMemory)
D(BinkOpenDirectSound) D(BinkOpenWaveOut) D(BinkOpenMiles)
D(BinkDX8SurfaceType) D(BinkDX9SurfaceType)
D(BinkBufferOpen) D(BinkBufferSetHWND)
D(BinkDDSurfaceType) D(BinkIsSoftwareCursor)
D(BinkCheckCursor) D(BinkBufferSetDirectDraw)
D(BinkBufferClose) D(BinkBufferLock) D(BinkBufferUnlock)
D(BinkBufferSetResolution) D(BinkBufferCheckWinPos)
D(BinkBufferSetOffset) D(BinkBufferBlit) D(BinkBufferSetScale)
D(BinkBufferGetDescription) D(BinkBufferGetError) D(BinkBufferClear)
D(BinkRestoreCursor) D(BinkStartAsyncThread)
D(BinkDoFrameAsync) D(BinkDoFrameAsyncWait)
D(BinkRequestStopAsyncThread) D(BinkWaitStopAsyncThread)
D(BinkSetMixBins) D(BinkSetMixBinVolumes)
D(ExpandBink) D(ExpandBundleSizes) D(RADSetMemory) D(RADTimerRead)
D(radmalloc) D(radfree)
D(YUV_init)
D(YUV_blit_16a1bpp) D(YUV_blit_16a1bpp_mask)
D(YUV_blit_16a4bpp) D(YUV_blit_16a4bpp_mask)
D(YUV_blit_16bpp) D(YUV_blit_16bpp_mask)
D(YUV_blit_24bpp) D(YUV_blit_24bpp_mask)
D(YUV_blit_24rbpp) D(YUV_blit_24rbpp_mask)
D(YUV_blit_32abpp) D(YUV_blit_32abpp_mask)
D(YUV_blit_32bpp) D(YUV_blit_32bpp_mask)
D(YUV_blit_32rabpp) D(YUV_blit_32rabpp_mask)
D(YUV_blit_32rbpp) D(YUV_blit_32rbpp_mask)
D(YUV_blit_UYVY) D(YUV_blit_UYVY_mask)
D(YUV_blit_YUY2) D(YUV_blit_YUY2_mask)
D(YUV_blit_YV12)
#undef D

// ============================================================================
// Logging subsystem
// ============================================================================

static HANDLE g_log = INVALID_HANDLE_VALUE;
static BOOL g_logEnabled = TRUE;
static char g_dllDir[MAX_PATH] = {0}; // Directory where this DLL resides

// Initialize log file. Priority: BINK_PROXY_LOG env var > binkw32.nolog file > default log
static void InitLog() {
    char env[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("BINK_PROXY_LOG", env, MAX_PATH)) {
        if (!env[0] || lstrcmpA(env, "0") == 0) { g_logEnabled = FALSE; return; }
        g_log = CreateFileA(env, GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        return;
    }
    char nologPath[MAX_PATH];
    _snprintf_s(nologPath, sizeof(nologPath), _TRUNCATE, "%sbinkw32.nolog", g_dllDir);
    if (GetFileAttributesA(nologPath) != INVALID_FILE_ATTRIBUTES) { g_logEnabled = FALSE; return; }
    char logPath[MAX_PATH];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", g_dllDir);
    g_log = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

// Write a message to the log file with auto-flush
static void Log(const char* msg) {
    if (!g_logEnabled) return;
    if (g_log == INVALID_HANDLE_VALUE) InitLog();
    if (g_log != INVALID_HANDLE_VALUE) {
        static BOOL logged_header = FALSE;
        if (!logged_header) {
            DWORD bw;
            const char* header =
                "=== Proxy_Bink32w v1.1.0 ===\r\n"
#ifdef BINK_10Q
                "Target: Bink 1.0q\r\n"
#else
                "Target: Bink 1.9u\r\n"
#endif
                "\r\n";
            WriteFile(g_log, header, (DWORD)strlen(header), &bw, NULL);
            logged_header = TRUE;
        }
        DWORD bw;
        SetFilePointer(g_log, 0, NULL, FILE_END);
        WriteFile(g_log, msg, (DWORD)strlen(msg), &bw, NULL);
        WriteFile(g_log, "\r\n", 2, &bw, NULL);
        FlushFileBuffers(g_log);
    }
}

static void LogF(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

static void TrimRight(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

static inline uint32_t ReadU32(const void* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t ReadU16(const void* p) { uint16_t v; memcpy(&v, p, 2); return v; }

// ============================================================================
// Audio replacement configuration
//
// Config file format (binkw32.cfg):
//   [exception]      — list of .mix files with per-mix audio rules
//   [mixname]        — per-mix .bik -> .wav mappings (higher priority)
//   [audio]          — global .bik -> .wav fallback
//   [log]            — logging options (wait=true to log BinkWait)
//
// Priority: [exception] > [audio]
// Reserved section names: audio, exception, log (cannot be .mix names)
// ============================================================================

static const int MAX_AUDIO_MAPS = 64;
static const int MAX_EXCEPTIONS = 32;

struct AudioMap {
    char bikName[MAX_PATH]; // .bik filename (e.g. "s01_f00e.bik")
    char wavPath[MAX_PATH]; // .wav replacement path
};

struct ExceptionEntry {
    char mixName[MAX_PATH]; // .mix archive name (e.g. "movies01.mix")
    AudioMap maps[MAX_AUDIO_MAPS];
    int mapCount;
};

static AudioMap g_audioMaps[MAX_AUDIO_MAPS];
static int g_audioMapCount = 0;
static ExceptionEntry g_exceptions[MAX_EXCEPTIONS];
static int g_exceptionCount = 0;
static BOOL g_audioConfigLoaded = FALSE;
static BOOL g_logWait = FALSE;

// Parse binkw32.cfg: [log], [exception], [mixname], [audio] sections
static void LoadAudioConfig() {
    if (g_audioConfigLoaded) return;
    g_audioConfigLoaded = TRUE;

    char cfgPath[MAX_PATH];
    _snprintf_s(cfgPath, sizeof(cfgPath), _TRUNCATE, "%sbinkw32.cfg", g_dllDir);

    FILE* f = NULL;
    fopen_s(&f, cfgPath, "r");
    if (!f) return;

    BOOL inAudioSection = FALSE;
    BOOL inExceptionSection = FALSE;
    BOOL inExceptionMix = FALSE;
    int currentExceptionIdx = -1;
    char sectionName[64] = "";
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        TrimRight(line);
        if (line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (close) *close = '\0';
            strncpy_s(sectionName, sizeof(sectionName), line + 1, _TRUNCATE);
            inAudioSection = (_stricmp(sectionName, "audio") == 0);
            inExceptionSection = (_stricmp(sectionName, "exception") == 0);
            inExceptionMix = FALSE;
            currentExceptionIdx = -1;

            if (!inAudioSection && !inExceptionSection && sectionName[0]) {
                BOOL reserved = (_stricmp(sectionName, "audio") == 0 ||
                                 _stricmp(sectionName, "exception") == 0 ||
                                 _stricmp(sectionName, "log") == 0);
                if (!reserved) {
                    char sectionWithMix[MAX_PATH];
                    _snprintf_s(sectionWithMix, sizeof(sectionWithMix), _TRUNCATE, "%s.mix", sectionName);
                    for (int i = 0; i < g_exceptionCount; i++) {
                        if (_stricmp(g_exceptions[i].mixName, sectionName) == 0 ||
                            _stricmp(g_exceptions[i].mixName, sectionWithMix) == 0) {
                            inExceptionMix = TRUE;
                            currentExceptionIdx = i;
                            break;
                        }
                    }
                }
            }
            continue;
        }
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        TrimRight(k);
        while (*v == ' ' || *v == '\t') v++;
        TrimRight(v);

        if (_stricmp(sectionName, "log") == 0) {
            if (_stricmp(k, "wait") == 0) g_logWait = (_stricmp(v, "true") == 0 || _stricmp(v, "1") == 0);
        }

        if (inExceptionSection && g_exceptionCount < MAX_EXCEPTIONS) {
            if (!k[0] || !v[0]) continue;
            strncpy_s(g_exceptions[g_exceptionCount].mixName,
                       sizeof(g_exceptions[g_exceptionCount].mixName), v, _TRUNCATE);
            g_exceptions[g_exceptionCount].mapCount = 0;
            LogF("  Exception: %s", v);
            g_exceptionCount++;
        }

        if (inExceptionMix && currentExceptionIdx >= 0) {
            if (!k[0] || !v[0]) continue;
            ExceptionEntry* ex = &g_exceptions[currentExceptionIdx];
            if (ex->mapCount < MAX_AUDIO_MAPS) {
                strncpy_s(ex->maps[ex->mapCount].bikName, sizeof(ex->maps[ex->mapCount].bikName), k, _TRUNCATE);
                strncpy_s(ex->maps[ex->mapCount].wavPath, sizeof(ex->maps[ex->mapCount].wavPath), v, _TRUNCATE);
                LogF("  Exception map [%s]: %s -> %s", ex->mixName, k, v);
                ex->mapCount++;
            }
        }

        if (inAudioSection && g_audioMapCount < MAX_AUDIO_MAPS) {
            if (!k[0] || !v[0]) continue;
            strncpy_s(g_audioMaps[g_audioMapCount].bikName, sizeof(g_audioMaps[g_audioMapCount].bikName), k, _TRUNCATE);
            strncpy_s(g_audioMaps[g_audioMapCount].wavPath, sizeof(g_audioMaps[g_audioMapCount].wavPath), v, _TRUNCATE);
            LogF("  Audio map: %s -> %s", k, v);
            g_audioMapCount++;
        }
    }
    fclose(f);
    LogF("Config loaded: %d audio maps, %d exceptions, log_wait=%d from %s",
         g_audioMapCount, g_exceptionCount, g_logWait, cfgPath);
}

// ============================================================================
// Bink file header reader
// ============================================================================

struct BinkFileInfo {
    uint32_t width;
    uint32_t height;
    uint32_t frameCount;
    uint32_t frameRate;
    uint32_t frameRateDiv;
    BOOL valid;
};

// Read Bink file header from an open file handle.
// Saves/restores file position. Returns valid BinkFileInfo if marker matches.
static BinkFileInfo ReadBinkHeaderFromFile(HANDLE hFile) {
    BinkFileInfo info = {0};
    DWORD origPos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
    char hdr[44];
    DWORD read;
    if (!ReadFile(hFile, hdr, sizeof(hdr), &read, NULL) || read < sizeof(hdr)) {
        return info;
    }
    SetFilePointer(hFile, origPos, NULL, FILE_BEGIN);

    uint32_t marker = ReadU32(hdr);
    if (marker != 0x42494B66 && marker != 0x42494B67 &&
        marker != 0x42494B68 && marker != 0x42494B69) {
        return info;
    }
    info.width = ReadU32(hdr + 20);
    info.height = ReadU32(hdr + 24);
    info.frameCount = ReadU32(hdr + 8);
    info.frameRate = ReadU32(hdr + 28);
    info.frameRateDiv = ReadU32(hdr + 32);
    info.valid = (info.width > 0 && info.height > 0);
    return info;
}

// Read Bink file header from a file path (opens, reads, closes)
static BinkFileInfo ReadBinkHeaderFromPath(const char* path) {
    BinkFileInfo info = {0};
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return info;
    info = ReadBinkHeaderFromFile(hFile);
    CloseHandle(hFile);
    return info;
}

// ============================================================================
// .mix archive parser + LMD (Local Mix Database) resolver
//
// RA2/YR .mix format:
//   Offset 0:  4 bytes reserved (0)
//   Offset 4:  uint16 file count
//   Offset 6:  4 bytes reserved
//   Offset 0xA: hash table (12 bytes/entry: CRC32 + offset + size)
//   After hash table: file data
//
// LMD file (CRC32 0x366E051F) contains CRC32 -> filename mappings.
// CRC32 is computed with RA2 convention: uppercase + padding to 4-byte alignment.
// ============================================================================

static uint32_t Crc32_Compute(const void* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;
    for (int i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
    }
    return ~crc;
}

// Compute CRC32 for .mix filename (uppercase + 4-byte padding per RA2 convention)
static uint32_t MixCrc32(const char* name) {
    char upper[256];
    int len = (int)strlen(name);
    if (len >= 256) len = 255;
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        upper[i] = c;
    }
    upper[len] = '\0';

    if (len & 3) {
        int mask = len & ~3;
        char pad = upper[mask];
        int padCount = 3 - (len & 3);
        for (int i = 0; i < padCount; i++)
            upper[len + i] = pad;
        upper[len + padCount] = '\0';
        len += padCount;
    }

    return Crc32_Compute(upper, len);
}

struct MixEntry {
    uint32_t crc;
    uint32_t offset;
    uint32_t size;
};

#define MAX_MIX_ENTRIES 256

struct MixArchive {
    char filePath[MAX_PATH];
    uint16_t fileCount;
    MixEntry entries[MAX_MIX_ENTRIES];
    int valid;
};

static MixArchive g_mixCache[8];
static int g_mixCacheCount = 0;

// Parse a .mix archive: read header, hash table, resolve LMD filenames.
// Results are cached in g_mixCache[8].
static MixArchive* ParseMixFile(const char* mixPath) {
    for (int i = 0; i < g_mixCacheCount; i++) {
        if (_stricmp(g_mixCache[i].filePath, mixPath) == 0)
            return &g_mixCache[i];
    }
    if (g_mixCacheCount >= 8) return NULL;

    LogF("ParseMixFile: opening %s", mixPath);

    HANDLE hFile = CreateFileA(mixPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogF("ParseMixFile: FAILED to open (error %lu)", GetLastError());
        return NULL;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    LogF("ParseMixFile: file size=%u", fileSize);
    if (fileSize < 14) { CloseHandle(hFile); return NULL; }

    uint8_t hdr[14];
    DWORD read;
    if (!ReadFile(hFile, hdr, 14, &read, NULL) || read < 14) {
        CloseHandle(hFile); return NULL;
    }

    LogF("ParseMixFile: header bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
         hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7],
         hdr[8], hdr[9], hdr[10], hdr[11], hdr[12], hdr[13]);

    uint16_t fileCount = ReadU16(hdr + 4);
    LogF("ParseMixFile: fileCount=%u", fileCount);
    if (fileCount == 0 || fileCount > MAX_MIX_ENTRIES) {
        LogF("ParseMixFile: invalid fileCount, aborting");
        CloseHandle(hFile); return NULL;
    }

    uint32_t hashTableSize = fileCount * 12;
    uint32_t hashTableOffset = 0xA;
    if (hashTableOffset + hashTableSize > fileSize) {
        LogF("ParseMixFile: hash table extends past EOF");
        CloseHandle(hFile); return NULL;
    }

    SetFilePointer(hFile, hashTableOffset, NULL, FILE_BEGIN);

    MixArchive* mix = &g_mixCache[g_mixCacheCount];
    memset(mix, 0, sizeof(MixArchive));
    strncpy_s(mix->filePath, sizeof(mix->filePath), mixPath, _TRUNCATE);
    mix->fileCount = fileCount;

    uint8_t* hashTable = (uint8_t*)VirtualAlloc(NULL, hashTableSize, MEM_COMMIT, PAGE_READWRITE);
    if (!hashTable) { CloseHandle(hFile); return NULL; }

    if (!ReadFile(hFile, hashTable, hashTableSize, &read, NULL) || read != hashTableSize) {
        LogF("ParseMixFile: ReadFile failed for hash table");
        VirtualFree(hashTable, 0, MEM_RELEASE);
        CloseHandle(hFile); return NULL;
    }

    for (uint16_t i = 0; i < fileCount; i++) {
        uint32_t off = i * 12;
        mix->entries[i].crc = ReadU32(hashTable + off);
        mix->entries[i].offset = ReadU32(hashTable + off + 4);
        mix->entries[i].size = ReadU32(hashTable + off + 8);
        LogF("ParseMixFile: [%u] CRC=0x%08X offset=%u size=%u", i,
             mix->entries[i].crc, mix->entries[i].offset, mix->entries[i].size);
    }
    VirtualFree(hashTable, 0, MEM_RELEASE);

    uint32_t bodyOffset = hashTableOffset + hashTableSize;
    LogF("ParseMixFile: bodyOffset=%u, bodySize=%u", bodyOffset, fileSize - bodyOffset);

    uint32_t lmdCrc = 0x366E051F;
    int lmdIndex = -1;
    for (uint16_t i = 0; i < fileCount; i++) {
        if (mix->entries[i].crc == lmdCrc) {
            lmdIndex = i;
            break;
        }
    }
    LogF("ParseMixFile: LMD index=%d", lmdIndex);

    if (lmdIndex >= 0 && mix->entries[lmdIndex].size > 52) {
        uint32_t lmdOffset = bodyOffset + mix->entries[lmdIndex].offset;
        uint32_t lmdSize = mix->entries[lmdIndex].size;

        if (lmdOffset + lmdSize <= fileSize) {
            SetFilePointer(hFile, lmdOffset, NULL, FILE_BEGIN);
            uint8_t* lmdData = (uint8_t*)VirtualAlloc(NULL, lmdSize, MEM_COMMIT, PAGE_READWRITE);
            if (lmdData) {
                if (!ReadFile(hFile, lmdData, lmdSize, &read, NULL) || read != lmdSize) {
                    VirtualFree(lmdData, 0, MEM_RELEASE);
                } else {

                const uint8_t* nameStart = lmdData + 52;
            int remaining = lmdSize - 52;

                while (remaining > 0) {
                    const char* name = (const char*)nameStart;
                    int nameLen = (int)strnlen(name, remaining);
                    if (nameLen == 0 || nameLen >= remaining) break;

                    uint32_t computedCrc = MixCrc32(name);

                    for (uint16_t i = 0; i < fileCount; i++) {
                        if (mix->entries[i].crc == computedCrc && mix->entries[i].size > 0) {
                            char resolved[MAX_PATH];
                            _snprintf_s(resolved, sizeof(resolved), _TRUNCATE,
                                        "%s\\%s", mixPath, name);

                            LogF("LMD resolved: CRC=0x%08X -> %s (offset=%u, size=%u)",
                                 computedCrc, name, mix->entries[i].offset, mix->entries[i].size);
                            break;
                        }
                    }

                    nameStart += nameLen + 1;
                    remaining -= nameLen + 1;
                }
                VirtualFree(lmdData, 0, MEM_RELEASE);
                }
            }
        }
    }

    CloseHandle(hFile);
    mix->valid = 1;
    g_mixCacheCount++;
    LogF("Parsed .mix: %s (%u files, LMD %s)", mixPath, fileCount,
         lmdIndex >= 0 ? "found" : "not found");
    return mix;
}

// Find the .bik filename inside a .mix archive by matching filePos against hash table entries.
// Opens the .mix file, reads the LMD, and resolves the CRC32 to a name.
static const char* FindBikNameInMix(const char* mixPath, DWORD filePos) {
    MixArchive* mix = ParseMixFile(mixPath);
    if (!mix || !mix->valid) return NULL;

    uint32_t bodyOffset = 0xA + mix->fileCount * 12;
    uint32_t lmdCrc = 0x366E051F;

    for (uint16_t i = 0; i < mix->fileCount; i++) {
        if (mix->entries[i].crc == lmdCrc) continue;
        if (mix->entries[i].size == 0) continue;

        uint32_t entryStart = bodyOffset + mix->entries[i].offset;
        uint32_t entryEnd = entryStart + mix->entries[i].size;

        if (filePos >= entryStart && filePos < entryEnd) {
            HANDLE hFile = CreateFileA(mixPath, GENERIC_READ, FILE_SHARE_READ,
                                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) return NULL;

            int lmdIndex = -1;
            for (uint16_t j = 0; j < mix->fileCount; j++) {
                if (mix->entries[j].crc == lmdCrc) { lmdIndex = j; break; }
            }

            if (lmdIndex < 0) { CloseHandle(hFile); return NULL; }

            uint32_t lmdOffset = bodyOffset + mix->entries[lmdIndex].offset;
            uint32_t lmdSize = mix->entries[lmdIndex].size;
            if (lmdSize <= 52) { CloseHandle(hFile); return NULL; }

            SetFilePointer(hFile, lmdOffset, NULL, FILE_BEGIN);
            uint8_t* lmdData = (uint8_t*)VirtualAlloc(NULL, lmdSize, MEM_COMMIT, PAGE_READWRITE);
            if (!lmdData) { CloseHandle(hFile); return NULL; }

            DWORD read;
            if (!ReadFile(hFile, lmdData, lmdSize, &read, NULL) || read != lmdSize) {
                VirtualFree(lmdData, 0, MEM_RELEASE);
                CloseHandle(hFile); return NULL;
            }
            CloseHandle(hFile);

            const uint8_t* nameStart = lmdData + 52;
            int remaining = lmdSize - 52;
            static char resolvedName[MAX_PATH];
            resolvedName[0] = '\0';

            while (remaining > 0) {
                const char* name = (const char*)nameStart;
                int nameLen = (int)strnlen(name, remaining);
                if (nameLen == 0 || nameLen >= remaining) break;

                uint32_t computedCrc = MixCrc32(name);
                if (computedCrc == mix->entries[i].crc) {
                    strncpy_s(resolvedName, sizeof(resolvedName), name, _TRUNCATE);
                    break;
                }

                nameStart += nameLen + 1;
                remaining -= nameLen + 1;
            }

            VirtualFree(lmdData, 0, MEM_RELEASE);
            return resolvedName[0] ? resolvedName : NULL;
        }
    }
    return NULL;
}

// Look up .wav replacement for a .bik file.
// Priority: [exception] section (by mixName) > [audio] section (global fallback).
static const char* FindWavForBik(const char* bikPath, const char* mixName) {
    LoadAudioConfig();

    const char* fileName = NULL;
    if (bikPath) {
        fileName = bikPath;
        const char* slash = strrchr(bikPath, '\\');
        if (!slash) slash = strrchr(bikPath, '/');
        if (slash) fileName = slash + 1;
    }

    if (fileName && mixName) {
        for (int i = 0; i < g_exceptionCount; i++) {
            if (_stricmp(g_exceptions[i].mixName, mixName) == 0) {
                for (int j = 0; j < g_exceptions[i].mapCount; j++) {
                    if (_stricmp(g_exceptions[i].maps[j].bikName, fileName) == 0) {
                        LogF("Exception match: [%s] %s -> %s", mixName, fileName, g_exceptions[i].maps[j].wavPath);
                        return g_exceptions[i].maps[j].wavPath;
                    }
                }
            }
        }
    }

    for (int i = 0; i < g_audioMapCount; i++) {
        if (fileName && g_audioMaps[i].bikName[0]) {
            if (_stricmp(g_audioMaps[i].bikName, fileName) == 0) {
                return g_audioMaps[i].wavPath;
            }
        }
    }
    return NULL;
}

// ============================================================================
// WAV player (WaveOut, 4-buffer callback-based)
//
// - ParseWav: reads RIFF/WAVE PCM data
// - WaveOutProc: callback that refills buffers from PCM stream
// - Start/Stop/Pause/Resume/Seek: playback control
// - CRITICAL_SECTION protects shared state between main thread and callback
// ============================================================================

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

#define MAX_WAV_PLAYERS 8
static WavPlayer g_players[MAX_WAV_PLAYERS];
static int g_playerCount = 0;

static WavPlayer* AllocPlayer() {
    if (g_playerCount < MAX_WAV_PLAYERS) {
        WavPlayer* pl = &g_players[g_playerCount++];
        memset(pl, 0, sizeof(WavPlayer));
        pl->hWave = NULL;
        InitializeCriticalSection(&pl->cs);
        return pl;
    }
    return NULL;
}

static void FreePlayer(WavPlayer* pl) {
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

// Parse a WAV file: read RIFF header, find fmt/data chunks, return PCM data
static BOOL ParseWav(const char* path, WAVEFORMATEX* fmt, char** pcmOut, DWORD* pcmSizeOut) {
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
            channels = (WORD)ReadU16(fmtData + 2);
            sampleRate = ReadU32(fmtData + 4);
            bitsPerSample = (WORD)ReadU16(fmtData + 14);
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

    fmt->wFormatTag = WAVE_FORMAT_PCM;
    fmt->nChannels = channels;
    fmt->nSamplesPerSec = sampleRate;
    fmt->wBitsPerSample = bitsPerSample;
    fmt->nBlockAlign = (channels * bitsPerSample) / 8;
    fmt->nAvgBytesPerSec = sampleRate * fmt->nBlockAlign;
    fmt->cbSize = 0;

    *pcmOut = (char*)VirtualAlloc(NULL, dataSize, MEM_COMMIT, PAGE_READWRITE);
    if (!*pcmOut) { CloseHandle(hFile); return FALSE; }

    ReadFile(hFile, *pcmOut, dataSize, &read, NULL);
    CloseHandle(hFile);

    *pcmSizeOut = read;
    return TRUE;
}

// WaveOut callback: refills buffer from PCM stream on WOM_DONE
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

// Start WAV playback: parse file, open waveOut, fill initial buffers
static void WavPlayerStop(WavPlayer* pl);
static BOOL WavPlayerStart(WavPlayer* pl, const char* wavPath) {
    if (!pl || !wavPath) return FALSE;

    char fullPath[MAX_PATH];
    if (wavPath[1] == ':' || (wavPath[0] == '\\' && wavPath[1] == '\\')) {
        strncpy_s(fullPath, sizeof(fullPath), wavPath, _TRUNCATE);
    } else {
        _snprintf_s(fullPath, sizeof(fullPath), _TRUNCATE, "%s%s", g_dllDir, wavPath);
    }

    WAVEFORMATEX fmt;
    char* pcm = NULL;
    DWORD pcmSize = 0;

    if (!ParseWav(fullPath, &fmt, &pcm, &pcmSize)) {
        LogF("WAV parse failed: %s", fullPath);
        return FALSE;
    }

    pl->format = fmt;
    pl->pcmData = pcm;
    pl->pcmSize = pcmSize;
    pl->pcmPos = 0;
    pl->playing = TRUE;
    pl->paused = FALSE;
    pl->bufIndex = 0;

    MMRESULT res = waveOutOpen(&pl->hWave, WAVE_MAPPER, &fmt, (DWORD_PTR)WaveOutProc,
                               (DWORD_PTR)pl, CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        LogF("waveOutOpen failed: %u", res);
        VirtualFree(pcm, 0, MEM_RELEASE);
        pl->pcmData = NULL;
        return FALSE;
    }

    pl->bufSize = fmt.nAvgBytesPerSec / 2;
    if (pl->bufSize < 4096) pl->bufSize = 4096;

    for (int i = 0; i < 4; i++) {
        pl->buffers[i] = (char*)VirtualAlloc(NULL, pl->bufSize, MEM_COMMIT, PAGE_READWRITE);
        if (!pl->buffers[i]) {
            LogF("VirtualAlloc failed for wave buffer %d", i);
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

    LogF("WAV playback started: %s (%u Hz, %u bit, %u ch)",
         fullPath, fmt.nSamplesPerSec, fmt.wBitsPerSample, fmt.nChannels);
    return TRUE;
}

// Stop playback, release all waveOut resources and PCM data
static void WavPlayerStop(WavPlayer* pl) {
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
    LogF("WAV playback stopped");
}

// Pause playback (safe to call from any thread)
static void WavPlayerPause(WavPlayer* pl) {
    if (!pl || !pl->hWave) return;
    EnterCriticalSection(&pl->cs);
    if (!pl->paused) {
        pl->paused = TRUE;
        waveOutPause(pl->hWave);
    }
    LeaveCriticalSection(&pl->cs);
}

// Resume playback from pause
static void WavPlayerResume(WavPlayer* pl) {
    if (!pl || !pl->hWave) return;
    EnterCriticalSection(&pl->cs);
    if (pl->paused) {
        pl->paused = FALSE;
        waveOutRestart(pl->hWave);
    }
    LeaveCriticalSection(&pl->cs);
}

// Seek to sample position (used by BinkGoto for sync)
static void WavPlayerSeek(WavPlayer* pl, DWORD sampleOffset) {
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

// ============================================================================
// DLL loader — resolves real Bink DLL functions by ordinal
// ============================================================================

static BOOL LoadDll() {
    if (g_hR) return TRUE;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (!slash) return FALSE;
    *(slash + 1) = 0;

    char dllPath[MAX_PATH];
#ifdef BINK_10Q
    snprintf(dllPath, MAX_PATH, "%sbinkw32_1.0q.dll", exePath);
#else
    snprintf(dllPath, MAX_PATH, "%sbinkw32_1.9u.dll", exePath);
#endif

    g_hR = LoadLibraryA(dllPath);
    if (!g_hR) {
        LogF("FAILED to load real DLL: %s (error %lu)", dllPath, GetLastError());
        char msg[MAX_PATH + 64];
        wsprintfA(msg, "Proxy_Bink32w v1.0.1\nFailed to load real Bink DLL:\n%s", dllPath);
        MessageBoxA(NULL, msg, "binkw32.dll", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    LogF("Real DLL loaded: %s", dllPath);

#define L(n, o) p##n = (void*)GetProcAddress(g_hR, (LPCSTR)o)

#ifdef BINK_10Q
    L(BinkBufferBlit,1);          L(BinkBufferCheckWinPos,2);
    L(BinkBufferClear,3);         L(BinkBufferClose,4);
    L(BinkBufferGetDescription,5); L(BinkBufferGetError,6);
    L(BinkBufferLock,7);          L(BinkBufferOpen,8);
    L(BinkBufferSetDirectDraw,9); L(BinkBufferSetHWND,10);
    L(BinkBufferSetOffset,11);    L(BinkBufferSetResolution,12);
    L(BinkBufferSetScale,13);     L(BinkBufferUnlock,14);
    L(BinkCheckCursor,15);        L(BinkClose,16);
    L(BinkCloseTrack,17);         L(BinkCopyToBuffer,18);
    L(BinkDDSurfaceType,19);      L(BinkDoFrame,20);
    L(BinkGetError,21);           L(BinkGetKeyFrame,22);
    L(BinkGetRealtime,23);        L(BinkGetRects,24);
    L(BinkGetSummary,25);         L(BinkGetTrackData,26);
    L(BinkGetTrackID,27);         L(BinkGetTrackMaxSize,28);
    L(BinkGetTrackType,29);       L(BinkGoto,30);
    L(BinkIsSoftwareCursor,31);   L(BinkLogoAddress,32);
    L(BinkNextFrame,33);          L(BinkOpen,34);
    L(BinkOpenDirectSound,35);    L(BinkOpenMiles,36);
    L(BinkOpenTrack,37);          L(BinkOpenWaveOut,38);
    L(BinkPause,39);              L(BinkRestoreCursor,40);
    L(BinkService,41);            L(BinkSetError,42);
    L(BinkSetFrameRate,43);       L(BinkSetIO,44);
    L(BinkSetIOSize,45);          L(BinkSetPan,46);
    L(BinkSetSimulate,47);        L(BinkSetSoundOnOff,48);
    L(BinkSetSoundSystem,49);     L(BinkSetSoundTrack8,50);
    L(BinkSetVideoOnOff,51);      L(BinkSetVolume,52);
    L(BinkWait,53);
    L(ExpandBink,54);             L(ExpandBundleSizes,55);
    L(RADSetMemory,56);           L(RADTimerRead,57);
    L(YUV_blit_16a1bpp,58);       L(YUV_blit_16a1bpp_mask,59);
    L(YUV_blit_16a4bpp,60);       L(YUV_blit_16a4bpp_mask,61);
    L(YUV_blit_16bpp,62);         L(YUV_blit_16bpp_mask,63);
    L(YUV_blit_24bpp,64);         L(YUV_blit_24bpp_mask,65);
    L(YUV_blit_24rbpp,66);        L(YUV_blit_24rbpp_mask,67);
    L(YUV_blit_32abpp,68);        L(YUV_blit_32abpp_mask,69);
    L(YUV_blit_32bpp,70);         L(YUV_blit_32bpp_mask,71);
    L(YUV_blit_32rabpp,72);       L(YUV_blit_32rabpp_mask,73);
    L(YUV_blit_32rbpp,74);        L(YUV_blit_32rbpp_mask,75);
    L(YUV_blit_UYVY,76);          L(YUV_blit_UYVY_mask,77);
    L(YUV_blit_YUY2,78);          L(YUV_blit_YUY2_mask,79);
    L(YUV_blit_YV12,80);          L(YUV_init,81);
    L(radfree,82);                L(radmalloc,83);
#else
    L(BinkBufferBlit,1);          L(BinkBufferCheckWinPos,2);
    L(BinkBufferClear,3);         L(BinkBufferClose,4);
    L(BinkBufferGetDescription,5); L(BinkBufferGetError,6);
    L(BinkBufferLock,7);          L(BinkBufferOpen,8);
    L(BinkBufferSetDirectDraw,9); L(BinkBufferSetHWND,10);
    L(BinkBufferSetOffset,11);    L(BinkBufferSetResolution,12);
    L(BinkBufferSetScale,13);     L(BinkBufferUnlock,14);
    L(BinkCheckCursor,15);        L(BinkClose,16);
    L(BinkCloseTrack,17);         L(BinkControlBackgroundIO,18);
    L(BinkControlPlatformFeatures,19); L(BinkCopyToBuffer,20);
    L(BinkCopyToBufferRect,21);   L(BinkDDSurfaceType,22);
    L(BinkDX8SurfaceType,23);     L(BinkDX9SurfaceType,24);
    L(BinkDoFrame,25);            L(BinkDoFrameAsync,26);
    L(BinkDoFrameAsyncWait,27);   L(BinkDoFramePlane,28);
    L(BinkGetError,29);           L(BinkGetFrameBuffersInfo,30);
    L(BinkGetKeyFrame,31);        L(BinkGetPalette,32);
    L(BinkGetRealtime,33);        L(BinkGetRects,34);
    L(BinkGetSummary,35);         L(BinkGetTrackData,36);
    L(BinkGetTrackID,37);         L(BinkGetTrackMaxSize,38);
    L(BinkGetTrackType,39);       L(BinkGoto,40);
    L(BinkIsSoftwareCursor,41);   L(BinkLogoAddress,42);
    L(BinkNextFrame,43);          L(BinkOpen,44);
    L(BinkOpenDirectSound,45);    L(BinkOpenMiles,46);
    L(BinkOpenTrack,47);          L(BinkOpenWaveOut,48);
    L(BinkPause,49);              L(BinkRegisterFrameBuffers,50);
    L(BinkRequestStopAsyncThread,51); L(BinkRestoreCursor,52);
    L(BinkService,53);            L(BinkSetError,54);
    L(BinkSetFrameRate,55);       L(BinkSetIO,56);
    L(BinkSetIOSize,57);          L(BinkSetMemory,58);
    L(BinkSetMixBinVolumes,59);   L(BinkSetMixBins,60);
    L(BinkSetPan,61);             L(BinkSetSimulate,62);
    L(BinkSetSoundOnOff,63);      L(BinkSetSoundSystem,64);
    L(BinkSetSoundTrack8,65);     L(BinkSetVideoOnOff,66);
    L(BinkSetVolume,67);          L(BinkSetWillLoop,68);
    L(BinkShouldSkip,69);         L(BinkStartAsyncThread,70);
    L(BinkWait,71);               L(BinkWaitStopAsyncThread,72);
    L(RADTimerRead,73);
#endif

#undef L
    LogF("Proxied functions resolved: pBinkOpen=%p pBinkDoFrame=%p pBinkClose=%p pBinkWait=%p",
         pBinkOpen, pBinkDoFrame, pBinkClose, pBinkWait);
    return TRUE;
}

// ============================================================================
// Video handle tracking + audio replacement trigger
//
// TrackVideo: called from BinkOpen, resolves .bik name from .mix archive,
//             looks up WAV replacement, starts playback if found.
// UntrackVideo: called from BinkClose, stops WAV playback, frees temp buffer.
// ============================================================================

struct VideoInfo {
    void* handle;           // Bink video handle
    uint32_t width;         // Video width (from BinkGetSummary)
    uint32_t height;        // Video height
    void* tempBuf;          // Temporary buffer for scaling
    int tempPitch;          // Pitch of temp buffer
    int tempHeight;         // Height of temp buffer
    char wavPath[MAX_PATH]; // WAV replacement path (if active)
    WavPlayer* wavPlayer;   // Active WAV player (NULL if no replacement)
};

#define MAX_TRACKED 32
static VideoInfo g_vids[MAX_TRACKED];
static int g_vidCount = 0;

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(h);
        {
            char dllPath[MAX_PATH];
            DWORD len = GetModuleFileNameA(h, dllPath, MAX_PATH);
            if (len == 0 || len >= MAX_PATH) {
                LogF("WARNING: GetModuleFileNameA failed");
                g_dllDir[0] = '\0';
            } else {
                char* slash = strrchr(dllPath, '\\');
                if (slash) { *(slash + 1) = 0; lstrcpynA(g_dllDir, dllPath, MAX_PATH); }
                else g_dllDir[0] = '\0';
            }
        }
        LoadDll();
        break;
    case DLL_PROCESS_DETACH:
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].wavPlayer) {
                WavPlayerStop(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
            if (g_vids[i].tempBuf) VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE);
        }
        g_vidCount = 0;
        for (int i = 0; i < g_playerCount; i++) {
            FreePlayer(&g_players[i]);
        }
        g_playerCount = 0;
        if (g_hR) { FreeLibrary(g_hR); g_hR = NULL; }
        if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
        break;
    }
    return TRUE;
}

// Register a video handle: read dimensions from BinkGetSummary, start WAV if mapped
static void TrackVideo(void* h, const char* bikPath, const char* mixName) {
    if (!h || !pBinkGetSummary) return;
    unsigned char summary[512];
    memset(summary, 0, sizeof(summary));
    ((void(__stdcall*)(void*, void*))pBinkGetSummary)(h, summary);
    uint32_t w = ReadU32(summary);
    uint32_t hv = ReadU32(summary + 4);
    uint32_t frameRate = ReadU32(summary + 20);
    uint32_t frameRateDiv = ReadU32(summary + 24);
    if (w > 0 && hv > 0 && g_vidCount < MAX_TRACKED) {
        for (int i = 0; i < g_vidCount; i++) {
            if (g_vids[i].handle == h) {
                g_vids[i].width = w;
                g_vids[i].height = hv;
                LogF("Updated video: %p %ux%u", h, w, hv);
                return;
            }
        }
        g_vids[g_vidCount].handle = h;
        g_vids[g_vidCount].width = w;
        g_vids[g_vidCount].height = hv;
        g_vids[g_vidCount].tempBuf = 0;
        g_vids[g_vidCount].tempPitch = 0;
        g_vids[g_vidCount].tempHeight = 0;
        g_vids[g_vidCount].wavPath[0] = '\0';
        g_vids[g_vidCount].wavPlayer = NULL;

        BinkFileInfo bfi = {0};
        bfi.width = w;
        bfi.height = hv;
        bfi.frameRate = frameRate;
        bfi.frameRateDiv = frameRateDiv;
        bfi.valid = TRUE;

        const char* wav = FindWavForBik(bikPath, mixName);
        if (wav) {
            strncpy_s(g_vids[g_vidCount].wavPath, sizeof(g_vids[g_vidCount].wavPath), wav, _TRUNCATE);
            LogF("Audio replacement: %s [%ux%u] -> %s", bikPath ? bikPath : "?", w, hv, wav);

            WavPlayer* pl = AllocPlayer();
            if (pl && WavPlayerStart(pl, wav)) {
                g_vids[g_vidCount].wavPlayer = pl;
            } else {
                LogF("Failed to start WAV playback for %s", bikPath ? bikPath : "?");
            }
        } else {
            LogF("No audio mapping for: %s [%ux%u]", bikPath ? bikPath : "?", w, hv);
        }

        g_vidCount++;
        LogF("Tracked video: %p %ux%u", h, w, hv);
    }
}

// Unregister a video handle: stop WAV, free temp buffer, remove from tracking array
static void UntrackVideo(void* h) {
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) {
            if (g_vids[i].wavPlayer) {
                WavPlayerStop(g_vids[i].wavPlayer);
                g_vids[i].wavPlayer = NULL;
            }
            if (g_vids[i].tempBuf) VirtualFree(g_vids[i].tempBuf, 0, MEM_RELEASE);
            g_vids[i] = g_vids[g_vidCount - 1];
            g_vidCount--;
            return;
        }
    }
}

// Find tracked video info by handle
static VideoInfo* FindVideo(void* h) {
    for (int i = 0; i < g_vidCount; i++) {
        if (g_vids[i].handle == h) return &g_vids[i];
    }
    return 0;
}

// ============================================================================
// Video scaler — aspect-ratio-preserving fit (object-fit: contain)
//
// ScaleBilinear: scales source to destination maintaining aspect ratio.
//   bpp=2: nearest-neighbor (16-bit surfaces, used by RA2/RA2YR)
//   bpp=3: nearest-neighbor (24-bit surfaces)
//   bpp=4: bilinear interpolation (32-bit surfaces)
// ============================================================================

// Extract bits-per-pixel from BinkCopyToBuffer flags
static int BppFromFlags(int flags) {
    int st = flags & 7;
    if (st == 0) return 3;
    if (st <= 4) return 2;
    return 4;
}

// Bilinear interpolation for 16-bit RGB565 surfaces (bpp=2, used by RA2/RA2YR)
static void ScaleRGB565(const uint8_t* src, int sw, int sh, int sp,
                         uint8_t* dst, int dw, int dh, int dp) {
    if (dw <= 0 || dh <= 0) return;

    for (int y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        int sy2 = sy + 1 < sh ? sy + 1 : sy;
        int fy = ((y * sh) % dh) * 256 / dh;
        int ify = 256 - fy;

        const uint16_t* r0 = (const uint16_t*)(src + sy * sp);
        const uint16_t* r1 = (const uint16_t*)(src + sy2 * sp);
        uint16_t* dr = (uint16_t*)(dst + y * dp);

        for (int x = 0; x < dw; x++) {
            int sx = x * sw / dw;
            int sx2 = sx + 1 < sw ? sx + 1 : sx;
            int fx = ((x * sw) % dw) * 256 / dw;
            int ifx = 256 - fx;

            int p00 = r0[sx], p10 = r1[sx];
            int p01 = r0[sx2], p11 = r1[sx2];

            int r00 = (p00 >> 11) & 0x1F, g00 = (p00 >> 5) & 0x3F, b00 = p00 & 0x1F;
            int r10 = (p10 >> 11) & 0x1F, g10 = (p10 >> 5) & 0x3F, b10 = p10 & 0x1F;
            int r01 = (p01 >> 11) & 0x1F, g01 = (p01 >> 5) & 0x3F, b01 = p01 & 0x1F;
            int r11 = (p11 >> 11) & 0x1F, g11 = (p11 >> 5) & 0x3F, b11 = p11 & 0x1F;

            int top_r = r00 * ifx + r10 * fx, bot_r = r01 * ifx + r11 * fx;
            int top_g = g00 * ifx + g10 * fx, bot_g = g01 * ifx + g11 * fx;
            int top_b = b00 * ifx + b10 * fx, bot_b = b01 * ifx + b11 * fx;

            int rv = (top_r * ify + bot_r * fy + 32768) >> 16;
            int gv = (top_g * ify + bot_g * fy + 32768) >> 16;
            int bv = (top_b * ify + bot_b * fy + 32768) >> 16;

            dr[x] = (uint16_t)((rv << 11) | (gv << 5) | bv);
        }
    }
}

// ============================================================================
// Proxy exports — one stub per Bink API function
//
// Each stub:
//   1. Logs the call (selectively)
//   2. Intercepts logic (audio muting, video tracking, scaling)
//   3. Forwards to real DLL via function pointer
// ============================================================================

extern "C" {

intptr_t __stdcall sBinkLogoAddress() {
    void* p = pBinkLogoAddress;
    return p ? ((intptr_t(__stdcall*)())p)() : 0;
}

void __stdcall sBinkSetError(void* a) {
    void* p = pBinkSetError;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetError() {
    void* p = pBinkGetError;
    intptr_t r = p ? ((intptr_t(__stdcall*)())p)() : 0;
    LogF("BinkGetError()=%p", (void*)r);
    return r;
}

// Extract filename from BinkOpen parameters.
// BINK_FILE_HANDLE (0x800000): uses GetFinalPathNameByHandle
// BINK_FROM_MEMORY (0x04000000): returns empty (no filename available)
// Default: treats first parameter as char* filename
static void ExtractFileName(void* a, DWORD flags, char* out, int outSize) {
    out[0] = '\0';

    if (flags & 0x00800000) {
        HANDLE hFile = (HANDLE)(intptr_t)a;
        char pathBuf[MAX_PATH];
        DWORD len = GetFinalPathNameByHandleA(hFile, pathBuf, MAX_PATH, FILE_NAME_NORMALIZED);
        if (len > 0 && len < MAX_PATH) {
            const char* p = pathBuf;
            if (memcmp(p, "\\\\?\\", 4) == 0) p += 4;
            const char* slash = strrchr(p, '\\');
            if (slash) strncpy_s(out, outSize, slash + 1, _TRUNCATE);
            else strncpy_s(out, outSize, p, _TRUNCATE);
        }
        return;
    }

    if (flags & 0x04000000) {
        return;
    }

    if (a) {
        strncpy_s(out, outSize, (const char*)a, _TRUNCATE);
    }
}

// Log call stack with module name + RVA for each frame
static void LogCallStack(int skip) {
    void* stack[8];
    USHORT frames = CaptureStackBackTrace(skip, 8, stack, NULL);
    if (frames == 0) return;
    HMODULE hMod = NULL;
    char buf[512] = "";
    int pos = 0;
    for (USHORT i = 0; i < frames && pos < sizeof(buf) - 80; i++) {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)stack[i], &hMod);
        char modName[MAX_PATH] = "?";
        if (hMod) GetModuleFileNameA(hMod, modName, MAX_PATH);
        const char* slash = strrchr(modName, '\\');
        DWORD rva = (DWORD)((char*)stack[i] - (char*)hMod);
        pos += _snprintf_s(buf + pos, sizeof(buf) - pos, _TRUNCATE,
                           "  -> %s+0x%X", slash ? slash + 1 : modName, rva);
    }
    LogF("Call stack:%s", buf);
}

// BinkOpen — main entry point for video playback.
// Resolves .bik filename from .mix archive via LMD, starts WAV replacement if mapped.
intptr_t __stdcall sBinkOpen(void* a, void* b) {
    void* p = pBinkOpen;
    DWORD flags = (DWORD)(intptr_t)b;
    LogF("BinkOpen(%p,%p) flags=0x%08X ptr=%p", a, b, flags, p);

    char extractedName[MAX_PATH] = "";
    char mixFileName[MAX_PATH] = "";
    char* bikName = extractedName;

    if (flags & 0x00800000) {
        HANDLE hFile = (HANDLE)(intptr_t)a;
        DWORD pos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
        BinkFileInfo bfi = ReadBinkHeaderFromFile(hFile);
        if (bfi.valid) {
            LogF("Bink header: %ux%u, %u frames, %u/%u fps (file pos=%u)",
                 bfi.width, bfi.height, bfi.frameCount,
                 bfi.frameRate, bfi.frameRateDiv, pos);
        } else {
            LogF("No Bink header at current pos=%u", pos);
        }

        char mixPath[MAX_PATH] = "";
        ExtractFileName(a, flags, mixPath, sizeof(mixPath));
        if (mixPath[0]) {
            LogF("BinkOpen file: %s", mixPath);
            strncpy_s(mixFileName, sizeof(mixFileName), mixPath, _TRUNCATE);
            char fullMixPath[MAX_PATH];
            _snprintf_s(fullMixPath, sizeof(fullMixPath), _TRUNCATE, "%s%s", g_dllDir, mixPath);

            const char* bikInternal = FindBikNameInMix(fullMixPath, pos);
            if (bikInternal) {
                LogF("Bik name from .mix: %s", bikInternal);
                strncpy_s(extractedName, sizeof(extractedName), bikInternal, _TRUNCATE);
                bikName = extractedName;
            } else {
                bikName = mixPath;
            }
        }

        LogCallStack(1);
    } else if (a && !(flags & 0x04000000)) {
        bikName = (char*)a;
    }

    intptr_t r = p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
    LogF("BinkOpen->%p", (void*)r);
    if (r) {
        if (bikName[0]) LogF("BinkOpen resolved: %s", bikName);
        TrackVideo((void*)r, bikName[0] ? bikName : NULL, mixFileName[0] ? mixFileName : NULL);
    }
    return r;
}

intptr_t __stdcall sBinkOpenWithOptions(void* a, void* b, void* c) {
    void* p = pBinkOpenWithOptions;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
}

void __stdcall sBinkDoFrame(void* a) {
    void* p = pBinkDoFrame;
    LogF("BinkDoFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDoFramePlane(void* a, void* b) {
    void* p = pBinkDoFramePlane;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkNextFrame(void* a) {
    void* p = pBinkNextFrame;
    LogF("BinkNextFrame(%p)", a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkWait(void* a) {
    void* p = pBinkWait;
    if (g_logWait) LogF("BinkWait(%p)", a);
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

// BinkClose — stops WAV playback, frees temp buffer, removes from tracking
void __stdcall sBinkClose(void* a) {
    void* p = pBinkClose;
    LogF("BinkClose(%p)", a);
    UntrackVideo(a);
    if (p) ((void(__stdcall*)(void*))p)(a);
}

// BinkPause — pauses/resumes WAV playback alongside Bink video
intptr_t __stdcall sBinkPause(void* a, void* b) {
    void* p = pBinkPause;
    int pause = (int)(intptr_t)b;
    LogF("BinkPause(%p, %d)", a, pause);
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        if (pause) WavPlayerPause(vi->wavPlayer);
        else WavPlayerResume(vi->wavPlayer);
    }
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

// BinkCopyToBuffer — renders video frame with fit-scaling if needed.
// Allocates temp buffer at full resolution, scales to destination with aspect ratio preserved.
intptr_t __stdcall sBinkCopyToBuffer(void* a, void* b, void* c, void* d, void* e, void* f, void* g) {
    void* p = pBinkCopyToBuffer;
    LogF("BinkCopyToBuffer(%p,...)", a);
    if (!p) return 0;

    VideoInfo* vi = FindVideo(a);
    if (vi) {
        int dstPitch = (int)(intptr_t)c;
        int dstHeight = (int)(intptr_t)d;
        int destX = (int)(intptr_t)e;
        int destY = (int)(intptr_t)f;
        int flags = (int)(intptr_t)g;
        int bpp = BppFromFlags(flags);

        if (bpp > 0 && dstPitch > 0 && dstHeight > 0) {
            int dstW = dstPitch / bpp;
            int needScale = (vi->width > (uint32_t)dstW || vi->height > (uint32_t)dstHeight);

            if (needScale) {
                int srcPitch = vi->width * bpp;
                srcPitch = (srcPitch + 15) & ~15;
                int srcH = vi->height;
                SIZE_T requiredSize = (SIZE_T)srcPitch * srcH;

                if (!vi->tempBuf || vi->tempPitch != srcPitch || vi->tempHeight != srcH) {
                    if (vi->tempBuf) VirtualFree(vi->tempBuf, 0, MEM_RELEASE);
                    vi->tempBuf = VirtualAlloc(0, requiredSize, MEM_COMMIT, PAGE_READWRITE);
                    if (vi->tempBuf) {
                        vi->tempPitch = srcPitch;
                        vi->tempHeight = srcH;
                    } else {
                        LogF("VirtualAlloc failed: %zu bytes (error %lu)", requiredSize, GetLastError());
                        vi->tempPitch = 0;
                        vi->tempHeight = 0;
                    }
                }

                if (vi->tempBuf) {
                    int availW = dstW - destX;
                    int availH = dstHeight - destY;

                    uint32_t srcW = vi->width;
                    uint32_t srcHH = vi->height;

                    int scaleW = availW;
                    int scaleH = (int)((uint64_t)srcHH * availW / srcW);
                    if (scaleH > availH) {
                        scaleH = availH;
                        scaleW = (int)((uint64_t)srcW * availH / srcHH);
                    }
                    if (scaleW < 1) scaleW = 1;
                    if (scaleH < 1) scaleH = 1;

                    int offX = destX + (availW - scaleW) / 2;
                    int offY = destY + (availH - scaleH) / 2;

                    LogF("Scaling %ux%u -> %dx%d (fit in %dx%d, bpp=%d) at (%d,%d)",
                         srcW, srcHH, scaleW, scaleH, availW, availH, bpp, offX, offY);

                    intptr_t result = ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(
                        a, vi->tempBuf, (void*)(intptr_t)srcPitch,
                        (void*)(intptr_t)srcH, (void*)0, (void*)0, g);

                    ScaleRGB565((const uint8_t*)vi->tempBuf, srcW, srcHH, srcPitch,
                            (uint8_t*)b + offY * dstPitch + offX * bpp,
                            scaleW, scaleH, dstPitch);

                    return result;
                }
            }
        }
    }

    return ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g);
}

intptr_t __stdcall sBinkCopyToBufferRect(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k) {
    void* p = pBinkCopyToBufferRect;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k) : 0;
}

intptr_t __stdcall sBinkGetRects(void* a, void* b) {
    void* p = pBinkGetRects;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

// BinkGoto — seeks video to frame, also seeks WAV player to matching sample position
void __stdcall sBinkGoto(void* a, void* b, void* c) {
    void* p = pBinkGoto;
    uint32_t frame = (uint32_t)(intptr_t)b;
    LogF("BinkGoto(%p, %u)", a, frame);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer && vi->height > 0) {
        uint32_t fps = 30;
        if (pBinkGetSummary) {
            unsigned char summary[512];
            memset(summary, 0, sizeof(summary));
            ((void(__stdcall*)(void*, void*))pBinkGetSummary)(a, summary);
            uint32_t fr = ReadU32(summary + 20);
            uint32_t frd = ReadU32(summary + 24);
            if (fr > 0 && frd > 0) {
                fps = fr / frd;
                if (fps == 0) fps = 1;
            }
        }
        DWORD sampleOffset = (DWORD)((uint64_t)frame * vi->wavPlayer->format.nSamplesPerSec / fps);
        WavPlayerSeek(vi->wavPlayer, sampleOffset);
    }
}

intptr_t __stdcall sBinkGetKeyFrame(void* a, void* b, void* c) {
    void* p = pBinkGetKeyFrame;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*))p)(a, b, c) : 0;
}

void __stdcall sBinkFreeGlobals() {
    void* p = pBinkFreeGlobals;
    if (p) ((void(__stdcall*)())p)();
}

void __stdcall sBinkGetPlatformInfo(void* a, void* b) {
    void* p = pBinkGetPlatformInfo;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetFrameBuffersInfo(void* a, void* b) {
    void* p = pBinkGetFrameBuffersInfo;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRegisterFrameBuffers(void* a, void* b) {
    void* p = pBinkRegisterFrameBuffers;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetVideoOnOff(void* a, void* b) {
    void* p = pBinkSetVideoOnOff;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

// BinkSetSoundOnOff — mutes Bink audio when WAV replacement is active
void __stdcall sBinkSetSoundOnOff(void* a, void* b) {
    void* p = pBinkSetSoundOnOff;
    int on = (int)(intptr_t)b;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer && on) {
        LogF("BinkSetSoundOnOff: muted (WAV replacement active)");
        if (p) ((void(__stdcall*)(void*,void*))p)(a, (void*)0);
        return;
    }
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

// BinkSetVolume2 — adapter for 2-arg game import, mutes when WAV active
// (game calls @8 with 2 params, real DLL expects @12 with 3 params)
#ifdef BINK_10Q
void __stdcall sBinkSetVolume2(void* a, void* b) {
    void* p = pBinkSetVolume;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        LogF("BinkSetVolume2: muted (WAV replacement active)");
        if (p) ((void(__stdcall*)(void*,void*))p)(a, (void*)0);
        return;
    }
    LogF("BinkSetVolume2(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}
// BinkSetPan (1.0q) — drops pan when WAV replacement is active
void __stdcall sBinkSetPan(void* a, void* b, void* c) {
    void* p = pBinkSetPan;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        LogF("BinkSetPan: muted (WAV replacement active)");
        return;
    }
    LogF("BinkSetPan(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}
#else
void __stdcall sBinkSetVolume2(void* a, void* b) {
    void* p = pBinkSetVolume;
    VideoInfo* vi = FindVideo(a);
    if (vi && vi->wavPlayer) {
        LogF("BinkSetVolume2: muted (WAV replacement active)");
        if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, (void*)0, (void*)0);
        return;
    }
    LogF("BinkSetVolume2(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, 0);
}
// BinkSetPan (1.9u) — simple passthrough
void __stdcall sBinkSetPan(void* a, void* b, void* c) {
    void* p = pBinkSetPan;
    LogF("BinkSetPan(%p,%p,%p)", a, b, c);
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}
#endif

void __stdcall sBinkSetSpeakerVolumes(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkSetSpeakerVolumes;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sBinkService(void* a) {
    void* p = pBinkService;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkShouldSkip(void* a) {
    void* p = pBinkShouldSkip;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

void __stdcall sBinkGetPalette(void* a) {
    void* p = pBinkGetPalette;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkControlBackgroundIO(void* a, void* b) {
    void* p = pBinkControlBackgroundIO;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkControlPlatformFeatures(void* a, void* b) {
    void* p = pBinkControlPlatformFeatures;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkSetWillLoop(void* a, void* b) {
    void* p = pBinkSetWillLoop;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sBinkOpenTrack(void* a, void* b) {
    void* p = pBinkOpenTrack;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkCloseTrack(void* a) {
    void* p = pBinkCloseTrack;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkGetTrackData(void* a, void* b) {
    void* p = pBinkGetTrackData;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkGetTrackType(void* a, void* b) {
    void* p = pBinkGetTrackType;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkGetTrackMaxSize(void* a, void* b) {
    void* p = pBinkGetTrackMaxSize;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkGetTrackID(void* a, void* b) {
    void* p = pBinkGetTrackID;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkGetSummary(void* a, void* b) {
    void* p = pBinkGetSummary;
    LogF("BinkGetSummary(%p,%p)", a, b);
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkGetRealtime(void* a, void* b, void* c) {
    void* p = pBinkGetRealtime;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkSetFileOffset(void* a, void* b) {
    void* p = pBinkSetFileOffset;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

// BinkSetSoundTrack@8 — 2-arg passthrough (real DLL signature)
void __stdcall sBinkSetSoundTrack8(void* a, void* b) {
    void* p = pBinkSetSoundTrack8;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

// BinkSetSoundTrack@4 — adapts 1-arg game import to 2-arg real DLL (adds 0)
void __stdcall sBinkSetSoundTrack4(void* a) {
    void* p = pBinkSetSoundTrack8;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, 0);
}

void __stdcall sBinkSetIO(void* a) {
    void* p = pBinkSetIO;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetFrameRate(void* a, void* b) {
    void* p = pBinkSetFrameRate;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkSetSimulate(void* a) {
    void* p = pBinkSetSimulate;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetIOSize(void* a) {
    void* p = pBinkSetIOSize;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkSetSoundSystem(void* a, void* b) {
    void* p = pBinkSetSoundSystem;
    LogF("BinkSetSoundSystem(%p,%p)", a, b);
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

void __stdcall sBinkOpenDirectSound(void* a) {
    void* p = pBinkOpenDirectSound;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenWaveOut(void* a) {
    void* p = pBinkOpenWaveOut;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkOpenMiles(void* a) {
    void* p = pBinkOpenMiles;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

intptr_t __stdcall sBinkDX8SurfaceType(void* a) {
    void* p = pBinkDX8SurfaceType;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkDX9SurfaceType(void* a) {
    void* p = pBinkDX9SurfaceType;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkBufferOpen(void* a, void* b, void* c, void* d) {
    void* p = pBinkBufferOpen;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d) : 0;
}

void __stdcall sBinkBufferSetHWND(void* a, void* b) {
    void* p = pBinkBufferSetHWND;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sBinkDDSurfaceType(void* a) {
    void* p = pBinkDDSurfaceType;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkIsSoftwareCursor(void* a, void* b) {
    void* p = pBinkIsSoftwareCursor;
    return p ? ((intptr_t(__stdcall*)(void*,void*))p)(a, b) : 0;
}

intptr_t __stdcall sBinkCheckCursor(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkCheckCursor;
    return p ? ((intptr_t(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e) : 0;
}

void __stdcall sBinkBufferSetDirectDraw(void* a, void* b) {
    void* p = pBinkBufferSetDirectDraw;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkBufferClose(void* a) {
    void* p = pBinkBufferClose;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferLock(void* a) {
    void* p = pBinkBufferLock;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferUnlock(void* a) {
    void* p = pBinkBufferUnlock;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkBufferSetResolution(void* a, void* b, void* c) {
    void* p = pBinkBufferSetResolution;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferCheckWinPos(void* a, void* b, void* c) {
    void* p = pBinkBufferCheckWinPos;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetOffset(void* a, void* b, void* c) {
    void* p = pBinkBufferSetOffset;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferBlit(void* a, void* b, void* c) {
    void* p = pBinkBufferBlit;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkBufferSetScale(void* a, void* b, void* c) {
    void* p = pBinkBufferSetScale;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

intptr_t __stdcall sBinkBufferGetDescription(void* a) {
    void* p = pBinkBufferGetDescription;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

intptr_t __stdcall sBinkBufferGetError() {
    void* p = pBinkBufferGetError;
    return p ? ((intptr_t(__stdcall*)())p)() : 0;
}

void __stdcall sBinkBufferClear(void* a, void* b) {
    void* p = pBinkBufferClear;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRestoreCursor(void* a) {
    void* p = pBinkRestoreCursor;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkStartAsyncThread(void* a, void* b) {
    void* p = pBinkStartAsyncThread;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkDoFrameAsync(void* a, void* b, void* c) {
    void* p = pBinkDoFrameAsync;
    if (p) ((void(__stdcall*)(void*,void*,void*))p)(a, b, c);
}

void __stdcall sBinkDoFrameAsyncWait(void* a, void* b) {
    void* p = pBinkDoFrameAsyncWait;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sBinkRequestStopAsyncThread(void* a) {
    void* p = pBinkRequestStopAsyncThread;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkWaitStopAsyncThread(void* a) {
    void* p = pBinkWaitStopAsyncThread;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sBinkSetMixBins(void* a, void* b, void* c, void* d) {
    void* p = pBinkSetMixBins;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*))p)(a, b, c, d);
}

void __stdcall sBinkSetMixBinVolumes(void* a, void* b, void* c, void* d, void* e) {
    void* p = pBinkSetMixBinVolumes;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*))p)(a, b, c, d, e);
}

void __stdcall sExpandBink(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m, void* n) {
    void* p = pExpandBink;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
}

void __stdcall sExpandBundleSizes(void* a, void* b) {
    void* p = pExpandBundleSizes;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

void __stdcall sRADSetMemory(void* a, void* b) {
    void* p = pRADSetMemory;
    if (p) ((void(__stdcall*)(void*,void*))p)(a, b);
}

intptr_t __stdcall sRADTimerRead() {
    void* p = pRADTimerRead;
    return p ? ((intptr_t(__stdcall*)())p)() : 0;
}

intptr_t __stdcall sradmalloc(void* a) {
    void* p = pradmalloc;
    return p ? ((intptr_t(__stdcall*)(void*))p)(a) : 0;
}

void __stdcall sradfree(void* a) {
    void* p = pradfree;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

// ============================================================================
// YUV blit proxy stubs — mechanical passthroughs for all YUV surface types
// (ordinals 58-107 in .def, mostly unused by modern games)
// ============================================================================

void __stdcall sYUV_init(void* a) {
    void* p = pYUV_init;
    if (p) ((void(__stdcall*)(void*))p)(a);
}

void __stdcall sYUV_blit_16a1bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a1bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16a1bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a1bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16a4bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a4bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16a4bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_16a4bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_16bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_16bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_16bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_16bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24rbpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24rbpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_24rbpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_24rbpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32abpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32abpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32abpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32abpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32bpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32bpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32bpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32bpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32rabpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32rabpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32rabpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_32rabpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

void __stdcall sYUV_blit_32rbpp(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32rbpp;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_32rbpp_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_32rbpp_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_UYVY(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_UYVY;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_UYVY_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_UYVY_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_YUY2(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_YUY2;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_YUY2_mask(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l) {
    void* p = pYUV_blit_YUY2_mask;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l);
}

void __stdcall sYUV_blit_YV12(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j, void* k, void* l, void* m) {
    void* p = pYUV_blit_YV12;
    if (p) ((void(__stdcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*))p)(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

} /* extern "C" */
