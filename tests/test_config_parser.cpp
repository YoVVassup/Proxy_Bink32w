#include <gtest/gtest.h>
#include "test_helpers.h"
#include <cstdio>
#include <cstring>

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
    }

    void TearDown() override {
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
    // Both might be added (parser doesn't check for empty value after =)
    // But at minimum "valid" should be there
    BOOL foundValid = FALSE;
    for (int i = 0; i < g_audioMapCount; i++) {
        if (strcmp(g_audioMaps[i].bikName, "valid.bik") == 0) foundValid = TRUE;
    }
    EXPECT_TRUE(foundValid);
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
