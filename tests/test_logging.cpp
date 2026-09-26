#include <gtest/gtest.h>
#include "test_helpers.h"
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>

// ============================================================================
// Tests for logging subsystem
//
// Tests Log(), LogF(), file creation, header, formatting.
//
// Note: g_logHeaderWritten and g_logCsOnce are static — cannot reset between
// tests. Use a single test with temp directory.
// ============================================================================

class LoggingTest : public ::testing::Test {
protected:
    char tempDir[MAX_PATH];
    char savedDllDir[MAX_PATH];

    void SetUp() override {
        memcpy(savedDllDir, g_dllDir, MAX_PATH);

        char tmpBase[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpBase);
        _snprintf_s(tempDir, sizeof(tempDir), _TRUNCATE, "%sbink32w_log_test_%d\\",
                     tmpBase, GetTickCount());
        CreateDirectoryA(tempDir, NULL);

        lstrcpynA(g_dllDir, tempDir, MAX_PATH);
        g_log = INVALID_HANDLE_VALUE;
        g_logEnabled = TRUE;
    }

    void TearDown() override {
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
        lstrcpynA(g_dllDir, savedDllDir, MAX_PATH);

        char pattern[MAX_PATH];
        _snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s*", tempDir);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char filePath[MAX_PATH];
                _snprintf_s(filePath, sizeof(filePath), _TRUNCATE, "%s%s", tempDir, fd.cFileName);
                DeleteFileA(filePath);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        RemoveDirectoryA(tempDir);
    }

    std::string GetLogPath() {
        return std::string(tempDir) + "binkw32_proxy.log";
    }

    std::string ReadLogFile() {
        // Close log handle first to flush writes
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
        std::string path = GetLogPath();
        FILE* f = NULL;
        fopen_s(&f, path.c_str(), "rb");
        if (!f) return "";
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string content(size, '\0');
        if (size > 0) fread(&content[0], 1, size, f);
        fclose(f);
        // Reopen log handle for subsequent writes
        g_log = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        return content;
    }

    DWORD GetLogFileSize() {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(GetLogPath().c_str(), GetFileExInfoStandard, &fad))
            return 0;
        return fad.nFileSizeLow;
    }

    // Reads the log without touching g_log (ReadLogFile re-opens it, which
    // would hide the "handle must stay closed after shutdown" assertion).
    std::string ReadLogFileRaw() {
        std::string path = GetLogPath();
        FILE* f = NULL;
        fopen_s(&f, path.c_str(), "rb");
        if (!f) return "";
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string content(size > 0 ? (size_t)size : 0, '\0');
        if (size > 0) fread(&content[0], 1, size, f);
        fclose(f);
        return content;
    }
};

