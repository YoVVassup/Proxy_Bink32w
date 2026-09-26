#include "binkw32_proxy.h"
#include "mix_crypto.h"
#include <stdlib.h>

// ============================================================================
// config.cpp — Configuration parsing, .mix archive parser, Bink header reader
//
// This module handles:
// 1. Config parsing (binkw32.cfg) — single-pass, in-memory
//    - [log]      : enabled, wait
//    - [exception]: per-mix audio replacement rules
//      entry: mixName  or  mixName|baseDir (auto stem.ogg/.wav under baseDir)
//    - [audio]    : global audio fallback
//    - [mixname]  : per-mix .bik -> .wav/.ogg mappings
//
// 2. Bink header reader — reads video dimensions from .bik file headers
//
// 3. .mix archive parser — parses RA2/YR .mix format:
//    - Extended unencrypted: count@4, hash table @0xA (12 bytes/entry)
//    - Extended encrypted (flags&2): Blowfish-ECB header via key_source(80B)
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

static void MixLock();
static void MixUnlock();
static LONG g_mixCsOnce = 0;
static CRITICAL_SECTION g_mixCs;

AudioMap g_audioMaps[MAX_AUDIO_MAPS];
int g_audioMapCount = 0;
ExceptionEntry g_exceptions[MAX_EXCEPTION_MIXES];
int g_exceptionCount = 0;
static LONG g_audioConfigLoaded = FALSE;
BOOL g_logWait = FALSE;

// Warn-once flags of the config loader. File scope rather than function
// statics so ResetAudioConfig can rewind them: production never reloads the
// config and kept them for the whole session, which is what broke
// --gtest_repeat (the second pass saw none of the warnings).
static BOOL g_warnedExLimit = FALSE;
static BOOL g_warnedAudioLimit = FALSE;
static BOOL g_warnedMissingCfg = FALSE;
static BOOL g_warnedEmptyCfg = FALSE;
static BOOL g_warnedWideCfg = FALSE;
static BOOL g_warnedCfgOom = FALSE;

// Test-only reset. Production never reloads the config at runtime, so the
// unlocked zeroing below is single-threaded by construction: do NOT call this
// while FindWavForBik/LoadAudioConfig may be running on another thread —
// readers take no lock either (they are serialised by the load state machine
// above, which ResetAudioConfig also rewinds to "idle").
void ResetAudioConfig() {
    InterlockedExchange(&g_audioConfigLoaded, FALSE);
    g_audioMapCount = 0;
    g_exceptionCount = 0;
    g_logEnabled = TRUE;
    g_logWait = FALSE;
    g_warnedExLimit = FALSE;
    g_warnedAudioLimit = FALSE;
    g_warnedMissingCfg = FALSE;
    g_warnedEmptyCfg = FALSE;
    g_warnedWideCfg = FALSE;
    g_warnedCfgOom = FALSE;
}

// Release the parsed-.mix cache. Called from DllMain(PROCESS_DETACH) —
// resetting g_mixCacheCount alone leaked every per-archive entry array.
//
// DllMain runs under the loader lock, so a plain MixLock() here deadlocks
// against a thread that holds g_mixCs inside ParseMixFile while itself waiting
// on the loader (CreateFile/ReadFile/Ldrp critical region). TryEnterCriticalSection:
// if the lock is busy, skip the cleanup — the process is going away and the
// OS reclaims the heap anyway. If the CS was never created nothing was ever
// cached, so there is nothing to free. Never log from here (called under the
// loader lock, and the log is already shut down).
void FreeMixCache() {
    if (InterlockedCompareExchange(&g_mixCsOnce, 2, 2) != 2) return;
    if (!TryEnterCriticalSection(&g_mixCs)) return;
    for (int i = 0; i < g_mixCacheCount; i++) {
        if (g_mixCache[i].entries) {
            free(g_mixCache[i].entries);
            g_mixCache[i].entries = NULL;
        }
    }
    g_mixCacheCount = 0;
    LeaveCriticalSection(&g_mixCs);
}

// Strip a trailing inline comment (` ; ...`, ` # ...`): the marker must be
// preceded by whitespace (or start the value) so Windows paths that contain
// `;`/`#` — e.g. `C:\a;b` — keep working. Documented in README.md:288, e.g.
// `enabled = false   ; disable all logging`.
static char* StripInlineComment(char* v) {
    for (char* p = v; *p; p++) {
        if ((*p == ';' || *p == '#') && (p == v || p[-1] == ' ' || p[-1] == '\t')) {
            *p = '\0';
            TrimRight(v);
            break;
        }
    }
    return v;
}

// true/1/yes/on -> TRUE, false/0/no/off -> FALSE, anything else -> def.
// Unrecognised values used to be dropped silently (`wait = yes` became FALSE,
// `enabled = no` stayed TRUE). warn=FALSE for the [log] pre-pass, which must
// stay quiet while `enabled = false` is still being resolved.
static BOOL ParseBoolValue(const char* v, BOOL def, const char* key, BOOL warn) {
    if (!v[0]) {
        if (warn) LogF("WARNING: [%s] value is empty, keeping default", key);
        return def;
    }
    if (!_stricmp(v, "true") || !_stricmp(v, "1") || !_stricmp(v, "yes") || !_stricmp(v, "on"))
        return TRUE;
    if (!_stricmp(v, "false") || !_stricmp(v, "0") || !_stricmp(v, "no") || !_stricmp(v, "off"))
        return FALSE;
    if (warn) LogF("WARNING: [%s] value '%s' not recognised, keeping default", key, v);
    return def;
}

// WarnTruncate (logging.cpp) is used for every `_TRUNCATE` copy here.

