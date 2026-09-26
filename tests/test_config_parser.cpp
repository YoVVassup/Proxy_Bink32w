#include <gtest/gtest.h>
#include "test_helpers.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <winioctl.h> // FSCTL_SET_SPARSE (huge-config OOM test)

// ============================================================================
// Tests for config parsing (binkw32.cfg format)
//
// Tests the INI parser by creating temp config files and verifying
// that LoadAudioConfig correctly parses them.
//
// The parser supports:
// - [section] headers
// - key = value pairs
// - Comments: ; and #
// - Reserved sections: [audio], [exception], [log]
// - Per-mix exception sections
// ============================================================================

class ConfigParserTest : public ::testing::Test {
protected:
    char tempDir[MAX_PATH];
    char savedDllDir[MAX_PATH];

    void SetUp() override {
        // Save original g_dllDir
        memcpy(savedDllDir, g_dllDir, MAX_PATH);

        // Create temp directory for test files
        GetTempPathA(MAX_PATH, tempDir);
        strcat_s(tempDir, "bink32w_test\\");
        CreateDirectoryA(tempDir, NULL);

        // Set g_dllDir to temp directory so LoadAudioConfig reads from there
        lstrcpynA(g_dllDir, tempDir, MAX_PATH);

        // Reset config state
        ResetAudioConfig();

        // Fresh log per test so warning assertions see only this test's output
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
        char logPath[MAX_PATH];
        _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", tempDir);
        DeleteFileA(logPath);
    }

    void TearDown() override {
        // Close the log first — TearDown deletes every file in tempDir
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }

        // Restore original g_dllDir
        lstrcpynA(g_dllDir, savedDllDir, MAX_PATH);

        // Clean up temp files
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

    void WriteConfig(const char* content) {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbinkw32.cfg", tempDir);
        FILE* f = NULL;
        fopen_s(&f, path, "w");
        ASSERT_NE(f, (FILE*)NULL);
        fwrite(content, 1, strlen(content), f);
        fclose(f);
    }

    std::string ReadConfigLog() {
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
        std::string path = std::string(tempDir) + "binkw32_proxy.log";
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

TEST_F(ConfigParserTest, EmptyConfig) {
    WriteConfig("");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 0);
    EXPECT_EQ(g_exceptionCount, 0);
}

TEST_F(ConfigParserTest, AudioSection) {
    WriteConfig(
        "[audio]\n"
        "test1.bik = test1.wav\n"
        "test2.bik = test2.ogg\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 2);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test1.bik");
    EXPECT_STREQ(g_audioMaps[0].wavPath, "test1.wav");
    EXPECT_STREQ(g_audioMaps[1].bikName, "test2.bik");
    EXPECT_STREQ(g_audioMaps[1].wavPath, "test2.ogg");
}

TEST_F(ConfigParserTest, ExceptionSection) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "1=movies02.mix\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 2);
    EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
    EXPECT_STREQ(g_exceptions[1].mixName, "movies02.mix");
}

TEST_F(ConfigParserTest, ExceptionWithMaps) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "westlogo.bik = BinkWAV\\westlogo.wav\n"
        "a01_f00e.bik = BinkWAV\\a01_f00e.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, 2);
    EXPECT_STREQ(g_exceptions[0].maps[0].bikName, "westlogo.bik");
    EXPECT_STREQ(g_exceptions[0].maps[0].wavPath, "BinkWAV\\westlogo.wav");
    EXPECT_STREQ(g_exceptions[0].maps[1].bikName, "a01_f00e.bik");
    EXPECT_STREQ(g_exceptions[0].maps[1].wavPath, "BinkWAV\\a01_f00e.wav");
}

TEST_F(ConfigParserTest, LogSectionEnabled) {
    WriteConfig(
        "[log]\n"
        "enabled = false\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_logEnabled, FALSE);
}

TEST_F(ConfigParserTest, LogSectionWait) {
    WriteConfig(
        "[log]\n"
        "wait = true\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_logWait, TRUE);
}

TEST_F(ConfigParserTest, CommentLines) {
    WriteConfig(
        "; this is a comment\n"
        "# this is also a comment\n"
        "[audio]\n"
        "; commented out entry\n"
        "test.bik = test.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
}

TEST_F(ConfigParserTest, CrlfLineEndings) {
    WriteConfig(
        "[audio]\r\n"
        "test.bik = test.wav\r\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
}

TEST_F(ConfigParserTest, ExceptionAndAudioCombined) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "a01.bik = a01.wav\n"
        "[audio]\n"
        "fallback.bik = fallback.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, 1);
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "fallback.bik");
}

TEST_F(ConfigParserTest, FindWavForBikException) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "a01.bik = BinkWAV\\a01.wav\n"
        "[audio]\n"
        "fallback.bik = fallback.wav\n"
    );
    LoadAudioConfig();

    const char* result = FindWavForBik("a01.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "BinkWAV\\a01.wav");
}

TEST_F(ConfigParserTest, FindWavForBikFallback) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "a01.bik = a01.wav\n"
        "[audio]\n"
        "fallback.bik = fallback.wav\n"
    );
    LoadAudioConfig();

    const char* result = FindWavForBik("fallback.bik", NULL);
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "fallback.wav");
}