TEST_F(LoggingTest, ComprehensiveLoggingTest) {
    // === 1. Log creates file and writes message ===
    Log("test message 1");
    std::string content = ReadLogFile();
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("test message 1"), std::string::npos);

    // === 2. Multiple lines ===
    Log("test message 2");
    Log("test message 3");
    content = ReadLogFile();
    EXPECT_NE(content.find("test message 2"), std::string::npos);
    EXPECT_NE(content.find("test message 3"), std::string::npos);

    // === 3. Line endings are \r\n ===
    EXPECT_NE(content.find("test message 1\r\n"), std::string::npos);

    // === 4. LogF formatting ===
    LogF("number: %d", 42);
    content = ReadLogFile();
    EXPECT_NE(content.find("number: 42"), std::string::npos);

    LogF("hex: 0x%08X", 0xDEADBEEF);
    content = ReadLogFile();
    EXPECT_NE(content.find("hex: 0xDEADBEEF"), std::string::npos);

    LogF("name: %s", "test.bik");
    content = ReadLogFile();
    EXPECT_NE(content.find("name: test.bik"), std::string::npos);

    LogF("file %s size %u", "a01.bik", 12345);
    content = ReadLogFile();
    EXPECT_NE(content.find("file a01.bik size 12345"), std::string::npos);

    LogF("ptr: %p", (void*)0x12345678);
    content = ReadLogFile();
    EXPECT_NE(content.find("ptr:"), std::string::npos);

    // === 5. LogF truncation (1024 byte buffer) ===
    char longMsg[2048];
    memset(longMsg, 'A', sizeof(longMsg) - 1);
    longMsg[sizeof(longMsg) - 1] = '\0';
    LogF("%s", longMsg);
    DWORD fileSizeAfterTrunc = GetLogFileSize();
    // File should contain header (if first run) + messages, but each LogF is truncated
    EXPECT_LT(fileSizeAfterTrunc, 8192u);

    // === 6. File size increases with writes ===
    DWORD sizeBefore = GetLogFileSize();
    for (int i = 0; i < 10; i++) {
        LogF("iteration %d", i);
    }
    DWORD sizeAfter = GetLogFileSize();
    EXPECT_GT(sizeAfter, sizeBefore);

    // === 7. Log disabled ===
    g_logEnabled = FALSE;
    DWORD sizeBeforeDisabled = GetLogFileSize();
    Log("should not appear");
    LogF("should not appear %d", 1);
    DWORD sizeAfterDisabled = GetLogFileSize();
    EXPECT_EQ(sizeBeforeDisabled, sizeAfterDisabled);
    g_logEnabled = TRUE;

    // === 8. ShutdownLog ===
    Log("before shutdown");
    EXPECT_NE(g_log, INVALID_HANDLE_VALUE);
    ShutdownLog();
    EXPECT_EQ(g_log, INVALID_HANDLE_VALUE);
    ShutdownLog(); // idempotent
}

// After ShutdownLog() nothing may bring the log file back: g_logEnabled=FALSE
// (outer check) and, for a writer that passed that check before the flag
// flipped, the re-check under g_logCs.
TEST_F(LoggingTest, ShutdownLogPreventsReopen) {
    g_logEnabled = TRUE;
    Log("before shutdown");
    ShutdownLog();
    ASSERT_EQ(g_log, INVALID_HANDLE_VALUE);
    ASSERT_EQ(g_logEnabled, FALSE);

    Log("after shutdown");
    LogF("after shutdown %d", 42);

    EXPECT_EQ(g_log, INVALID_HANDLE_VALUE) << "Log() re-opened the file after shutdown";
    EXPECT_EQ(ReadLogFileRaw().find("after shutdown"), std::string::npos)
        << "log written after ShutdownLog";
}

// The race the audit describes: a thread passes the outer g_logEnabled check,
// then ShutdownLog() runs. Two properties are checked here:
//  1. ShutdownLog() must not block on g_logCs (it runs under the loader lock
//     while a writer may sit in WriteFile/FlushFileBuffers);
//  2. the writer must not re-open the file after the flag has flipped.
TEST_F(LoggingTest, ShutdownLogDoesNotBlockAndWriterDoesNotReopen) {
    g_logEnabled = TRUE;
    Log("warmup");
    ShutdownLog();
    ASSERT_EQ(g_log, INVALID_HANDLE_VALUE);
    ASSERT_EQ(g_logEnabled, FALSE);

    // Re-arm the flag and hold the lock, so the writer passes the outer check
    // and then blocks inside Log() waiting for g_logCs.
    g_logEnabled = TRUE;
    LogLockForTest();

    std::atomic<bool> writerDone{false};
    std::thread writer([&] {
        Log("racing message");
        writerDone = true;
    });
    Sleep(150); // give the writer time to reach (and block on) the lock

    std::atomic<bool> shutdownDone{false};
    std::thread shutdown([&] {
        ShutdownLog();
        shutdownDone = true;
    });

    for (int i = 0; i < 100 && !shutdownDone; i++) Sleep(50); // up to 5 s
    EXPECT_TRUE(shutdownDone.load()) << "ShutdownLog blocked on g_logCs";

    LogUnlockForTest();
    shutdown.join();
    writer.join();

    EXPECT_EQ(g_logEnabled, FALSE);
    EXPECT_EQ(g_log, INVALID_HANDLE_VALUE) << "writer re-opened the log after shutdown";
    EXPECT_EQ(ReadLogFileRaw().find("racing message"), std::string::npos)
        << "a message was written after ShutdownLog";
}