// `[log]` may sit after lines that already emit output, so apply the
// section in a pre-pass before the real parse — otherwise those leading lines
// are written to the log even when the user set `enabled = false`.
static void PrescanLogSection(const char* fileBuf) {
    char section[64] = "";
    char line[1024];
    const char* pos = fileBuf;
    if ((unsigned char)pos[0] == 0xEF && (unsigned char)pos[1] == 0xBB && (unsigned char)pos[2] == 0xBF)
        pos += 3;
    while (*pos) {
        const char* eol = pos;
        while (*eol && *eol != '\n') eol++;
        int lineLen = (int)(eol - pos);
        if (lineLen >= (int)sizeof(line)) lineLen = sizeof(line) - 1;
        memcpy(line, pos, lineLen);
        line[lineLen] = '\0';
        pos = *eol ? eol + 1 : eol;

        TrimRight(line);
        // Blank lines and comments (leading whitespace tolerated) never
        // carry data — including indented "; key = value" comments.
        char* skip = line;
        while (*skip == ' ' || *skip == '\t') skip++;
        if (!*skip || *skip == ';' || *skip == '#') continue;
        if (skip != line) memmove(line, skip, strlen(skip) + 1);
        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (close) *close = '\0';
            WarnTruncate("prescan section name", line + 1, sizeof(section));
            strncpy_s(section, sizeof(section), line + 1, _TRUNCATE);
            continue;
        }
        if (_stricmp(section, "log") != 0) continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        TrimRight(k);
        while (*v == ' ' || *v == '\t') v++;
        TrimRight(v);
        StripInlineComment(v);
        if (_stricmp(k, "enabled") == 0)
            g_logEnabled = ParseBoolValue(v, g_logEnabled, "enabled", FALSE);
        if (_stricmp(k, "wait") == 0)
            g_logWait = ParseBoolValue(v, g_logWait, "wait", FALSE);
    }
}

// UTF-16 without BOM: ASCII text is stored as (char, 0x00) pairs, so count
// NULs on even/odd offsets over the first 64 bytes — >=50% on one side means
// UTF-16 (LE: NULs at odd offsets, BE: at even). Plain ANSI has no NULs.
static BOOL LooksLikeUtf16NoBom(const char* buf, size_t len, BOOL* isLe) {
    size_t n = (len < 64) ? len : 64;
    size_t pairs = 0, evenNul = 0, oddNul = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        pairs++;
        if ((unsigned char)buf[i] == 0) evenNul++;
        if ((unsigned char)buf[i + 1] == 0) oddNul++;
    }
    if (pairs == 0) return FALSE;
    if (oddNul * 2 >= pairs) { *isLe = TRUE; return TRUE; }
    if (evenNul * 2 >= pairs) { *isLe = FALSE; return TRUE; }
    return FALSE;
}