TEST_F(ConfigParserTest, FindWavForBikNoMatch) {
    WriteConfig(
        "[audio]\n"
        "test.bik = test.wav\n"
    );
    LoadAudioConfig();

    const char* result = FindWavForBik("nonexistent.bik", NULL);
    EXPECT_EQ(result, (const char*)NULL);
}

TEST_F(ConfigParserTest, ExceptionPriorityOverAudio) {
    // When a bik is found in both exception and audio sections,
    // exception should take priority
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "test.bik = exception.wav\n"
        "[audio]\n"
        "test.bik = audio.wav\n"
    );
    LoadAudioConfig();

    const char* result = FindWavForBik("test.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "exception.wav");
}

TEST_F(ConfigParserTest, Binkw32CfgFormat) {
    // Real-world config from the project
    WriteConfig(
        "[log]\n"
        "; enabled = false\n"
        "; wait = true\n"
        "\n"
        "[exception]\n"
        "; RA2 movies\n"
        "0=movies01.mix\n"
        "1=movies02.mix\n"
        "\n"
        "[movies01]\n"
        "westlogo.bik = BinkWAV\\RA2\\westlogo.wav\n"
        "a00_f00e.bik = BinkWAV\\RA2\\a00_f00e.wav\n"
        "a01_f00e.bik = BinkWAV\\RA2\\a01_f00e.wav\n"
        "\n"
        "[audio]\n"
        "; Global fallback\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 2);
    EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
    EXPECT_EQ(g_exceptions[0].mapCount, 3);
    EXPECT_STREQ(g_exceptions[0].maps[0].bikName, "westlogo.bik");
}

TEST_F(ConfigParserTest, EmptyExceptionSection) {
    WriteConfig(
        "[exception]\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 0);
    EXPECT_EQ(g_audioMapCount, 0);
}

// ============================================================================
// Additional edge case tests
// ============================================================================

TEST_F(ConfigParserTest, SectionNameMatching) {
    // Section [movies01] should match exception entry "movies01.mix"
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "test.bik = test.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, 1);
    EXPECT_STREQ(g_exceptions[0].maps[0].bikName, "test.bik");
}

TEST_F(ConfigParserTest, SectionNameWithDotMix) {
    // Section [movies01.mix] should also match exception entry "movies01.mix"
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01.mix]\n"
        "test.bik = test.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, 1);
}

TEST_F(ConfigParserTest, CaseInsensitiveSectionMatch) {
    // Section names should match case-insensitively
    WriteConfig(
        "[exception]\n"
        "0=MOVIES01.MIX\n"
        "[movies01]\n"
        "test.bik = test.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, 1);
}

TEST_F(ConfigParserTest, MultipleExceptionMixes) {
    WriteConfig(
        "[exception]\n"
        "0=mix_a.mix\n"
        "1=mix_b.mix\n"
        "2=mix_c.mix\n"
        "[mix_a]\n"
        "a.bik = a.wav\n"
        "[mix_b]\n"
        "b.bik = b.wav\n"
        "[mix_c]\n"
        "c.bik = c.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 3);
    EXPECT_STREQ(g_exceptions[0].mixName, "mix_a.mix");
    EXPECT_STREQ(g_exceptions[1].mixName, "mix_b.mix");
    EXPECT_STREQ(g_exceptions[2].mixName, "mix_c.mix");
    EXPECT_EQ(g_exceptions[0].mapCount, 1);
    EXPECT_EQ(g_exceptions[1].mapCount, 1);
    EXPECT_EQ(g_exceptions[2].mapCount, 1);
}

TEST_F(ConfigParserTest, EmptyKeyIgnored) {
    WriteConfig(
        "[audio]\n"
        "= nokey.wav\n"
        "valid.bik = valid.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "valid.bik");
}

TEST_F(ConfigParserTest, EmptyValueIgnored) {
    WriteConfig(
        "[audio]\n"
        "test.bik =\n"
        "valid.bik = valid.wav\n"
    );
    LoadAudioConfig();
    ASSERT_EQ(g_audioMapCount, 1) << "an entry without a value must not be added";
    EXPECT_STREQ(g_audioMaps[0].bikName, "valid.bik");
    EXPECT_STREQ(g_audioMaps[0].wavPath, "valid.wav");

    // Dropping it silently is how "my wav never plays" bugs ship: the log has
    // to name the key that was rejected.
    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("[audio] entry needs"), std::string::npos) << log;
}

TEST_F(ConfigParserTest, SpacesAroundEquals) {
    WriteConfig(
        "[audio]\n"
        "test.bik  =  test.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
    EXPECT_STREQ(g_audioMaps[0].wavPath, "test.wav");
}

TEST_F(ConfigParserTest, LogDisabledByDefault) {
    WriteConfig(
        "[log]\n"
    );
    LoadAudioConfig();
    // g_logEnabled should remain TRUE (not explicitly set to false)
    EXPECT_EQ(g_logEnabled, TRUE);
}

TEST_F(ConfigParserTest, WaitDefaultFalse) {
    WriteConfig(
        "[log]\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_logWait, FALSE);
}

TEST_F(ConfigParserTest, WaitTrueValues) {
    // Both "true" and "1" should set g_logWait to TRUE
    WriteConfig(
        "[log]\n"
        "wait = true\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_logWait, TRUE);
}

TEST_F(ConfigParserTest, WaitCaseInsensitive) {
    WriteConfig(
        "[log]\n"
        "WAIT = True\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_logWait, TRUE);
}

TEST_F(ConfigParserTest, ExceptionWithMultipleMaps) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "a01.bik = a01.wav\n"
        "a02.bik = a02.wav\n"
        "a03.bik = a03.wav\n"
        "a04.bik = a04.wav\n"
        "a05.bik = a05.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, 5);
}

TEST_F(ConfigParserTest, ReservedSectionNamesIgnored) {
    // [log], [audio], [exception] are reserved — should not be treated as .mix sections
    WriteConfig(
        "[exception]\n"
        "0=audio.mix\n"
        "[audio]\n"
        "test.bik = test.wav\n"
    );
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_audioMapCount, 1);
    // [audio] section should be parsed as global audio, not as exception for "audio.mix"
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
}

TEST_F(ConfigParserTest, NoTrailingNewline) {
    WriteConfig("[audio]\ntest.bik = test.wav");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
}

TEST_F(ConfigParserTest, WindowsLineEndings) {
    WriteConfig("[audio]\r\ntest.bik = test.wav\r\n");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
}

TEST_F(ConfigParserTest, MixedLineEndings) {
    WriteConfig("[audio]\r\n\ntest.bik = test.wav\n");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
}

TEST_F(ConfigParserTest, FindWavForBikCaseInsensitive) {
    WriteConfig(
        "[audio]\n"
        "TEST.BIK = test.wav\n"
    );
    LoadAudioConfig();

    const char* result = FindWavForBik("test.bik", NULL);
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "test.wav");
}

TEST_F(ConfigParserTest, FindWavForBikWithSubdirectory) {
    WriteConfig(
        "[audio]\n"
        "test.bik = BinkWAV\\RA2\\test.wav\n"
    );
    LoadAudioConfig();

    const char* result = FindWavForBik("test.bik", NULL);
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "BinkWAV\\RA2\\test.wav");
}

TEST_F(ConfigParserTest, FindWavForBikNullMixName) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "a01.bik = exception.wav\n"
        "[audio]\n"
        "a01.bik = audio.wav\n"
    );
    LoadAudioConfig();

    // With NULL mixName (BINKIOPROCESSOR mode), search all exception sections
    const char* result = FindWavForBik("a01.bik", NULL);
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "exception.wav");
}

