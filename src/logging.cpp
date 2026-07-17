#include "binkw32_proxy.h"

// ============================================================================
// logging.cpp — Log subsystem with file rotation
//
// Features:
// - Log file rotation: max 10MB per file, rotates on startup and when exceeded
// - Rotation chain: .log -> .log.1 -> .log.2 -> ... -> .log.10 (oldest deleted)
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

static void InitLogCs() {
    if (InterlockedCompareExchange(&g_logCsOnce, 1, 0) == 0) {
        InitializeCriticalSection(&g_logCs);
        InterlockedExchange(&g_logCsOnce, 2);
    } else {
        while (g_logCsOnce != 2) { SwitchToThread(); }
    }
}

static void RotateLogFile(const char* logPath) {
    // Check current file size
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(logPath, GetFileExInfoStandard, &fad)) return;
    if (fad.nFileSizeHigh > 0 || fad.nFileSizeLow < LOG_MAX_SIZE) return;

    // Rotate: .log -> .log.1 -> .log.2 -> ... -> delete oldest
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

static void OpenLogFile(const char* logPath) {
    RotateLogFile(logPath);
    g_log = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void InitLog() {
    if (!g_logEnabled) return;
    char logPath[MAX_PATH];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", g_dllDir);
    OpenLogFile(logPath);
}

void Log(const char* msg) {
    if (!g_logEnabled) return;
    InitLogCs();
    EnterCriticalSection(&g_logCs);

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
                "=== Proxy_Bink32w v2.0.1 ===\r\n"
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

void TrimRight(char* s) {
    if (!s || !*s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

void ShutdownLog() {
    if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
    if (g_logCsOnce == 2) { DeleteCriticalSection(&g_logCs); InterlockedExchange(&g_logCsOnce, 0); }
}