void LoadAudioConfig() {
    // 0 = idle, 1 = loading in progress, 2 = loaded (publish state only AFTER
    // parsing so other threads never see a half-filled config).
    LONG prev = InterlockedCompareExchange(&g_audioConfigLoaded, 1, 0);
    if (prev == 2) return;
    if (prev == 1) {
        // Bounded wait, mirroring EnsureInitialized(): a loader that never
        // publishes state 2 would otherwise hang every caller forever, and
        // FindWavForBik runs under TrackLock. State 0 means the loader gave
        // up (no cfg file yet) — nothing left to wait for.
        DWORD start = GetTickCount();
        LONG s = 1;
        while ((s = InterlockedCompareExchange(&g_audioConfigLoaded, 2, 2)) == 1 &&
               GetTickCount() - start < 2000) {
            SwitchToThread();
        }
        if (s == 1)
            LogF("LoadAudioConfig: loader stuck for 2s, continuing without config");
        return;
    }
    // Per-load, per-mix: one flag for the whole config meant the FIRST mix to
    // hit MAX_MAPS_PER_MIX was reported and every later one was dropped
    // silently. Tracked by section index (sections are contiguous), so each
    // overflowing mix gets exactly one warning per parse.
    int warnedMapLimitMix = -1;

    char cfgPath[MAX_PATH];
    // `_TRUNCATE` would leave a path that simply does not exist and the
    // "Config not found" line below would name it as if it were correct.
    if (strlen(g_dllDir) + strlen("binkw32.cfg") >= sizeof(cfgPath))
        LogF("WARNING: config path longer than %u chars, truncated: %sbinkw32.cfg",
             (unsigned)(sizeof(cfgPath) - 1), g_dllDir);
    _snprintf_s(cfgPath, sizeof(cfgPath), _TRUNCATE, "%sbinkw32.cfg", g_dllDir);

    FILE* f = NULL;
    // "rb": in text mode 0x1A is EOF (a stray Ctrl+Z silently dropped the
    // rest of the cfg) and CRLF translation makes fread return fewer bytes
    // than ftell reported.
    fopen_s(&f, cfgPath, "rb");
    if (!f) {
        // Deliberately NOT marked as loaded: a cfg created or fixed later
        // (user edit, mod installer) must still be picked up. The retry costs
        // one fopen per video open — negligible — and it is logged once so a
        // missing config is never silent. Publish "idle" (0), not "loaded" (2),
        // and never leave the state at 1 (that is "loader in progress" and
        // would make every caller wait for a loader that already gave up).
        if (!g_warnedMissingCfg) {
            g_warnedMissingCfg = TRUE;
            LogF("Config not found: %s (audio replacement off until it appears)", cfgPath);
        }
        InterlockedExchange(&g_audioConfigLoaded, 0);
        return;
    }

    // Read entire file into memory for single-pass parsing
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0) {
        // An empty cfg is the same situation as a missing one: a file the
        // user fills in later must still be picked up, so publish "idle" (0)
        // rather than "loaded" (2) — otherwise a config created after the
        // first video open is ignored for the rest of the session.
        fclose(f);
        if (!g_warnedEmptyCfg) {
            g_warnedEmptyCfg = TRUE;
            LogF("Config is empty: %s (audio replacement off until it has content)", cfgPath);
        }
        InterlockedExchange(&g_audioConfigLoaded, 0);
        return;
    }
    char* fileBuf = (char*)malloc(fileSize + 1);
    if (!fileBuf) {
        // Same story as a missing cfg: log it and publish "idle" so the next
        // video open retries. state 2 here used to latch audio replacement
        // off for the rest of the session with nothing in the log.
        fclose(f);
        if (!g_warnedCfgOom) {
            g_warnedCfgOom = TRUE;
            LogF("WARNING: out of memory reading config (%ld bytes), audio replacement off for now: %s",
                 fileSize, cfgPath);
        }
        InterlockedExchange(&g_audioConfigLoaded, 0);
        return;
    }
    size_t bytesRead = fread(fileBuf, 1, fileSize, f);
    fileBuf[bytesRead] = '\0';
    fclose(f);
    BOOL shortRead = (bytesRead != (size_t)fileSize);

    // UTF-16 (with or without the file being re-saved as ANSI) is unreadable
    // by this byte-oriented parser: the ASCII parts are interleaved with NUL
    // bytes, so the first `*pos` check ends the parse immediately and the user
    // gets a silently empty config. Say so and leave the state idle so a
    // re-saved file is picked up on the next video open.
    if (bytesRead >= 2 &&
        ((unsigned char)fileBuf[0] == 0xFF && (unsigned char)fileBuf[1] == 0xFE ||
         (unsigned char)fileBuf[0] == 0xFE && (unsigned char)fileBuf[1] == 0xFF)) {
        if (!g_warnedWideCfg) {
            g_warnedWideCfg = TRUE;
            LogF("Config is UTF-16 (%s), not UTF-8/ANSI — re-save it as ANSI or UTF-8: %s",
                 ((unsigned char)fileBuf[0] == 0xFF) ? "LE BOM" : "BE BOM", cfgPath);
        }
        free(fileBuf);
        InterlockedExchange(&g_audioConfigLoaded, 0);
        return;
    }
    // Same story without the marker: the interleaved NULs end the parse on
    // the first line and the keys are dropped silently, so detect the pair
    // pattern, say so, and leave the state idle for a re-saved file.
    if (bytesRead >= 4) {
        BOOL isLe = FALSE;
        if (LooksLikeUtf16NoBom(fileBuf, bytesRead, &isLe)) {
            if (!g_warnedWideCfg) {
                g_warnedWideCfg = TRUE;
                LogF("Config is UTF-16 (no BOM, %s), not UTF-8/ANSI — re-save it as ANSI or UTF-8: %s",
                     isLe ? "LE" : "BE", cfgPath);
            }
            free(fileBuf);
            InterlockedExchange(&g_audioConfigLoaded, 0);
            return;
        }
    }

    // Honour `[log]` before anything below logs.
    PrescanLogSection(fileBuf);

    if (shortRead)
        LogF("WARNING: config read %u of %ld bytes: %s",
             (unsigned)bytesRead, fileSize, cfgPath);

    // Single-pass parse
    BOOL inAudioSection = FALSE;
    BOOL inExceptionSection = FALSE;
    BOOL inExceptionMix = FALSE;
    BOOL inLogSection = FALSE;
    BOOL sectionSeen = FALSE;
    BOOL sawDataLine = FALSE;
    int currentExceptionIdx = -1;
    char sectionName[64] = "";
    char line[1024];
    int lineNo = 0;

    char* pos = fileBuf;
    if (bytesRead >= 3 &&
        (unsigned char)pos[0] == 0xEF && (unsigned char)pos[1] == 0xBB && (unsigned char)pos[2] == 0xBF)
        pos += 3;
    while (*pos) {
        lineNo++;
        // Find end of line (handle both \n and \r\n)
        char* eol = pos;
        while (*eol && *eol != '\n') eol++;
        int lineLen = (int)(eol - pos);
        if (lineLen >= (int)sizeof(line)) {
            // The length reported to the user must exclude the CR of a CRLF
            // terminator — that byte belongs to the line ending, not to the
            // line that has to be shortened in the editor.
            int reportLen = lineLen;
            if (reportLen > 0 && pos[reportLen - 1] == '\r') reportLen--;
            char preview[65];
            int pv = (reportLen < 64) ? reportLen : 64;
            memcpy(preview, pos, pv);
            preview[pv] = '\0';
            LogF("WARNING: config line %d is %d bytes (max %d), truncated: %s",
                 lineNo, reportLen, (int)sizeof(line) - 1, preview);
            lineLen = sizeof(line) - 1;
        }
        memcpy(line, pos, lineLen);
        line[lineLen] = '\0';
        pos = *eol ? eol + 1 : eol;

        TrimRight(line);
        // Blank lines and comments (leading whitespace tolerated) are skipped
        // before the '=' check below: a blank line used to fall through and
        // log "WARNING: config line has no '='" once per blank line in the cfg.
        char* skip = line;
        while (*skip == ' ' || *skip == '\t') skip++;
        if (!*skip || *skip == ';' || *skip == '#') continue;
        sawDataLine = TRUE;
        if (skip != line) memmove(line, skip, strlen(skip) + 1);
        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (close) *close = '\0';
            else LogF("WARNING: section header without ']': %s", line);
            WarnTruncate("section name", line + 1, sizeof(sectionName));
            strncpy_s(sectionName, sizeof(sectionName), line + 1, _TRUNCATE);
            inAudioSection = (_stricmp(sectionName, "audio") == 0);
            inExceptionSection = (_stricmp(sectionName, "exception") == 0);
            inLogSection = (_stricmp(sectionName, "log") == 0);
            inExceptionMix = FALSE;
            currentExceptionIdx = -1;
            sectionSeen = TRUE;

            // `[]` leaves sectionName empty: none of the section flags match
            // and the guard below skips the per-mix check, so every key that
            // follows used to be dropped without a trace.
            if (!sectionName[0])
                LogF("WARNING: empty section name [], keys under it are ignored");

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
                if (!inExceptionMix) {
                    // Per-mix sections only bind to an [exception] entry, so a
                    // section placed before [exception] (or one that was never
                    // declared) used to be dropped without a trace.
                    LogF("WARNING: section [%s] ignored — no matching entry in [exception] "
                         "(declare [exception] entries first)", sectionName);
                }
            }
            continue;
        }
        char* eq = strchr(line, '=');
        if (!eq) {
            LogF("WARNING: config line has no '=', ignored: %s", line);
            continue;
        }
        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        TrimRight(k);
        while (*v == ' ' || *v == '\t') v++;
        TrimRight(v);
        StripInlineComment(v);

        // A `key = value` above the first `[section]` header was matched
        // against nothing at all and dropped without a trace — users who put
        // `enabled = true` at the top of the file saw no effect and no clue.
        if (!sectionSeen) {
            LogF("WARNING: '%s = %s' appears before any [section] header and is ignored",
                 k, v);
            continue;
        }

        if (inLogSection) {
            if (_stricmp(k, "enabled") == 0)
                g_logEnabled = ParseBoolValue(v, g_logEnabled, "enabled", TRUE);
            else if (_stricmp(k, "wait") == 0)
                g_logWait = ParseBoolValue(v, g_logWait, "wait", TRUE);
            else
                LogF("WARNING: unknown [log] key '%s' (known: enabled, wait)", k);
        }

        if (inExceptionSection && g_exceptionCount < MAX_EXCEPTION_MIXES) {
            if (!k[0] || !v[0]) {
                LogF("WARNING: [exception] entry needs 'name = mix', ignored: %s=%s", k, v);
                continue;
            }
            char entry[MAX_PATH];
            WarnTruncate("exception entry", v, sizeof(entry));
            strncpy_s(entry, sizeof(entry), v, _TRUNCATE);
            char* bar = strchr(entry, '|');
            char* base = NULL;
            if (bar) {
                *bar = '\0';
                base = bar + 1;
                while (*base == ' ' || *base == '\t') base++;
            }
            TrimRight(entry);
            if (!entry[0]) continue;
            ExceptionEntry* exEntry = &g_exceptions[g_exceptionCount];
            strncpy_s(exEntry->mixName, sizeof(exEntry->mixName), entry, _TRUNCATE);
            exEntry->baseDir[0] = '\0';
            exEntry->mapCount = 0;
            if (base) {
                TrimRight(base);
                if (base[0]) {
                    WarnTruncate("exception base dir", base, sizeof(exEntry->baseDir));
                    strncpy_s(exEntry->baseDir, sizeof(exEntry->baseDir), base, _TRUNCATE);
                    LogF("  Exception: %s (base=%s)", entry, exEntry->baseDir);
                } else {
                    LogF("  Exception: %s", entry);
                }
            } else {
                LogF("  Exception: %s", entry);
            }
            g_exceptionCount++;
        } else if (inExceptionSection && g_exceptionCount >= MAX_EXCEPTION_MIXES) {
            if (!g_warnedExLimit) {
                g_warnedExLimit = TRUE;
                LogF("WARNING: Exception section limit reached (%d), skipping further entries", MAX_EXCEPTION_MIXES);
            }
        }

        if (inExceptionMix && currentExceptionIdx >= 0) {
            if (!k[0] || !v[0]) {
                LogF("WARNING: [%s] map needs 'bik = wav', ignored: %s=%s", sectionName, k, v);
                continue;
            }
            ExceptionEntry* ex = &g_exceptions[currentExceptionIdx];
            if (ex->mapCount < MAX_MAPS_PER_MIX) {
                WarnTruncate("map .bik name", k, sizeof(ex->maps[ex->mapCount].bikName));
                WarnTruncate("map audio path", v, sizeof(ex->maps[ex->mapCount].wavPath));
                strncpy_s(ex->maps[ex->mapCount].bikName, sizeof(ex->maps[ex->mapCount].bikName), k, _TRUNCATE);
                strncpy_s(ex->maps[ex->mapCount].wavPath, sizeof(ex->maps[ex->mapCount].wavPath), v, _TRUNCATE);
                LogF("  Exception map [%s]: %s -> %s", ex->mixName, k, v);
                ex->mapCount++;
            } else if (warnedMapLimitMix != currentExceptionIdx) {
                warnedMapLimitMix = currentExceptionIdx;
                LogF("WARNING: Exception map limit reached (%d) for [%s], skipping further entries",
                     MAX_MAPS_PER_MIX, ex->mixName);
            }
        }

        if (inAudioSection && g_audioMapCount < MAX_AUDIO_MAPS) {
            if (!k[0] || !v[0]) {
                LogF("WARNING: [audio] entry needs 'bik = wav', ignored: %s=%s", k, v);
                continue;
            }
            WarnTruncate("audio .bik name", k, sizeof(g_audioMaps[g_audioMapCount].bikName));
            WarnTruncate("audio path", v, sizeof(g_audioMaps[g_audioMapCount].wavPath));
            strncpy_s(g_audioMaps[g_audioMapCount].bikName, sizeof(g_audioMaps[g_audioMapCount].bikName), k, _TRUNCATE);
            strncpy_s(g_audioMaps[g_audioMapCount].wavPath, sizeof(g_audioMaps[g_audioMapCount].wavPath), v, _TRUNCATE);
            LogF("  Audio map: %s -> %s", k, v);
            g_audioMapCount++;
        } else if (inAudioSection && g_audioMapCount >= MAX_AUDIO_MAPS) {
            if (!g_warnedAudioLimit) {
                g_warnedAudioLimit = TRUE;
                LogF("WARNING: Audio map limit reached (%d), skipping further entries", MAX_AUDIO_MAPS);
            }
        }
    }
    // Comments and blank lines only: the same situation as an empty cfg —
    // a file the user fills in later must still be picked up, so publish
    // "idle" instead of latching "loaded" and dropping the real content that
    // arrives on the next attempt.
    if (!sawDataLine) {
        if (!g_warnedEmptyCfg) {
            g_warnedEmptyCfg = TRUE;
            LogF("Config is empty: %s (audio replacement off until it has content)", cfgPath);
        }
        free(fileBuf);
        InterlockedExchange(&g_audioConfigLoaded, 0);
        return;
    }
    free(fileBuf);
    LogF("Config loaded: %d audio maps, %d exceptions, log_wait=%d from %s",
         g_audioMapCount, g_exceptionCount, g_logWait, cfgPath);
    InterlockedExchange(&g_audioConfigLoaded, 2);
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

    // Bink markers: 'BIK' + one revision byte.
    // Compare bytes directly — multi-char literals are unreliable across compilers.
    //
    // Bink 1 revisions found in the wild are b, d, f, g, h, i and k (vgmstream
    // and ffmpeg's demuxer agree; 'j' is Bink 2 only). Only f..i were accepted
    // for years, so a BIKb/BIKd/BIKk movie was reported as "no dimensions"
    // and never scaled. The first 44 bytes — everything this reader consumes —
    // are laid out identically for all of them; the extra colour-flags dword
    // that BIKk adds starts at 0x2C, past the end of this buffer.
    if (!(hdr[0] == 0x42 && hdr[1] == 0x49 && hdr[2] == 0x4B)) {
        return info;
    }
    {
        static const unsigned char kBink1Revs[] = {
            0x62, 0x64, 0x66, 0x67, 0x68, 0x69, 0x6B  // b d f g h i k
        };
        BOOL revOk = FALSE;
        for (int i = 0; i < (int)(sizeof(kBink1Revs) / sizeof(kBink1Revs[0])); i++) {
            if ((unsigned char)hdr[3] == kBink1Revs[i]) { revOk = TRUE; break; }
        }
        if (!revOk) return info;
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

static uint32_t Crc32Update(uint32_t crc, const void* data, int len) {
    const uint8_t* p = (const uint8_t*)data;
    for (int i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
    }
    return crc;
}

uint32_t MixCrc32(const char* name) {
    // The game hashes the whole name (ReSource ComputeId has no length cap),
    // so the bytes are streamed through in 64-byte chunks instead of a
    // 256-byte stack buffer that silently hashed only the first 255 chars.
    // Uppercase + TS padding (length byte, then the boundary char up to a
    // multiple of 4) are identical to the previous implementation.
    size_t len = strlen(name);
    uint32_t crc = 0xFFFFFFFF;
    char buf[64];
    size_t done = 0;
    while (len - done > sizeof(buf)) {
        for (size_t i = 0; i < sizeof(buf); i++) {
            char c = name[done + i];
            if (c >= 'a' && c <= 'z') c -= 32;
            buf[i] = c;
        }
        crc = Crc32Update(crc, buf, (int)sizeof(buf));
        done += sizeof(buf);
    }
    int rest = (int)(len - done);
    for (int i = 0; i < rest; i++) {
        char c = name[done + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        buf[i] = c;
    }
    int outLen = rest;
    if (len & 3) {
        int padTotal = 4 - (int)(len & 3);
        char fill = name[len & ~3];
        if (fill >= 'a' && fill <= 'z') fill -= 32;
        buf[rest] = (char)(len & 3);
        for (int i = 1; i < padTotal; i++)
            buf[rest + i] = fill;
        outLen = rest + padTotal;
    }

    return ~Crc32Update(crc, buf, outLen);
}

MixArchive g_mixCache[8];
int g_mixCacheCount = 0;

static void InitMixCs() {
    if (InterlockedCompareExchange(&g_mixCsOnce, 1, 0) == 0) {
        InitializeCriticalSection(&g_mixCs);
        InterlockedExchange(&g_mixCsOnce, 2);
    } else {
        while (InterlockedCompareExchange(&g_mixCsOnce, 2, 2) != 2) { SwitchToThread(); }
    }
}

static void MixLock() {
    InitMixCs();
    EnterCriticalSection(&g_mixCs);
}

static void MixUnlock() {
    LeaveCriticalSection(&g_mixCs);
}

// An LMD name comes straight out of the .mix bytes: CR/LF in it would split
// the log line and forge entries ("LMD resolved: CRC=..."), so control bytes
// are replaced before the name is stored — storage is what the log (and every
// later lookup) prints. A name with control bytes is malformed anyway.
static void SanitizeMixName(const char* src, char* dst, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 0x20 || c == 0x7F) ? '?' : (char)c;
    }
    dst[i] = '\0';
}

