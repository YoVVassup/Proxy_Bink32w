#include "binkw32_proxy.h"

// ============================================================================
// logging.cpp — Log subsystem with file rotation
//
// Features:
// - Log file rotation: max 10MB per file, rotates on startup and when exceeded
// - Rotation chain: .log -> .log.1 -> .log.2 -> ... -> .log.9 (oldest .log.9 deleted)
// - Thread-safe via CRITICAL_SECTION
// - Auto-flush via FlushFileBuffers
// - Header shows version and target Bink DLL name (from BINK_REAL_DLL)
//
// Public API:
//   InitLog()  — Called lazily on first Log() call
//   Log(msg)   — Write a message to the log file
//   LogF(fmt, ...) — Formatted log write
// ============================================================================

HANDLE g_log = INVALID_HANDLE_VALUE;
BOOL g_logEnabled = TRUE;
static BOOL g_logHeaderWritten = FALSE;
char g_dllDir[MAX_PATH] = {0};
static CRITICAL_SECTION g_logCs;
static LONG g_logCsOnce = 0;
static const DWORD LOG_MAX_SIZE = 10 * 1024 * 1024; // 10 MB

// Open-failure state. `g_logOpenFailed` doubles as the "already warned" latch:
// CreateFileA failing on every Log() must not re-emit the warning (and, see
// OpenLogFile, must not re-run the rotation chain) on every call.
static BOOL g_logOpenFailed = FALSE;
static int  g_logOpenWarnCount = 0;
static char g_logOpenWarnMsg[512] = {0};
// Path whose startup rotation was already attempted in this process, so a
// mid-session reopen only does the size-gated rotation.
static char g_logPreservedPath[MAX_PATH] = {0};

static void InitLogCs() {
    if (InterlockedCompareExchange(&g_logCsOnce, 1, 0) == 0) {
        InitializeCriticalSection(&g_logCs);
        InterlockedExchange(&g_logCsOnce, 2);
    } else {
        while (InterlockedCompareExchange(&g_logCsOnce, 2, 2) != 2) { SwitchToThread(); }
    }
}

// Shift .log -> .log.1 -> .log.2 -> ... -> .log.9 (oldest dropped)
// unconditionally. No size gate: startup rotation must preserve the previous
// run's log whatever its size.
static void RotateChain(const char* logPath) {
    for (int i = 9; i >= 1; i--) {
        char oldPath[MAX_PATH], newPath[MAX_PATH];
        _snprintf_s(oldPath, sizeof(oldPath), _TRUNCATE, "%s.%d", logPath, i);
        _snprintf_s(newPath, sizeof(newPath), _TRUNCATE, "%s.%d", logPath, i + 1);
        if (i == 9) {
            DeleteFileA(oldPath); // Delete oldest
        } else {
            MoveFileExA(oldPath, newPath, MOVEFILE_REPLACE_EXISTING);
        }
    }
    // Rotate current to .1
    char newPath[MAX_PATH];
    _snprintf_s(newPath, sizeof(newPath), _TRUNCATE, "%s.1", logPath);
    MoveFileExA(logPath, newPath, MOVEFILE_REPLACE_EXISTING);
}

#ifdef BINK_TEST_BUILD
void RotateLogFile(const char* logPath) {
#else
static void RotateLogFile(const char* logPath) {
#endif
    // Size gate: mid-session rotation only fires for an overflowed log.
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(logPath, GetFileExInfoStandard, &fad)) return;
    if (fad.nFileSizeHigh > 0 || fad.nFileSizeLow < LOG_MAX_SIZE) return;
    RotateChain(logPath);
}

static void WarnLogOpenFailed(const char* logPath, DWORD err) {
    if (g_logOpenFailed) return; // one warning per failure, not one per Log()
    g_logOpenFailed = TRUE;
    g_logOpenWarnCount++;
    _snprintf_s(g_logOpenWarnMsg, sizeof(g_logOpenWarnMsg), _TRUNCATE,
                "WARNING: cannot open log file %s (error %u); log output is lost\r\n",
                logPath, (unsigned)err);
    // Never route this through Log(): the log itself is the thing that failed.
    OutputDebugStringA(g_logOpenWarnMsg);
    fputs(g_logOpenWarnMsg, stderr);
}

static void OpenLogFile(const char* logPath) {
    BOOL samePath = (_stricmp(g_logPreservedPath, logPath) == 0);
    if (!samePath) {
        strncpy_s(g_logPreservedPath, sizeof(g_logPreservedPath), logPath, _TRUNCATE);
        // The failure state belongs to the previous path: a stale latch must
        // not swallow the warning for a brand-new one.
        g_logOpenFailed = FALSE;
    }

    // Rotate before CREATE_ALWAYS, otherwise the previous run's log is
    // truncated away (README: "rotates automatically on startup"). Skipped
    // once this very path has failed to open: re-running the chain shift on
    // every Log() would push the whole history past .log.9 while the file
    // stays unwritable.
    if (!(g_logOpenFailed && samePath)) {
        if (samePath) {
            RotateLogFile(logPath); // mid-session reopen: size-gated
        } else {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExA(logPath, GetFileExInfoStandard, &fad) &&
                (fad.nFileSizeHigh != 0 || fad.nFileSizeLow != 0))
                RotateChain(logPath); // startup: preserve a non-empty log
        }
    }

    g_log = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE)
        WarnLogOpenFailed(logPath, GetLastError());
    else
        g_logOpenFailed = FALSE;
}