// ============================================================================
// RotateLogFile tests
// ============================================================================

TEST_F(LoggingTest, RotateLogFileNoopWhenSmallFile) {
    std::string logPath = GetLogPath();

    // Create a small log file (< 10MB)
    FILE* f = NULL;
    fopen_s(&f, logPath.c_str(), "w");
    ASSERT_NE(f, (FILE*)NULL);
    fprintf(f, "small log\n");
    fclose(f);

    RotateLogFile(logPath.c_str());

    // File should remain unchanged
    EXPECT_TRUE(GetFileAttributesA(logPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    // No .1 file should be created
    std::string rotatedPath = logPath + ".1";
    EXPECT_FALSE(GetFileAttributesA(rotatedPath.c_str()) != INVALID_FILE_ATTRIBUTES);
}

TEST_F(LoggingTest, RotateLogFileCreatesChain) {
    std::string logPath = GetLogPath();

    auto readFile = [](const std::string& p) -> std::string {
        FILE* f = NULL;
        fopen_s(&f, p.c_str(), "rb");
        if (!f) return "";
        std::string content;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
        fclose(f);
        return content;
    };

    // Create .log.1 through .log.9 with distinct content
    for (int i = 1; i <= 9; i++) {
        std::string path = logPath + "." + std::to_string(i);
        FILE* f = NULL;
        fopen_s(&f, path.c_str(), "w");
        if (f) { fprintf(f, "rotated %d\n", i); fclose(f); }
    }

    // Rotation only fires at LOG_MAX_SIZE (10 MB), so build a log of exactly
    // that size instead of asserting on file presence alone.
    {
        FILE* f = NULL;
        fopen_s(&f, logPath.c_str(), "wb");
        ASSERT_NE(f, (FILE*)NULL);
        fprintf(f, "main log\n");           // 9 bytes
        char buf[65536];
        memset(buf, 'x', sizeof(buf));
        for (int i = 0; i < 160; i++) fwrite(buf, 1, sizeof(buf), f);  // 10 MB
        fclose(f);
    }
    ASSERT_EQ(GetLogFileSize(), 10u * 1024 * 1024 + 9u);

    RotateLogFile(logPath.c_str());

    // main -> .log.1, .log.N -> .log.N+1, .log.9 dropped
    auto startsWith = [&readFile](const std::string& p, const char* prefix) {
        std::string content = readFile(p);
        return content.compare(0, strlen(prefix), prefix) == 0;
    };
    EXPECT_EQ(GetFileAttributesA(logPath.c_str()), INVALID_FILE_ATTRIBUTES)
        << "main log must be moved aside by rotation";
    EXPECT_TRUE(startsWith(logPath + ".1", "main log"))
        << "old main content must land in .log.1";
    EXPECT_TRUE(startsWith(logPath + ".2", "rotated 1"));
    EXPECT_TRUE(startsWith(logPath + ".3", "rotated 2"));
    EXPECT_TRUE(startsWith(logPath + ".9", "rotated 8"))
        << "chain must shift up to .log.9";
    EXPECT_EQ(GetFileAttributesA((logPath + ".10").c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(LoggingTest, RotateLogFileHandlesNoExistingLog) {
    std::string logPath = GetLogPath() + "_nonexistent";

    RotateLogFile(logPath.c_str());

    EXPECT_EQ(GetFileAttributesA(logPath.c_str()), INVALID_FILE_ATTRIBUTES);
    EXPECT_EQ(GetFileAttributesA((logPath + ".1").c_str()), INVALID_FILE_ATTRIBUTES)
        << "a missing log must not start a rotation chain";
}