TEST_F(ConfigParserTest, FindWavForBikWrongMixName) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[movies01]\n"
        "a01.bik = exception.wav\n"
        "[audio]\n"
        "a01.bik = audio.wav\n"
    );
    LoadAudioConfig();

    // Wrong mix name — should fall through to [audio]
    const char* result = FindWavForBik("a01.bik", "wrong.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "audio.wav");
}

TEST_F(ConfigParserTest, FullRa2Config) {
    // Simulate real RA2 config with movies01 + movies02
    WriteConfig(
        "[log]\n"
        "; enabled = false\n"
        "wait = true\n"
        "\n"
        "[exception]\n"
        "0=movies01.mix\n"
        "1=movies02.mix\n"
        "\n"
        "[movies01]\n"
        "westlogo.bik = BinkWAV\\RA2\\westlogo.wav\n"
        "a01_f00e.bik = BinkWAV\\RA2\\a01_f00e.wav\n"
        "\n"
        "[movies02]\n"
        "s01_f00e.bik = BinkWAV\\RA2\\s01_f00e.wav\n"
        "\n"
        "[audio]\n"
        "; Global fallback\n"
    );
    LoadAudioConfig();

    EXPECT_EQ(g_logWait, TRUE);
    EXPECT_EQ(g_exceptionCount, 2);
    EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
    EXPECT_STREQ(g_exceptions[1].mixName, "movies02.mix");
    EXPECT_EQ(g_exceptions[0].mapCount, 2);
    EXPECT_EQ(g_exceptions[1].mapCount, 1);

    // Exception lookup
    const char* r1 = FindWavForBik("westlogo.bik", "movies01.mix");
    ASSERT_NE(r1, (const char*)NULL);
    EXPECT_STREQ(r1, "BinkWAV\\RA2\\westlogo.wav");

    const char* r2 = FindWavForBik("s01_f00e.bik", "movies02.mix");
    ASSERT_NE(r2, (const char*)NULL);
    EXPECT_STREQ(r2, "BinkWAV\\RA2\\s01_f00e.wav");

    // Non-existent
    const char* r3 = FindWavForBik("nonexistent.bik", "movies01.mix");
    EXPECT_EQ(r3, (const char*)NULL);
}

// ============================================================================
// Exception baseDir — auto .wav/.ogg under per-mix base path
// ============================================================================

TEST_F(ConfigParserTest, ExceptionBaseDirParsed) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|BinkWAV\\RA2\n"
        "1=movies02.mix\n"
        "2=movmd03.mix|Audio\\YR/\n"
    );
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 3);
    EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
    EXPECT_STREQ(g_exceptions[0].baseDir, "BinkWAV\\RA2");
    EXPECT_STREQ(g_exceptions[1].mixName, "movies02.mix");
    EXPECT_STREQ(g_exceptions[1].baseDir, "");
    EXPECT_STREQ(g_exceptions[2].mixName, "movmd03.mix");
    EXPECT_STREQ(g_exceptions[2].baseDir, "Audio\\YR/");
}