static MixArchive* ParseMixFileUnlocked(const char* mixPath) {
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

    uint8_t prefix[4];
    DWORD read;
    if (!ReadFile(hFile, prefix, 4, &read, NULL) || read < 4) {
        CloseHandle(hFile); return NULL;
    }

    uint16_t firstField = ReadU16(prefix);
    uint16_t flags = ReadU16(prefix + 2);
    BOOL encrypted = (firstField == 0 && (flags & 2) != 0);

    uint16_t fileCount = 0;
    uint32_t hashTableOffset = 0;
    uint32_t bodyOffset = 0;
    uint8_t* hashTable = NULL;
    uint32_t hashTableSize = 0;
    uint8_t* encBuf = NULL;

    if (encrypted) {
        // Extended encrypted: key_source(80) @4, Blowfish-ECB header+index @84
        LogF("ParseMixFile: encrypted extended (flags=0x%04X)", flags);
        if (fileSize < 4 + 80 + 8) {
            LogF("ParseMixFile: encrypted mix too small");
            CloseHandle(hFile); return NULL;
        }

        uint8_t key_source[80];
        if (!ReadFile(hFile, key_source, 80, &read, NULL) || read != 80) {
            LogF("ParseMixFile: failed to read key_source");
            CloseHandle(hFile); return NULL;
        }

        uint8_t bf_key[56];
        MixComputeBlowfishKey(key_source, (uint32_t)sizeof(key_source), bf_key);
        MixBlowfishCtx bf;
        MixBlowfishInit(&bf, bf_key, 56);

        const uint32_t encStart = 84;
        uint8_t firstBlock[8];
        if (!ReadFile(hFile, firstBlock, 8, &read, NULL) || read != 8) {
            LogF("ParseMixFile: failed to read encrypted header");
            CloseHandle(hFile); return NULL;
        }
        MixBlowfishDecipherBlock(&bf, firstBlock);

        fileCount = ReadU16(firstBlock);
        LogF("ParseMixFile: encrypted fileCount=%u", fileCount);
        if (fileCount == 0) {
            LogF("ParseMixFile: invalid encrypted fileCount, aborting");
            CloseHandle(hFile); return NULL;
        }

        // plaintext: count(2)+body_size(4)+index(count*12), pad to 8
        uint32_t totalEnc = 6 + (uint32_t)fileCount * 12;
        uint32_t padded = (totalEnc + 7u) & ~7u;
        if ((uint64_t)encStart + padded > fileSize) {
            LogF("ParseMixFile: encrypted header extends past EOF");
            CloseHandle(hFile); return NULL;
        }

        encBuf = (uint8_t*)malloc(padded);
        if (!encBuf) { CloseHandle(hFile); return NULL; }
        // firstBlock already decrypted — keep it; decrypt only remaining blocks
        memcpy(encBuf, firstBlock, 8);
        if (padded > 8) {
            if (!ReadFile(hFile, encBuf + 8, padded - 8, &read, NULL) || read != padded - 8) {
                LogF("ParseMixFile: failed to read encrypted index");
                free(encBuf);
                CloseHandle(hFile); return NULL;
            }
            MixBlowfishDecipher(&bf, encBuf + 8, (int)(padded - 8));
        }

        fileCount = ReadU16(encBuf);
        if (fileCount == 0) {
            LogF("ParseMixFile: invalid fileCount after decrypt");
            free(encBuf);
            CloseHandle(hFile); return NULL;
        }

        hashTableOffset = encStart + 6;
        hashTableSize = (uint32_t)fileCount * 12;
        bodyOffset = encStart + padded;
        hashTable = encBuf + 6; // borrow; freed after copy (encBuf)
    } else {
        uint8_t hdr[14];
        memcpy(hdr, prefix, 4);
        if (!ReadFile(hFile, hdr + 4, 10, &read, NULL) || read < 10) {
            CloseHandle(hFile); return NULL;
        }

        LogF("ParseMixFile: header bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7],
             hdr[8], hdr[9], hdr[10], hdr[11], hdr[12], hdr[13]);

        // @0-1 must be 0x0000 — the "extended" signature. Anything else means
        // a legacy TS/TD mix (count@0) or garbage: the index would be read
        // from the wrong place and every entry would be noise. Previously only
        // fileCount==0 was rejected, so MixInvalidMagic passed by luck.
        if (firstField != 0) {
            LogF("ParseMixFile: not an extended .mix (signature 0x%04X), refusing", firstField);
            CloseHandle(hFile); return NULL;
        }

        fileCount = ReadU16(hdr + 4);
        LogF("ParseMixFile: fileCount=%u", fileCount);
        if (fileCount == 0) {
            LogF("ParseMixFile: invalid fileCount, aborting");
            CloseHandle(hFile); return NULL;
        }

        hashTableSize = fileCount * 12;
        hashTableOffset = 0xA;
        if (hashTableOffset + hashTableSize > fileSize) {
            LogF("ParseMixFile: hash table extends past EOF");
            CloseHandle(hFile); return NULL;
        }

        // body_size (@6) is not used for positioning. A value reaching past
        // EOF is *not* grounds for rejection: protection deliberately corrupts
        // header fields (incl. body_size) while the game ignores them, and the
        // per-entry cross-check below already drops entries that do not fit
        // the real file. Warn only — otherwise replacement audio dies on
        // archives the game itself loads fine.
        uint64_t declaredBodyEnd = (uint64_t)hashTableOffset + hashTableSize + ReadU32(hdr + 6);
        if (declaredBodyEnd > fileSize) {
            LogF("ParseMixFile: WARNING: body_size reaches past EOF (ends at %llu, file %u), ignoring",
                 (unsigned long long)declaredBodyEnd, fileSize);
        }

        SetFilePointer(hFile, hashTableOffset, NULL, FILE_BEGIN);

        hashTable = (uint8_t*)malloc(hashTableSize);
        if (!hashTable) { CloseHandle(hFile); return NULL; }

        if (!ReadFile(hFile, hashTable, hashTableSize, &read, NULL) || read != hashTableSize) {
            LogF("ParseMixFile: ReadFile failed for hash table");
            free(hashTable);
            CloseHandle(hFile); return NULL;
        }
        bodyOffset = hashTableOffset + hashTableSize;
    }

    MixArchive* mix = &g_mixCache[g_mixCacheCount];
    if (mix->entries) {
        free(mix->entries);
        mix->entries = NULL;
    }
    memset(mix, 0, sizeof(MixArchive));
    WarnTruncate("cached MIX path", mixPath, sizeof(mix->filePath));
    strncpy_s(mix->filePath, sizeof(mix->filePath), mixPath, _TRUNCATE);
    mix->fileCount = fileCount;
    mix->bodyOffset = bodyOffset;
    mix->encrypted = encrypted ? 1 : 0;

    mix->entries = (MixEntry*)calloc(fileCount, sizeof(MixEntry));
    if (!mix->entries) {
        LogF("ParseMixFile: out of memory for %u entries", fileCount);
        if (encBuf) free(encBuf);
        else free(hashTable);
        CloseHandle(hFile);
        return NULL;
    }

    int logAll = (fileCount <= 32);
    int dropped = 0;
    for (uint16_t i = 0; i < fileCount; i++) {
        uint32_t off = i * 12;
        mix->entries[i].crc = ReadU32(hashTable + off);
        mix->entries[i].offset = ReadU32(hashTable + off + 4);
        mix->entries[i].size = ReadU32(hashTable + off + 8);

        // Cross-check every entry against the real file. Only the LMD path had
        // a 64-bit bounds check before; a bogus offset/size could otherwise put
        // filePos inside a range that ends past EOF and resolve the wrong .bik
        // name — i.e. the wrong audio replacement. size=0 makes both the LMD
        // and the lookup loops skip the entry.
        if (mix->entries[i].size) {
            uint64_t entryEnd = (uint64_t)bodyOffset + mix->entries[i].offset +
                                mix->entries[i].size;
            if (entryEnd > (uint64_t)fileSize) {
                if (dropped < 4)
                    LogF("ParseMixFile: entry %u out of range (offset=%u size=%u), dropping",
                         i, mix->entries[i].offset, mix->entries[i].size);
                mix->entries[i].size = 0;
                dropped++;
            }
        }

        if (logAll) {
            LogF("ParseMixFile: [%u] CRC=0x%08X offset=%u size=%u", i,
                 mix->entries[i].crc, mix->entries[i].offset, mix->entries[i].size);
        }
    }
    if (dropped)
        LogF("ParseMixFile: %d of %u index entries end past EOF", dropped, fileCount);
    if (!logAll) {
        LogF("ParseMixFile: loaded %u index entries (per-entry log skipped)", fileCount);
    }
    if (encBuf) free(encBuf);
    else free(hashTable);

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

    // Open-addressing index crc -> first entry with size>0. Every LMD name
    // used to rescan all fileCount entries, i.e. O(names x fileCount): with
    // 65535 entries and tens of thousands of names each archive open burned
    // seconds of CPU. Table size is a power of two >= 2*fileCount so a probe
    // always finds an empty slot; first match wins, exactly like the scan.
    // On malloc failure the lookup below falls back to the linear scan.
    uint32_t* crcIndex = NULL;
    uint32_t crcIndexSize = 0;

    if (lmdIndex >= 0 && mix->entries[lmdIndex].size > 52) {
        // 64-bit bounds. bodyOffset+offset and offset+size wrap on a corrupt
        // index, which would silently pass the check and read from a bogus
        // file position.
        uint64_t lmdOffset64 = (uint64_t)bodyOffset + mix->entries[lmdIndex].offset;
        uint32_t lmdSize = mix->entries[lmdIndex].size;

        if (lmdOffset64 <= fileSize && lmdOffset64 + lmdSize <= fileSize) {
            uint32_t lmdOffset = (uint32_t)lmdOffset64;
            SetFilePointer(hFile, lmdOffset, NULL, FILE_BEGIN);
            uint8_t* lmdData = (uint8_t*)malloc(lmdSize);
            if (lmdData) {
                if (!ReadFile(hFile, lmdData, lmdSize, &read, NULL) || read != lmdSize) {
                    free(lmdData);
                } else {

                    uint32_t idxSize = 8;
                    while (idxSize < (uint32_t)fileCount * 2) idxSize <<= 1;
                    crcIndex = (uint32_t*)malloc(idxSize * sizeof(uint32_t));
                    if (crcIndex) {
                        crcIndexSize = idxSize;
                        for (uint32_t s = 0; s < idxSize; s++) crcIndex[s] = 0xFFFFFFFFu;
                        for (uint16_t i = 0; i < fileCount; i++) {
                            if (!mix->entries[i].size) continue;
                            uint32_t c = mix->entries[i].crc;
                            uint32_t slot = (c * 2654435761u) & (idxSize - 1);
                            while (crcIndex[slot] != 0xFFFFFFFFu &&
                                   mix->entries[crcIndex[slot]].crc != c)
                                slot = (slot + 1) & (idxSize - 1);
                            if (crcIndex[slot] == 0xFFFFFFFFu) crcIndex[slot] = i;
                        }
                    }

                    const uint8_t* nameStart = lmdData + 52;
                    int remaining = (int)(lmdSize - 52);

                    while (remaining > 0) {
                        const char* name = (const char*)nameStart;
                        int nameLen = (int)strnlen(name, remaining);
                        if (nameLen == 0 || nameLen >= remaining) break;

                        uint32_t computedCrc = MixCrc32(name);
                        uint32_t hit = 0xFFFFFFFFu;

                        if (crcIndex) {
                            uint32_t slot = (computedCrc * 2654435761u) & (crcIndexSize - 1);
                            while (crcIndex[slot] != 0xFFFFFFFFu) {
                                if (mix->entries[crcIndex[slot]].crc == computedCrc) {
                                    hit = crcIndex[slot];
                                    break;
                                }
                                slot = (slot + 1) & (crcIndexSize - 1);
                            }
                        } else {
                            for (uint16_t i = 0; i < fileCount; i++) {
                                if (mix->entries[i].crc == computedCrc && mix->entries[i].size > 0) {
                                    hit = i;
                                    break;
                                }
                            }
                        }

                        if (hit != 0xFFFFFFFFu) {
                            char cleanName[sizeof(mix->entries[hit].name)];
                            WarnTruncate("LMD file name", name, sizeof(mix->entries[hit].name));
                            SanitizeMixName(name, cleanName, sizeof(cleanName));
                            strncpy_s(mix->entries[hit].name, sizeof(mix->entries[hit].name), cleanName, _TRUNCATE);
                            LogF("LMD resolved: CRC=0x%08X -> %s (offset=%u, size=%u)",
                                 computedCrc, cleanName, mix->entries[hit].offset, mix->entries[hit].size);
                        }

                        nameStart += nameLen + 1;
                        remaining -= nameLen + 1;
                    }
                    free(lmdData);
                }
            }
        }
    }
    if (crcIndex) free(crcIndex);

    CloseHandle(hFile);
    mix->valid = 1;
    g_mixCacheCount++;
    LogF("Parsed .mix: %s (%u files, LMD %s, enc=%d)", mixPath, fileCount,
         lmdIndex >= 0 ? "found" : "not found", mix->encrypted);
    return mix;
}

