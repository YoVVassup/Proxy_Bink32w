#include "binkw32_proxy.h"

// ============================================================================
// Logging subsystem
// ============================================================================

HANDLE g_log = INVALID_HANDLE_VALUE;
BOOL g_logEnabled = TRUE;
char g_dllDir[MAX_PATH] = {0};

void InitLog() {
    if (!g_logEnabled) return;
    char logPath[MAX_PATH];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", g_dllDir);
    g_log = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void Log(const char* msg) {
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
    }
}

void LogF(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

void TrimRight(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}