TEST_F(ConfigParserTest, ExceptionBaseDirEmptyAfterBar) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|\n"
        "1=movies02.mix|   \n"
    );
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 2);
    EXPECT_STREQ(g_exceptions[0].baseDir, "");
    EXPECT_STREQ(g_exceptions[1].baseDir, "");
}

TEST_F(ConfigParserTest, ExceptionBaseDirAutoWav) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
    );
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 1);
    EXPECT_STREQ(g_exceptions[0].baseDir, "WavPack");

    // Create temp base dir + wav file under g_dllDir
    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char wavPath[MAX_PATH];
    _snprintf_s(wavPath, sizeof(wavPath), _TRUNCATE, "%s\\intro.wav", baseDir);
    FILE* f = NULL;
    fopen_s(&f, wavPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("RIFF", 1, 4, f);
    fclose(f);

    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "WavPack\\intro.wav");

    DeleteFileA(wavPath);
    RemoveDirectoryA(baseDir);
}

TEST_F(ConfigParserTest, ExceptionBaseDirAutoOggFallback) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
    );
    LoadAudioConfig();

    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char oggPath[MAX_PATH];
    _snprintf_s(oggPath, sizeof(oggPath), _TRUNCATE, "%s\\intro.ogg", baseDir);
    FILE* f = NULL;
    fopen_s(&f, oggPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("OggS", 1, 4, f);
    fclose(f);

    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "WavPack\\intro.ogg");

    DeleteFileA(oggPath);
    RemoveDirectoryA(baseDir);
}

TEST_F(ConfigParserTest, ExceptionBaseDirOggPreferredOverWav) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
    );
    LoadAudioConfig();

    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char wavPath[MAX_PATH];
    char oggPath[MAX_PATH];
    _snprintf_s(wavPath, sizeof(wavPath), _TRUNCATE, "%s\\intro.wav", baseDir);
    _snprintf_s(oggPath, sizeof(oggPath), _TRUNCATE, "%s\\intro.ogg", baseDir);
    FILE* f = NULL;
    fopen_s(&f, wavPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("RIFF", 1, 4, f);
    fclose(f);
    fopen_s(&f, oggPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("OggS", 1, 4, f);
    fclose(f);

    // Both exist — .ogg wins (priority: ogg then wav)
    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "WavPack\\intro.ogg");

    DeleteFileA(wavPath);
    DeleteFileA(oggPath);
    RemoveDirectoryA(baseDir);
}

TEST_F(ConfigParserTest, ExceptionBaseDirNoFileFallsThrough) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
        "[audio]\n"
        "intro.bik = fallback.wav\n"
    );
    LoadAudioConfig();

    // baseDir set but file missing → fall to [audio]
    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "fallback.wav");
}

TEST_F(ConfigParserTest, ExceptionBaseDirNotSpecifiedNoAuto) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix\n"
        "[audio]\n"
        "intro.bik = fallback.wav\n"
    );
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 1);
    EXPECT_STREQ(g_exceptions[0].baseDir, "");

    // No baseDir, no explicit map → [audio]
    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "fallback.wav");
}

TEST_F(ConfigParserTest, ExceptionBaseDirExplicitMapWins) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
        "[movies01]\n"
        "intro.bik = custom\\intro.wav\n"
        "[audio]\n"
        "intro.bik = audio.wav\n"
    );
    LoadAudioConfig();

    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char wavPath[MAX_PATH];
    _snprintf_s(wavPath, sizeof(wavPath), _TRUNCATE, "%s\\intro.wav", baseDir);
    FILE* f = NULL;
    fopen_s(&f, wavPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("RIFF", 1, 4, f);
    fclose(f);

    // Explicit map beats auto baseDir
    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "custom\\intro.wav");

    DeleteFileA(wavPath);
    RemoveDirectoryA(baseDir);
}

TEST_F(ConfigParserTest, ExceptionBaseDirTrailingSlash) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\\\n"
    );
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 1);
    EXPECT_STREQ(g_exceptions[0].baseDir, "WavPack\\");

    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char wavPath[MAX_PATH];
    _snprintf_s(wavPath, sizeof(wavPath), _TRUNCATE, "%s\\intro.wav", baseDir);
    FILE* f = NULL;
    fopen_s(&f, wavPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("RIFF", 1, 4, f);
    fclose(f);

    // Trailing slash must not produce double separator issues
    const char* result = FindWavForBik("intro.bik", "movies01.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "WavPack\\intro.wav");

    DeleteFileA(wavPath);
    RemoveDirectoryA(baseDir);
}

TEST_F(ConfigParserTest, ExceptionBaseDirNoMixName) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
        "[audio]\n"
        "intro.bik = fallback.wav\n"
    );
    LoadAudioConfig();

    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char wavPath[MAX_PATH];
    _snprintf_s(wavPath, sizeof(wavPath), _TRUNCATE, "%s\\intro.wav", baseDir);
    FILE* f = NULL;
    fopen_s(&f, wavPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("RIFF", 1, 4, f);
    fclose(f);

    // mixName=NULL (BINKIOPROCESSOR) still auto-resolves via baseDir
    const char* result = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "WavPack\\intro.wav");

    DeleteFileA(wavPath);
    RemoveDirectoryA(baseDir);
}

