#include "binkw32_proxy.h"
#include <stdlib.h>

// ============================================================================
// config.cpp — Configuration parsing, .mix archive parser, Bink header reader
//
// This module handles:
// 1. Config parsing (binkw32.cfg) — single-pass, in-memory
//    - [log]      : enabled, wait
//    - [exception]: per-mix audio replacement rules
//    - [audio]    : global audio fallback
//    - [mixname]  : per-mix .bik -> .wav/.ogg mappings
//
// 2. Bink header reader — reads video dimensions from .bik file headers
//
// 3. .mix archive parser — parses RA2/YR .mix format:
//    - Hash table at offset 0xA (12 bytes/entry: CRC32 + offset + size)
//    - LMD file (CRC32 0x366E051F) for CRC32 -> filename mapping
//    - CRC32 computed with RA2 convention: uppercase + 4-byte padding
//    - Results cached in g_mixCache[8] to avoid re-parsing
//
// 4. FindBikNameInMix — resolves .bik filename from .mix by file position
// 5. FindWavForBik — looks up .wav/.ogg replacement for a .bik file
// ============================================================================

// ============================================================================
// Audio replacement configuration
// ============================================================================

AudioMap g_audioMaps[64];
int g_audioMapCount = 0;
ExceptionEntry g_exceptions[32];
int g_exceptionCount = 0;
static LONG g_audioConfigLoaded = FALSE;
BOOL g_logWait = FALSE;

void ResetAudioConfig() {
    InterlockedExchange(&g_audioConfigLoaded, FALSE);
    g_audioMapCount = 0;
    g_exceptionCount = 0;
    g_logEnabled = TRUE;
    g_logWait = FALSE;
}

