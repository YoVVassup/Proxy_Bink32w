#include "binkw32_proxy.h"

// ============================================================================
// Audio replacement configuration
// ============================================================================

AudioMap g_audioMaps[64];
int g_audioMapCount = 0;
ExceptionEntry g_exceptions[32];
int g_exceptionCount = 0;
BOOL g_audioConfigLoaded = FALSE;
BOOL g_logWait = FALSE;
ScaleMode g_scaleMode = SCALE_BILINEAR;

void LoadAudioConfig() {
    if (g_audioConfigLoaded) return;
    g_audioConfigLoaded = TRUE;

    char cfgPath[MAX_PATH];
    _snprintf_s(cfgPath, sizeof(cfgPath), _TRUNCATE, "%sbinkw32.cfg", g_dllDir);

    FILE* f = NULL;
    fopen_s(&f, cfgPath, "r");
    if (!f) return;

    {
        char preLine[1024];
        BOOL inLogSection = FALSE;
        while (fgets(preLine, sizeof(preLine), f)) {
            TrimRight(preLine);
            if (preLine[0] == '[') {
                char* close = strchr(preLine, ']');
                if (close) *close = '\0';
                inLogSection = (_stricmp(preLine + 1, "log") == 0);
                continue;
            }
            if (inLogSection && preLine[0]) {
                char* eq = strchr(preLine, '=');
                if (eq) {
                    *eq = '\0';
                    char* k = preLine;
                    char* v = eq + 1;
                    TrimRight(k);
                    while (*v == ' ' || *v == '\t') v++;
                    TrimRight(v);
                    if (_stricmp(k, "enabled") == 0 && (_stricmp(v, "false") == 0 || _stricmp(v, "0") == 0)) {
                        g_logEnabled = FALSE;
                    }
                }
            }
        }
        rewind(f);
    }

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
                                 _stricmp(sectionName, "log") == 0 ||
                                 _stricmp(sectionName, "video") == 0);
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
        if (_stricmp(sectionName, "video") == 0) {
            if (_stricmp(k, "scale_mode") == 0) {
                g_scaleMode = ScaleModeFromName(v);
                LogF("  Video scale_mode: %s", ScaleModeName(g_scaleMode));
            }
        }

        if (inExceptionSection && g_exceptionCount < 32) {
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
            if (ex->mapCount < 64) {
                strncpy_s(ex->maps[ex->mapCount].bikName, sizeof(ex->maps[ex->mapCount].bikName), k, _TRUNCATE);
                strncpy_s(ex->maps[ex->mapCount].wavPath, sizeof(ex->maps[ex->mapCount].wavPath), v, _TRUNCATE);
                LogF("  Exception map [%s]: %s -> %s", ex->mixName, k, v);
                ex->mapCount++;
            }
        }

        if (inAudioSection && g_audioMapCount < 64) {
            if (!k[0] || !v[0]) continue;
            strncpy_s(g_audioMaps[g_audioMapCount].bikName, sizeof(g_audioMaps[g_audioMapCount].bikName), k, _TRUNCATE);
            strncpy_s(g_audioMaps[g_audioMapCount].wavPath, sizeof(g_audioMaps[g_audioMapCount].wavPath), v, _TRUNCATE);
            LogF("  Audio map: %s -> %s", k, v);
            g_audioMapCount++;
        }
    }
    fclose(f);
    LogF("Config loaded: %d audio maps, %d exceptions, log_wait=%d, scale=%s from %s",
         g_audioMapCount, g_exceptionCount, g_logWait, ScaleModeName(g_scaleMode), cfgPath);
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

const char* FindBikNameInMix(const char* mixPath, DWORD filePos) {
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