TEST_F(ConfigParserTest, ExceptionBaseDirWrongMixFallsThrough) {
    WriteConfig(
        "[exception]\n"
        "0=movies01.mix|WavPack\n"
        "[audio]\n"
        "intro.bik = fallback.wav\n"
    );
    LoadAudioConfig();

    char baseDir[MAX_PATH];
    _snprintf_s(baseDir, sizeof(baseDir), _TRUNCATE, "%sWavPack", tempDir);
    CreateDirectoryA(baseDir, NULL);
    char wavPath[MAX_PATH];
    _snprintf_s(wavPath, sizeof(wavPath), _TRUNCATE, "%s\\intro.wav", baseDir);
    FILE* f = NULL;
    fopen_s(&f, wavPath, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite("RIFF", 1, 4, f);
    fclose(f);

    // Wrong mix name → no auto from movies01, fall to [audio]
    const char* result = FindWavForBik("intro.bik", "other.mix");
    ASSERT_NE(result, (const char*)NULL);
    EXPECT_STREQ(result, "fallback.wav");

    DeleteFileA(wavPath);
    RemoveDirectoryA(baseDir);
}

// ============================================================================
// Config capacity limits (MAX_AUDIO_MAPS / MAX_EXCEPTION_MIXES / MAX_MAPS_PER_MIX)
// ============================================================================

TEST_F(ConfigParserTest, AudioMapLimitAcceptsFullCapacity) {
    std::string cfg = "[audio]\n";
    for (int i = 0; i < MAX_AUDIO_MAPS; i++) {
        char line[64];
        snprintf(line, sizeof(line), "f%03d.bik = f%03d.wav\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, MAX_AUDIO_MAPS);
    EXPECT_STREQ(g_audioMaps[MAX_AUDIO_MAPS - 1].bikName, "f255.bik");
}

TEST_F(ConfigParserTest, AudioMapLimitCapsOverflow) {
    std::string cfg = "[audio]\n";
    for (int i = 0; i < MAX_AUDIO_MAPS + 10; i++) {
        char line[64];
        snprintf(line, sizeof(line), "f%03d.bik = f%03d.wav\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, MAX_AUDIO_MAPS); // overflow dropped, no crash
}

TEST_F(ConfigParserTest, ExceptionMixLimitAcceptsFullCapacity) {
    std::string cfg = "[exception]\n";
    for (int i = 0; i < MAX_EXCEPTION_MIXES; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%d=mix%02d.mix\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, MAX_EXCEPTION_MIXES);
    EXPECT_STREQ(g_exceptions[MAX_EXCEPTION_MIXES - 1].mixName, "mix63.mix");
}

TEST_F(ConfigParserTest, ExceptionMixLimitCapsOverflow) {
    std::string cfg = "[exception]\n";
    for (int i = 0; i < MAX_EXCEPTION_MIXES + 5; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%d=mix%02d.mix\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();
    EXPECT_EQ(g_exceptionCount, MAX_EXCEPTION_MIXES); // overflow dropped, no crash
}

TEST_F(ConfigParserTest, ExceptionMapLimitAcceptsFullCapacity) {
    std::string cfg = "[exception]\n0=movies01.mix\n[movies01]\n";
    for (int i = 0; i < MAX_MAPS_PER_MIX; i++) {
        char line[64];
        snprintf(line, sizeof(line), "f%03d.bik = f%03d.wav\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, MAX_MAPS_PER_MIX);
    EXPECT_STREQ(g_exceptions[0].maps[MAX_MAPS_PER_MIX - 1].bikName, "f255.bik");
}

TEST_F(ConfigParserTest, ExceptionMapLimitCapsOverflow) {
    std::string cfg = "[exception]\n0=movies01.mix\n[movies01]\n";
    for (int i = 0; i < MAX_MAPS_PER_MIX + 10; i++) {
        char line[64];
        snprintf(line, sizeof(line), "f%03d.bik = f%03d.wav\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();
    ASSERT_EQ(g_exceptionCount, 1);
    EXPECT_EQ(g_exceptions[0].mapCount, MAX_MAPS_PER_MIX); // overflow dropped, no crash
}

// ============================================================================
// Inline comments, boolean values, malformed lines (Agent.md §14.3)
// ============================================================================

// README.md:288 documents `enabled = false   ; ...` — the comment used to be
// part of the value, so the log stayed enabled.
TEST_F(ConfigParserTest, InlineCommentStripped) {
    WriteConfig("[log]\nenabled = false   ; disable all logging\nwait = true ; pause\n");
    LoadAudioConfig();
    EXPECT_EQ(g_logEnabled, FALSE);
    EXPECT_EQ(g_logWait, TRUE);
}

// Paths may legitimately contain ';' — only whitespace-preceded markers go.
TEST_F(ConfigParserTest, InlineCommentKeepsPathWithoutSpace) {
    WriteConfig("[audio]\na.bik = C:\\a;b.wav\nb.bik = C:\\p#1\\c.wav\n");
    LoadAudioConfig();
    ASSERT_EQ(g_audioMapCount, 2);
    EXPECT_STREQ(g_audioMaps[0].wavPath, "C:\\a;b.wav");
    EXPECT_STREQ(g_audioMaps[1].wavPath, "C:\\p#1\\c.wav");
}

// `wait = yes` / `enabled = no` used to be dropped silently.
TEST_F(ConfigParserTest, BooleanSynonyms) {
    WriteConfig("[log]\nenabled = no\nwait = yes\n");
    LoadAudioConfig();
    EXPECT_EQ(g_logEnabled, FALSE);
    EXPECT_EQ(g_logWait, TRUE);

    ResetAudioConfig();
    WriteConfig("[log]\nenabled = yes\nwait = off\n");
    LoadAudioConfig();
    EXPECT_EQ(g_logEnabled, TRUE);
    EXPECT_EQ(g_logWait, FALSE);
}

// A config created after the first lookup must still be picked up (the
// "no cfg file" state used to be latched as "loaded" forever).
TEST_F(ConfigParserTest, ConfigCreatedAfterFirstLookupIsPickedUp) {
    EXPECT_EQ(FindWavForBik("intro.bik", NULL), (const char*)NULL);

    WriteConfig("[audio]\nintro.bik = intro.wav\n");
    const char* wav = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(wav, (const char*)NULL);
    EXPECT_STREQ(wav, "intro.wav");
}

// Malformed input must be reported in the log instead of being dropped
// silently (unknown section/line/key, empty value, unrecognised boolean).
TEST_F(ConfigParserTest, ParserWarnsOnMalformedInput) {
    WriteConfig("[exception]\nmovies01.mix = \nbare line\n[nope]\nfoo = 1\n"
                "[log]\nunknownkey = 1\nwait = maybe\n");
    LoadAudioConfig();

    EXPECT_EQ(g_logWait, FALSE); // unrecognised value keeps the default

    std::string log = ReadConfigLog();
    ASSERT_FALSE(log.empty()) << "config log was not written";
    EXPECT_NE(log.find("line has no '='"), std::string::npos) << log;
    EXPECT_NE(log.find("section [nope] ignored"), std::string::npos) << log;
    EXPECT_NE(log.find("unknown [log] key 'unknownkey'"), std::string::npos) << log;
    EXPECT_NE(log.find("[exception] entry needs"), std::string::npos) << log;
    EXPECT_NE(log.find("value 'maybe' not recognised"), std::string::npos) << log;
}

TEST_F(ConfigParserTest, BlankLinesAndIndentedCommentsDoNotWarn) {
    // Field log (2026-09-26): every blank line in binkw32.cfg used to emit
    // "WARNING: config line has no '='"; an indented "; key = value" comment
    // must not be parsed as a key either.
    WriteConfig("[log]\n\n   \n  ; wait = true\nenabled = true\n\n"
                "[exception]\n\n0=movies01.mix|BinkWAV\\RA2\n");
    LoadAudioConfig();

    EXPECT_TRUE(g_logEnabled) << "enabled = true must apply";
    EXPECT_FALSE(g_logWait) << "indented comment must not be parsed as a key";

    std::string log = ReadConfigLog();
    EXPECT_EQ(log.find("line has no '='"), std::string::npos) << log;
    EXPECT_EQ(log.find("unknown [log] key"), std::string::npos) << log;

    ASSERT_EQ(g_exceptionCount, 1);
    EXPECT_STREQ(g_exceptions[0].mixName, "movies01.mix");
}

// ============================================================================
// Agent.md — meloчи третьего аудита
// ============================================================================

// A `key = value` above the first `[section]` header used to be matched
// against nothing at all and dropped without a trace — no effect, no log line.
TEST_F(ConfigParserTest, KeyBeforeFirstSectionWarnsAndIsIgnored) {
    WriteConfig("wait = true\n[audio]\nintro.bik = intro.wav\n");
    LoadAudioConfig();

    EXPECT_FALSE(g_logWait) << "a key outside any section must not be applied";
    ASSERT_EQ(g_audioMapCount, 1);

    std::string log = ReadConfigLog();
    ASSERT_FALSE(log.empty()) << "config log was not written";
    EXPECT_NE(log.find("before any [section]"), std::string::npos) << log;
}

// An empty cfg must publish "idle", not "loaded": a file the user fills in
// later has to be picked up, otherwise audio replacement stays off for the
// rest of the session with nothing in the log to say why.
TEST_F(ConfigParserTest, EmptyConfigIsPickedUpLater) {
    WriteConfig("");
    EXPECT_EQ(FindWavForBik("intro.bik", NULL), (const char*)NULL);

    WriteConfig("[audio]\nintro.bik = intro.wav\n");
    const char* wav = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(wav, (const char*)NULL) << "a cfg written after an empty one was ignored";
    EXPECT_STREQ(wav, "intro.wav");
}

// UTF-16 content is unreadable by this byte parser: the ASCII parts are
// interleaved with NULs, so the loop stops on the first one and the user got
// a silently empty config marked as loaded.
TEST_F(ConfigParserTest, Utf16ConfigRejectedAndRetried) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbinkw32.cfg", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    static const unsigned char utf16le[] = {
        0xFF, 0xFE,
        'w', 0, 'a', 0, 'i', 0, 't', 0, ' ', 0, '=', 0, ' ', 0,
        't', 0, 'r', 0, 'u', 0, 'e', 0, '\r', 0, '\n', 0
    };
    fwrite(utf16le, 1, sizeof(utf16le), f);
    fclose(f);

    EXPECT_EQ(FindWavForBik("intro.bik", NULL), (const char*)NULL);
    EXPECT_FALSE(g_logWait) << "UTF-16 content must not be parsed as ANSI";

    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("Config is UTF-16"), std::string::npos) << log;

    // Re-saving as ANSI must be picked up (state 0, not "loaded").
    WriteConfig("[audio]\nintro.bik = intro.wav\n");
    const char* wav = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(wav, (const char*)NULL) << "a re-saved cfg was ignored";
    EXPECT_STREQ(wav, "intro.wav");
}

// One process-wide flag meant the FIRST mix to hit MAX_MAPS_PER_MIX was
// reported and every later one was dropped silently.
TEST_F(ConfigParserTest, MapLimitWarnsForEachMix) {
    std::string cfg = "[exception]\n0=movies01.mix\n1=movies02.mix\n[movies01]\n";
    for (int i = 0; i < MAX_MAPS_PER_MIX + 1; i++) {
        char line[64];
        snprintf(line, sizeof(line), "f%03d.bik = f%03d.wav\n", i, i);
        cfg += line;
    }
    cfg += "[movies02]\n";
    for (int i = 0; i < MAX_MAPS_PER_MIX + 1; i++) {
        char line[64];
        snprintf(line, sizeof(line), "g%03d.bik = g%03d.wav\n", i, i);
        cfg += line;
    }
    WriteConfig(cfg.c_str());
    LoadAudioConfig();

    ASSERT_EQ(g_exceptionCount, 2);
    EXPECT_EQ(g_exceptions[0].mapCount, MAX_MAPS_PER_MIX);
    EXPECT_EQ(g_exceptions[1].mapCount, MAX_MAPS_PER_MIX);

    std::string log = ReadConfigLog();
    size_t p1 = log.find("Exception map limit reached");
    ASSERT_NE(p1, std::string::npos) << "first overflowing mix not reported:\n" << log;
    EXPECT_NE(log.substr(p1).find("movies01.mix"), std::string::npos) << log;
    size_t p2 = log.find("Exception map limit reached", p1 + 1);
    ASSERT_NE(p2, std::string::npos)
        << "second overflowing mix still dropped silently:\n" << log;
    EXPECT_NE(log.substr(p2).find("movies02.mix"), std::string::npos) << log;
    EXPECT_EQ(log.find("Exception map limit reached", p2 + 1), std::string::npos) << log;
}

// ============================================================================
// Четвёртый аудит — молчаливые отказы и производительность
// ============================================================================

// A cfg holding nothing but comments used to be published as "loaded": the
// parse loop ran to the end, logged "Config loaded", and state 2 blocked the
// retry — so a user filling the file in later never got audio replacement,
// with no log line explaining why.
TEST_F(ConfigParserTest, CommentsOnlyConfigIsPickedUpLater) {
    WriteConfig("; nothing but comments here\n# still nothing\n");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 0);
    EXPECT_EQ(g_exceptionCount, 0);

    WriteConfig("[audio]\nintro.bik = intro.wav\n");
    const char* wav = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(wav, (const char*)NULL)
        << "a comments-only cfg was latched as loaded, later content ignored";
    EXPECT_STREQ(wav, "intro.wav");

    // Read the log last: ReadConfigLog closes it and the next LogF would
    // truncate the file on reopen.
    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("Config is empty"), std::string::npos) << log;
}

// In text mode ("r") fread stops at the first 0x1A, so everything after a
// stray Ctrl+Z in the cfg was silently discarded (and ftell/fread counts no
// longer matched). The parser must read bytes verbatim ("rb").
TEST_F(ConfigParserTest, CtrlZInsideConfigDoesNotTruncateIt) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbinkw32.cfg", tempDir);
    static const char content[] =
        "[audio]\n"
        "before.bik = before.wav\n"
        "; stray Ctrl+Z " "\x1A" " inside a comment\n"
        "after.bik = after.wav\n";
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(content, 1, sizeof(content) - 1, f);
    fclose(f);

    LoadAudioConfig();
    ASSERT_EQ(g_audioMapCount, 2)
        << "text-mode EOF (0x1A) truncated the config at the comment";
    EXPECT_STREQ(g_audioMaps[0].bikName, "before.bik");
    EXPECT_STREQ(g_audioMaps[1].bikName, "after.bik");
}

// `[]` leaves the section name empty: no section flag matches and every key
// that follows used to be dropped without a trace — not even a log line.
TEST_F(ConfigParserTest, EmptySectionNameWarns) {
    WriteConfig("[]\nwait = true\n[audio]\ntest.bik = test.wav\n");
    LoadAudioConfig();

    // The keys under `[]` belong to no section, so wait must stay default.
    EXPECT_FALSE(g_logWait) << "keys under [] must not reach [log]";
    ASSERT_EQ(g_audioMapCount, 1);

    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("empty section name"), std::string::npos) << log;
}

// UTF-16 without a BOM: same NUL-interleaved dead end as the BOM case, but
// the detector only looked for FF FE / FE FF — the parser stopped on the
// first embedded NUL, logged "Config loaded", and never retried.
TEST_F(ConfigParserTest, Utf16NoBomConfigRejectedAndRetried) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbinkw32.cfg", tempDir);
    static const unsigned char utf16le[] = {
        'w',0,'a',0,'i',0,'t',0,' ',0,'=',0,' ',0,'t',0,'r',0,'u',0,'e',0,'\r',0,'\n',0,
        '[',0,'a',0,'u',0,'d',0,'i',0,'o',0,']',0,'\r',0,'\n',0,
        'i',0,'n',0,'t',0,'r',0,'o',0,'.',0,'b',0,'i',0,'k',0,
        ' ',0,'=',0,' ',0,
        'i',0,'n',0,'t',0,'r',0,'o',0,'.',0,'w',0,'a',0,'v',0,'\r',0,'\n',0
    };
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(utf16le, 1, sizeof(utf16le), f);
    fclose(f);

    EXPECT_EQ(FindWavForBik("intro.bik", NULL), (const char*)NULL);
    EXPECT_FALSE(g_logWait) << "UTF-16 content must not be parsed as ANSI";

    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("Config is UTF-16 (no BOM"), std::string::npos) << log;

    // State must be idle: re-saving as ANSI is picked up on the next lookup.
    WriteConfig("[audio]\nintro.bik = intro.wav\n");
    const char* wav = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(wav, (const char*)NULL) << "a re-saved cfg was ignored";
    EXPECT_STREQ(wav, "intro.wav");
}

// An over-long line was reported without its line number and without any
// snippet, so "which line do I fix?" was unanswerable in a big cfg.
TEST_F(ConfigParserTest, LongLineWarningHasLineNumberAndPreview) {
    std::string cfg = "[audio]\n;";
    cfg.append(1300, 'x');
    cfg += "\nafter.bik = after.wav\n";
    WriteConfig(cfg.c_str());
    LoadAudioConfig();

    // Truncation must not swallow the line after the long one.
    ASSERT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "after.bik");

    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("config line 2 is 1301 bytes"), std::string::npos) << log;
    EXPECT_NE(log.find("truncated: ;"), std::string::npos) << log;
}