void LoadAudioConfig() {
    if (InterlockedCompareExchange(&g_audioConfigLoaded, TRUE, FALSE) != FALSE) return;

    char cfgPath[MAX_PATH];
    _snprintf_s(cfgPath, sizeof(cfgPath), _TRUNCATE, "%sbinkw32.cfg", g_dllDir);

    FILE* f = NULL;
    fopen_s(&f, cfgPath, "r");
    if (!f) return;

    // Read entire file into memory for single-pass parsing
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0) { fclose(f); return; }
    char* fileBuf = (char*)malloc(fileSize + 1);
    if (!fileBuf) { fclose(f); return; }
    size_t bytesRead = fread(fileBuf, 1, fileSize, f);
    fileBuf[bytesRead] = '\0';
    fclose(f);

    // Single-pass parse
    BOOL inAudioSection = FALSE;
    BOOL inExceptionSection = FALSE;
    BOOL inExceptionMix = FALSE;
    BOOL inLogSection = FALSE;
    int currentExceptionIdx = -1;
    char sectionName[64] = "";
    char line[1024];

    char* pos = fileBuf;
    if (bytesRead >= 3 &&
        (unsigned char)pos[0] == 0xEF && (unsigned char)pos[1] == 0xBB && (unsigned char)pos[2] == 0xBF)
        pos += 3;
    while (*pos) {
        // Find end of line (handle both \n and \r\n)
        char* eol = pos;
        while (*eol && *eol != '\n') eol++;
        int lineLen = (int)(eol - pos);
        if (lineLen >= (int)sizeof(line)) lineLen = sizeof(line) - 1;
        memcpy(line, pos, lineLen);
        line[lineLen] = '\0';
        pos = *eol ? eol + 1 : eol;

        TrimRight(line);
        if (line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (close) *close = '\0';
            strncpy_s(sectionName, sizeof(sectionName), line + 1, _TRUNCATE);
            inAudioSection = (_stricmp(sectionName, "audio") == 0);
            inExceptionSection = (_stricmp(sectionName, "exception") == 0);
            inLogSection = (_stricmp(sectionName, "log") == 0);
            inExceptionMix = FALSE;
            currentExceptionIdx = -1;

            if (!inAudioSection && !inExceptionSection && !inLogSection && sectionName[0]) {
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

        if (inLogSection) {
            if (_stricmp(k, "enabled") == 0 && (_stricmp(v, "false") == 0 || _stricmp(v, "0") == 0)) {
                g_logEnabled = FALSE;
            }
            if (_stricmp(k, "wait") == 0) {
                g_logWait = (_stricmp(v, "true") == 0 || _stricmp(v, "1") == 0);
            }
        }

        if (inExceptionSection && g_exceptionCount < 32) {
            if (!k[0] || !v[0]) continue;
            strncpy_s(g_exceptions[g_exceptionCount].mixName,
                       sizeof(g_exceptions[g_exceptionCount].mixName), v, _TRUNCATE);
            g_exceptions[g_exceptionCount].mapCount = 0;
            LogF("  Exception: %s", v);
            g_exceptionCount++;
        } else if (inExceptionSection && g_exceptionCount >= 32) {
            LogF("WARNING: Exception section limit reached (32), skipping: %s", v);
        }

        if (inExceptionMix && currentExceptionIdx >= 0) {
            if (!k[0] || !v[0]) continue;
            ExceptionEntry* ex = &g_exceptions[currentExceptionIdx];
            if (ex->mapCount < 64) {
                strncpy_s(ex->maps[ex->mapCount].bikName, sizeof(ex->maps[ex->mapCount].bikName), k, _TRUNCATE);
                strncpy_s(ex->maps[ex->mapCount].wavPath, sizeof(ex->maps[ex->mapCount].wavPath), v, _TRUNCATE);
                LogF("  Exception map [%s]: %s -> %s", ex->mixName, k, v);
                ex->mapCount++;
            } else {
                LogF("WARNING: Exception map limit reached (64) for [%s], skipping: %s", ex->mixName, k);
            }
        }

        if (inAudioSection && g_audioMapCount < 64) {
            if (!k[0] || !v[0]) continue;
            strncpy_s(g_audioMaps[g_audioMapCount].bikName, sizeof(g_audioMaps[g_audioMapCount].bikName), k, _TRUNCATE);
            strncpy_s(g_audioMaps[g_audioMapCount].wavPath, sizeof(g_audioMaps[g_audioMapCount].wavPath), v, _TRUNCATE);
            LogF("  Audio map: %s -> %s", k, v);
            g_audioMapCount++;
        } else if (inAudioSection && g_audioMapCount >= 64) {
            LogF("WARNING: Audio map limit reached (64), skipping: %s", k);
        }
    }
    free(fileBuf);
    LogF("Config loaded: %d audio maps, %d exceptions, log_wait=%d from %s",
         g_audioMapCount, g_exceptionCount, g_logWait, cfgPath);
}

// ============================================================================
// Bink file header reader
// ============================================================================

BinkFileInfo ReadBinkHeaderFromFile(HANDLE hFile) {
    BinkFileInfo info = {0};
    DWORD origPos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
    char hdr[44];
    DWORD read;
    if (!ReadFile(hFile, hdr, sizeof(hdr), &read, NULL) || read < sizeof(hdr)) {
        SetFilePointer(hFile, origPos, NULL, FILE_BEGIN);
        return info;
    }
    SetFilePointer(hFile, origPos, NULL, FILE_BEGIN);

    // Bink markers: 'BIKf' (0x42,0x49,0x4B,0x66), 'BIKg' (..0x67), 'BIKh' (..0x68), 'BIKi' (..0x69)
    // Compare bytes directly — multi-char literals are unreliable across compilers
    if (!((hdr[0] == 0x42 && hdr[1] == 0x49 && hdr[2] == 0x4B) &&
          (hdr[3] == 0x66 || hdr[3] == 0x67 || hdr[3] == 0x68 || hdr[3] == 0x69))) {
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

BinkFileInfo ReadBinkHeaderFromPath(const char* path) {
    BinkFileInfo info = {0};
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return info;
    info = ReadBinkHeaderFromFile(hFile);
    CloseHandle(hFile);
    return info;
}

// ============================================================================
// .mix archive parser + LMD resolver
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

uint32_t MixCrc32(const char* name) {
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

MixArchive g_mixCache[8];
int g_mixCacheCount = 0;

MixArchive* ParseMixFile(const char* mixPath) {
    for (int i = 0; i < g_mixCacheCount; i++) {
        if (_stricmp(g_mixCache[i].filePath, mixPath) == 0)
            return &g_mixCache[i];
    }
    if (g_mixCacheCount >= 8) {
        LogF("WARNING: MixArchive cache full (%d entries), cannot parse: %s", g_mixCacheCount, mixPath);
        return NULL;
    }

    LogF("ParseMixFile: opening %s", mixPath);

    HANDLE hFile = CreateFileA(mixPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogF("ParseMixFile: FAILED to open (error %lu)", GetLastError());
        return NULL;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    LogF("ParseMixFile: file size=%u", fileSize);
    if (fileSize == INVALID_FILE_SIZE || fileSize < 14) { CloseHandle(hFile); return NULL; }

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
    if (fileCount == 0 || fileCount > 256) {
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

    uint8_t* hashTable = (uint8_t*)malloc(hashTableSize);
    if (!hashTable) { CloseHandle(hFile); return NULL; }

    if (!ReadFile(hFile, hashTable, hashTableSize, &read, NULL) || read != hashTableSize) {
        LogF("ParseMixFile: ReadFile failed for hash table");
        free(hashTable);
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
    free(hashTable);

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
            uint8_t* lmdData = (uint8_t*)malloc(lmdSize);
            if (lmdData) {
                if (!ReadFile(hFile, lmdData, lmdSize, &read, NULL) || read != lmdSize) {
                    free(lmdData);
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
                                LogF("LMD resolved: CRC=0x%08X -> %s (offset=%u, size=%u)",
                                     computedCrc, name, mix->entries[i].offset, mix->entries[i].size);
                                break;
                            }
                        }

                        nameStart += nameLen + 1;
                        remaining -= nameLen + 1;
                    }
                    free(lmdData);
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

BOOL FindBikNameInMix(const char* mixPath, DWORD filePos, char* outName, int outNameSize) {
    outName[0] = '\0';
    MixArchive* mix = ParseMixFile(mixPath);
    if (!mix || !mix->valid) return FALSE;

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
            if (hFile == INVALID_HANDLE_VALUE) return FALSE;

            int lmdIndex = -1;
            for (uint16_t j = 0; j < mix->fileCount; j++) {
                if (mix->entries[j].crc == lmdCrc) { lmdIndex = j; break; }
            }

            if (lmdIndex < 0) { CloseHandle(hFile); return FALSE; }

            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize == INVALID_FILE_SIZE) { CloseHandle(hFile); return FALSE; }
            uint32_t lmdOffset = bodyOffset + mix->entries[lmdIndex].offset;
            uint32_t lmdSize = mix->entries[lmdIndex].size;
            if (lmdSize <= 52 || lmdOffset + lmdSize > fileSize) { CloseHandle(hFile); return FALSE; }

            SetFilePointer(hFile, lmdOffset, NULL, FILE_BEGIN);
            uint8_t* lmdData = (uint8_t*)malloc(lmdSize);
            if (!lmdData) { CloseHandle(hFile); return FALSE; }

            DWORD read;
            if (!ReadFile(hFile, lmdData, lmdSize, &read, NULL) || read != lmdSize) {
                free(lmdData);
                CloseHandle(hFile); return FALSE;
            }
            CloseHandle(hFile);

            const uint8_t* nameStart = lmdData + 52;
            int remaining = lmdSize - 52;

            while (remaining > 0) {
                const char* name = (const char*)nameStart;
                int nameLen = (int)strnlen(name, remaining);
                if (nameLen == 0 || nameLen >= remaining) break;

                uint32_t computedCrc = MixCrc32(name);
                if (computedCrc == mix->entries[i].crc) {
                    strncpy_s(outName, outNameSize, name, _TRUNCATE);
                    break;
                }

                nameStart += nameLen + 1;
                remaining -= nameLen + 1;
            }

            free(lmdData);
            return outName[0] ? TRUE : FALSE;
        }
    }
    return FALSE;
}

const char* FindWavForBik(const char* bikPath, const char* mixName) {
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