MixArchive* ParseMixFile(const char* mixPath) {
    MixLock();
    MixArchive* r = ParseMixFileUnlocked(mixPath);
    MixUnlock();
    return r;
}

static BOOL FindBikNameInMixUnlocked(const char* mixPath, DWORD filePos, char* outName, int outNameSize) {
    outName[0] = '\0';
    MixArchive* mix = ParseMixFileUnlocked(mixPath);
    if (!mix || !mix->valid) return FALSE;

    uint32_t bodyOffset = mix->bodyOffset;
    uint32_t lmdCrc = 0x366E051F;

    for (uint16_t i = 0; i < mix->fileCount; i++) {
        if (mix->entries[i].crc == lmdCrc) continue;
        if (mix->entries[i].size == 0) continue;

        // 64-bit so a wrapped bodyOffset+offset cannot produce a bogus match.
        uint64_t entryStart = (uint64_t)bodyOffset + mix->entries[i].offset;
        uint64_t entryEnd = entryStart + mix->entries[i].size;

        if ((uint64_t)filePos >= entryStart && (uint64_t)filePos < entryEnd) {
            if (!mix->entries[i].name[0]) return FALSE;
            strncpy_s(outName, outNameSize, mix->entries[i].name, _TRUNCATE);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL FindBikNameInMix(const char* mixPath, DWORD filePos, char* outName, int outNameSize) {
    outName[0] = '\0';
    MixLock();
    BOOL r = FindBikNameInMixUnlocked(mixPath, filePos, outName, outNameSize);
    MixUnlock();
    return r;
}

static BOOL RelFileExists(const char* rel) {
    char full[MAX_PATH];
    if (rel[0] && (rel[1] == ':' || (rel[0] == '\\' && rel[1] == '\\'))) {
        WarnTruncate("auto-wav path", rel, sizeof(full));
        strncpy_s(full, sizeof(full), rel, _TRUNCATE);
    } else {
        // g_dllDir + rel can exceed MAX_PATH; a silent cut produces a wrong
        // path, GetFileAttributes fails and the auto-base is skipped as if
        // the file did not exist — the log must say why.
        if (strlen(g_dllDir) + strlen(rel) >= sizeof(full))
            LogF("WARNING: auto-wav full path longer than %u chars, truncated: %s%s",
                 (unsigned)(sizeof(full) - 1), g_dllDir, rel);
        _snprintf_s(full, sizeof(full), _TRUNCATE, "%s%s", g_dllDir, rel);
    }
    DWORD a = GetFileAttributesA(full);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Auto-resolve baseDir\stem.ogg then .wav (ogg preferred). Returns static buffer (caller copies).
// thread-local so two threads resolving their own videos at the same time
// cannot overwrite each other's auto-base path. The pointer stays valid until
// the next TryAutoWav() call on the same thread — callers (TrackVideo) copy it
// out immediately.
static __declspec(thread) char g_autoWavPath[MAX_PATH];

static const char* TryAutoWav(const ExceptionEntry* ex, const char* fileName) {
    if (!ex || !ex->baseDir[0] || !fileName || !fileName[0]) return NULL;
    char stem[MAX_PATH];
    WarnTruncate("auto-wav file name", fileName, sizeof(stem));
    strncpy_s(stem, sizeof(stem), fileName, _TRUNCATE);
    char* dot = strrchr(stem, '.');
    if (dot && _stricmp(dot, ".bik") == 0) *dot = '\0';
    if (strpbrk(stem, "\\/:")) return NULL;
    size_t bl = strlen(ex->baseDir);
    const char* sep = (bl > 0 && (ex->baseDir[bl - 1] == '\\' || ex->baseDir[bl - 1] == '/')) ? "" : "\\";
    // A truncated path is never found on disk, so the auto-base silently
    // falls through to the original audio — say so instead of guessing.
    size_t stemLen = strlen(stem);
    if (bl + strlen(sep) + stemLen + 4 >= sizeof(g_autoWavPath))
        LogF("WARNING: auto-wav path longer than %u chars, truncated: %s%s%s",
             (unsigned)(sizeof(g_autoWavPath) - 1), ex->baseDir, sep, stem);
    _snprintf_s(g_autoWavPath, sizeof(g_autoWavPath), _TRUNCATE, "%s%s%s.ogg", ex->baseDir, sep, stem);
    if (RelFileExists(g_autoWavPath)) return g_autoWavPath;
    _snprintf_s(g_autoWavPath, sizeof(g_autoWavPath), _TRUNCATE, "%s%s%s.wav", ex->baseDir, sep, stem);
    if (RelFileExists(g_autoWavPath)) return g_autoWavPath;
    return NULL;
}

// Called from the BinkOpen path (under TrackLock) and from tests. It reads
// g_exceptionCount/g_audioMapCount without a lock: the arrays are written only
// by the thread that wins the LoadAudioConfig state machine and published
// after parsing, so no reader ever sees a half-filled config. The only writer
// that runs concurrently with readers would be ResetAudioConfig — that one is
// test-only (see its comment).
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
                const char* autoWav = TryAutoWav(&g_exceptions[i], fileName);
                if (autoWav) {
                    LogF("Exception auto base [%s]: %s -> %s", mixName, fileName, autoWav);
                    return autoWav;
                }
            }
        }
    }

    if (fileName && !mixName) {
        for (int i = 0; i < g_exceptionCount; i++) {
            for (int j = 0; j < g_exceptions[i].mapCount; j++) {
                if (_stricmp(g_exceptions[i].maps[j].bikName, fileName) == 0) {
                    LogF("Exception match (no mix): [%s] %s -> %s",
                         g_exceptions[i].mixName, fileName, g_exceptions[i].maps[j].wavPath);
                    return g_exceptions[i].maps[j].wavPath;
                }
            }
        }
        for (int i = 0; i < g_exceptionCount; i++) {
            const char* autoWav = TryAutoWav(&g_exceptions[i], fileName);
            if (autoWav) {
                LogF("Exception auto base (no mix): [%s] %s -> %s",
                     g_exceptions[i].mixName, fileName, autoWav);
                return autoWav;
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