// The warn-once flags used to be function-local statics that lived for the
// whole process: after ResetAudioConfig (test reset, but semantically the
// same as a user fixing their cfg) the second empty cfg stayed silent.
TEST_F(ConfigParserTest, WarnOnceFlagsResetWithConfigReset) {
    LoadAudioConfig(); // no file at all yet
    EXPECT_EQ(g_audioMapCount, 0);

    ResetAudioConfig();
    WriteConfig("");
    LoadAudioConfig();

    ResetAudioConfig();
    WriteConfig("");
    LoadAudioConfig();

    std::string log = ReadConfigLog();
    ASSERT_FALSE(log.empty()) << "config log was not written";
    EXPECT_NE(log.find("Config not found"), std::string::npos) << log;
    size_t p1 = log.find("Config is empty");
    ASSERT_NE(p1, std::string::npos) << log;
    size_t p2 = log.find("Config is empty", p1 + 1);
    EXPECT_NE(p2, std::string::npos)
        << "the empty-config warning did not repeat after ResetAudioConfig:\n" << log;
}

// malloc of ~2 GiB must fail in the 32-bit proxy (2 GiB VA, no LAA): the
// cfg read used to die silently with state latched "loaded", so audio
// replacement stayed off for the session with nothing in the log. The file
// is sparse so the test costs no disk space.
TEST_F(ConfigParserTest, OomOnHugeConfigIsLoggedAndRetried) {
    if (sizeof(void*) != 4)
        GTEST_SKIP() << "32-bit address space required for the malloc to fail";

    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbinkw32.cfg", tempDir);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    DWORD dummy = 0;
    DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dummy, NULL);
    LARGE_INTEGER sz;
    sz.QuadPart = 2147482647LL; // LONG_MAX - 1000: ftell-safe, malloc(2 GiB) fails
    ASSERT_TRUE(SetFilePointerEx(h, sz, NULL, FILE_BEGIN));
    ASSERT_TRUE(SetEndOfFile(h));
    CloseHandle(h);

    DWORD hi = 0;
    DWORD lo = GetCompressedFileSizeA(path, &hi);
    ULONGLONG alloc = ((ULONGLONG)hi << 32) | lo;
    if (alloc > 16ull * 1024 * 1024) {
        DeleteFileA(path);
        GTEST_SKIP() << "filesystem did not keep the huge file sparse (alloc="
                     << alloc << " bytes)";
    }

    LoadAudioConfig();

    // Idle, not "loaded": a normal cfg written afterwards must be parsed.
    WriteConfig("[audio]\nintro.bik = intro.wav\n");
    const char* wav = FindWavForBik("intro.bik", NULL);
    ASSERT_NE(wav, (const char*)NULL)
        << "after the OOM the config was latched as loaded";
    EXPECT_STREQ(wav, "intro.wav");

    std::string log = ReadConfigLog();
    EXPECT_NE(log.find("out of memory reading config"), std::string::npos) << log;
}

// The old MixCrc32 copied at most 255 chars into a stack buffer (len clamped
// with `if (len >= 256) len = 255`), so distinct long names hashed equal —
// the game hashes the whole name (ReSource ComputeId has no length cap).
// LMD names longer than 255 chars could therefore resolve to the wrong
// entry. Reference: python zlib.crc32 over the TS-padded uppercase name.
TEST_F(ConfigParserTest, MixCrc32HashesFullNameBeyond255Chars) {
    char long300[301];
    memset(long300, 'a', 300);
    long300[300] = '\0';
    char long255[256];
    memset(long255, 'a', 255);
    long255[255] = '\0';

    EXPECT_NE(MixCrc32(long300), MixCrc32(long255))
        << "names beyond 255 chars hash to the same CRC (truncated hash)";
    EXPECT_EQ(MixCrc32(long300), 0xBBA03323u);
    EXPECT_EQ(MixCrc32(long255), 0xD1457BAFu);
}