void InitLog() {
    if (!g_logEnabled) return;
    InitLogCs();
    EnterCriticalSection(&g_logCs);
    // Re-check under the lock: ShutdownLog() may have run while we waited —
    // opening the file after teardown would leak the handle at FreeLibrary.
    if (g_logEnabled && g_log == INVALID_HANDLE_VALUE) {
        char logPath[MAX_PATH];
        _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", g_dllDir);
        OpenLogFile(logPath);
    }
    LeaveCriticalSection(&g_logCs);
}

void Log(const char* msg) {
    if (!g_logEnabled) return;
    InitLogCs();
    EnterCriticalSection(&g_logCs);

    // Re-check under the lock. The outer test above is only a fast path: a
    // thread that passed it before ShutdownLog()/DllMain flipped the flag must
    // not re-open the file here — that resurrects the log after teardown and
    // leaks the handle at FreeLibrary (the shutdown path can no longer close
    // it, it has already returned).
    if (!g_logEnabled) {
        LeaveCriticalSection(&g_logCs);
        return;
    }

    if (g_log == INVALID_HANDLE_VALUE) {
        char logPath[MAX_PATH];
        _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", g_dllDir);
        OpenLogFile(logPath);
    }

    if (g_log != INVALID_HANDLE_VALUE) {
        if (!g_logHeaderWritten) {
            DWORD bw;
            char header[256];
            _snprintf_s(header, sizeof(header), _TRUNCATE,
                "=== Proxy_Bink32w v2.1.0 ===\r\n"
                "Target: %s\r\n"
                "\r\n",
                BINK_REAL_DLL);
            WriteFile(g_log, header, (DWORD)strlen(header), &bw, NULL);
            g_logHeaderWritten = TRUE;
        }

        DWORD bw;
        SetFilePointer(g_log, 0, NULL, FILE_END);
        WriteFile(g_log, msg, (DWORD)strlen(msg), &bw, NULL);
        WriteFile(g_log, "\r\n", 2, &bw, NULL);
        FlushFileBuffers(g_log);

        // Check if rotation needed
        DWORD sizeHigh = 0;
        DWORD sizeLow = GetFileSize(g_log, &sizeHigh);
        if (sizeLow != INVALID_FILE_SIZE && (sizeHigh > 0 || sizeLow >= LOG_MAX_SIZE)) {
            char logPath[MAX_PATH];
            _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", g_dllDir);
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
            g_logHeaderWritten = FALSE;
            OpenLogFile(logPath);
        }
    }

    LeaveCriticalSection(&g_logCs);
}

void LogF(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

// `_TRUNCATE` used to swallow over-long values without a trace.
void WarnTruncate(const char* what, const char* value, size_t cap) {
    if (value && strlen(value) >= cap)
        LogF("WARNING: %s longer than %u chars, truncated: %s",
             what, (unsigned)(cap - 1), value);
}

void TrimRight(char* s) {
    if (!s || !*s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

void ShutdownLog() {
    // `g_logCs` is initialized once and never deleted. Deleting it here (and
    // resetting g_logCsOnce) while another thread is inside Log() would let that
    // thread re-enter freed memory, and would let a later Log() re-initialise a
    // half-torn-down log. Turning logging off first also stops Log() from
    // re-opening the file — DllMain calls us under the loader lock.
    g_logEnabled = FALSE;
    InitLogCs();

    // Never block unboundedly on g_logCs: this runs under the loader lock while
    // another thread may be inside WriteFile/FlushFileBuffers, and that thread
    // may itself be waiting for the loader (deadlock). Bounded wait instead
    // (50 ms, yielding with SwitchToThread): close the handle in the normal
    // case, and if a writer still holds the lock after the timeout, leave the
    // handle open — one leaked handle during unload beats a hang. The flag is
    // already FALSE, so nobody re-opens it.
    BOOL locked = TryEnterCriticalSection(&g_logCs);
    DWORD start = GetTickCount();
    while (!locked && GetTickCount() - start < 50) {
        SwitchToThread();
        locked = TryEnterCriticalSection(&g_logCs);
    }
    if (!locked) {
        // A writer is mid-write; skip the close (documented residual risk).
        return;
    }
    if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
    g_logHeaderWritten = FALSE;
    LeaveCriticalSection(&g_logCs);
}

#ifdef BINK_TEST_BUILD
// Test-only access to g_logCs: lets a test hold the lock so a concurrent
// Log() passes the outer g_logEnabled check and then blocks on it.
void LogLockForTest() { InitLogCs(); EnterCriticalSection(&g_logCs); }
void LogUnlockForTest() { LeaveCriticalSection(&g_logCs); }

// Test-only view of the open-failure warning (see WarnLogOpenFailed).
const char* LogOpenFailureForTest() { return g_logOpenWarnMsg; }
int LogOpenWarnCountForTest() { return g_logOpenWarnCount; }
void LogResetOpenFailureForTest() {
    g_logOpenFailed = FALSE;
    g_logOpenWarnCount = 0;
    g_logOpenWarnMsg[0] = '\0';
}
#endif
