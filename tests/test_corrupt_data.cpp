#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// test_corrupt_data.cpp — Negative tests for corrupt/malformed files
// ============================================================================

extern BOOL g_logEnabled;

class CorruptTest : public ::testing::Test {
protected:
    BOOL savedLog;
    char savedDir[MAX_PATH];
    void SetUp() override {
        savedLog = g_logEnabled;
        lstrcpynA(savedDir, g_dllDir, MAX_PATH);
        g_mixCacheCount = 0;
        ResetAudioConfig();
        g_logEnabled = FALSE;
    }
    void TearDown() override {
        g_logEnabled = savedLog;
        lstrcpynA(g_dllDir, savedDir, MAX_PATH);
        g_mixCacheCount = 0;
        ResetAudioConfig();
    }
};

// ============================================================================
// .mix file corruption tests
// ============================================================================

TEST_F(CorruptTest, MixEmptyFile) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\empty.mix", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

TEST_F(CorruptTest, MixTooSmallHeader) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\small.mix", TEST_DATA_DIR);
    uint8_t data[10] = {0};
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 10, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

TEST_F(CorruptTest, MixZeroFileCount) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\zero_count.mix", TEST_DATA_DIR);
    uint8_t data[14] = {0};
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 14, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

TEST_F(CorruptTest, MixHugeFileCount) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\huge_count.mix", TEST_DATA_DIR);
    uint8_t data[14] = {0};
    data[5] = 0x02;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 14, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

TEST_F(CorruptTest, MixHashTableTruncated) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\trunc_hash.mix", TEST_DATA_DIR);
    uint8_t data[14] = {0};
    data[4] = 0x05;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 14, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

TEST_F(CorruptTest, MixInvalidMagic) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\bad_magic.mix", TEST_DATA_DIR);
    uint8_t data[100] = {0};
    data[0] = 0xFF;
    data[1] = 0xFE;
    data[2] = 0xFD;
    data[3] = 0xFC;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 100, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

// ============================================================================
// .bik file corruption tests
// ============================================================================

TEST_F(CorruptTest, BikEmptyFile) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\empty.bik", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fclose(f);

    BinkFileInfo info = ReadBinkHeaderFromPath(path);
    EXPECT_FALSE(info.valid);
    DeleteFileA(path);
}

TEST_F(CorruptTest, BikInvalidMarker) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\bad_marker.bik", TEST_DATA_DIR);
    uint8_t hdr[44] = {0};
    hdr[0] = 0xFF; hdr[1] = 0xFE; hdr[2] = 0xFD; hdr[3] = 0xFC;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(hdr, 1, 44, f);
    fclose(f);

    BinkFileInfo info = ReadBinkHeaderFromPath(path);
    EXPECT_FALSE(info.valid);
    DeleteFileA(path);
}

TEST_F(CorruptTest, BikValidMarkerZeroDimensions) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\zero_dim.bik", TEST_DATA_DIR);
    uint8_t hdr[44] = {0};
    hdr[0] = 0x42; hdr[1] = 0x49; hdr[2] = 0x4B; hdr[3] = 0x66;
    hdr[8] = 0x64;
    hdr[28] = 0x0F;
    hdr[32] = 0x01;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(hdr, 1, 44, f);
    fclose(f);

    BinkFileInfo info = ReadBinkHeaderFromPath(path);
    EXPECT_FALSE(info.valid);
    DeleteFileA(path);
}

TEST_F(CorruptTest, BikTruncatedHeader) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\trunc.bik", TEST_DATA_DIR);
    uint8_t hdr[20] = {0};
    hdr[0] = 0x42; hdr[1] = 0x49; hdr[2] = 0x4B; hdr[3] = 0x66;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(hdr, 1, 20, f);
    fclose(f);

    BinkFileInfo info = ReadBinkHeaderFromPath(path);
    EXPECT_FALSE(info.valid);
    DeleteFileA(path);
}

TEST_F(CorruptTest, BikAllValidMarkers) {
    uint8_t markers[][4] = {
        {0x42, 0x49, 0x4B, 0x66},
        {0x42, 0x49, 0x4B, 0x67},
        {0x42, 0x49, 0x4B, 0x68},
        {0x42, 0x49, 0x4B, 0x69},
    };
    for (int m = 0; m < 4; m++) {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\marker_%d.bik", TEST_DATA_DIR, m);
        uint8_t hdr[44] = {0};
        memcpy(hdr, markers[m], 4);
        hdr[20] = 0x80; hdr[21] = 0x02;
        hdr[24] = 0xE0; hdr[25] = 0x01;
        hdr[8] = 0x64;
        hdr[28] = 0x0F;
        hdr[32] = 0x01;
        FILE* f = NULL;
        fopen_s(&f, path, "wb");
        ASSERT_NE(f, (FILE*)NULL);
        fwrite(hdr, 1, 44, f);
        fclose(f);

        BinkFileInfo info = ReadBinkHeaderFromPath(path);
        EXPECT_TRUE(info.valid) << "Marker 0x" << std::hex << markers[m][3];
        EXPECT_EQ(info.width, 640u);
        EXPECT_EQ(info.height, 480u);
        DeleteFileA(path);
    }
}

TEST_F(CorruptTest, BikInvalidMarkers) {
    uint8_t markers[][4] = {
        {0x42, 0x49, 0x4B, 0x61},
        {0x42, 0x49, 0x4B, 0x62},
        {0x42, 0x49, 0x4B, 0x6A},
        {0x42, 0x49, 0x4B, 0xFF},
        {0x00, 0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0xFF, 0xFF},
    };
    for (int m = 0; m < 6; m++) {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\inv_%02X.bik", TEST_DATA_DIR, markers[m][3]);
        uint8_t hdr[44] = {0};
        memcpy(hdr, markers[m], 4);
        hdr[20] = 0x80; hdr[21] = 0x02;
        hdr[24] = 0xE0; hdr[25] = 0x01;
        FILE* f = NULL;
        fopen_s(&f, path, "wb");
        ASSERT_NE(f, (FILE*)NULL);
        fwrite(hdr, 1, 44, f);
        fclose(f);

        BinkFileInfo info = ReadBinkHeaderFromPath(path);
        EXPECT_FALSE(info.valid) << "Marker 0x" << std::hex << markers[m][3];
        DeleteFileA(path);
    }
}

// ============================================================================
// .wav corruption tests
// ============================================================================

TEST_F(CorruptTest, WavEmptyFile) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\empty.wav", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavWrongRiffHeader) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\wrong_riff.wav", TEST_DATA_DIR);
    uint8_t data[44] = {0};
    data[0] = 'X'; data[1] = 'X'; data[2] = 'X'; data[3] = 'X';
    data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 44, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavWrongWaveMarker) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\wrong_wave.wav", TEST_DATA_DIR);
    uint8_t data[44] = {0};
    data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
    data[8] = 'X'; data[9] = 'X'; data[10] = 'X'; data[11] = 'X';
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 44, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavZeroChannels) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\zero_ch.wav", TEST_DATA_DIR);
    uint8_t data[44] = {0};
    data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
    data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
    data[20] = 1;
    data[28] = 0x44; data[29] = 0xAC;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 44, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavTooManyChannels) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\many_ch.wav", TEST_DATA_DIR);
    uint8_t data[44] = {0};
    data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
    data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
    data[20] = 1;
    data[24] = 9;
    data[28] = 0x44; data[29] = 0xAC;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 44, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavZeroSampleRate) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\zero_rate.wav", TEST_DATA_DIR);
    uint8_t data[44] = {0};
    data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
    data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
    data[20] = 1;
    data[24] = 1;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 44, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavInvalidBitsPerSample) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\bad_bits.wav", TEST_DATA_DIR);
    uint8_t data[44] = {0};
    data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
    data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
    data[20] = 1;
    data[24] = 1;
    data[28] = 0x44; data[29] = 0xAC;
    data[34] = 7;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 44, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
}

TEST_F(CorruptTest, WavNonexistentFile) {
    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile("C:\\nonexistent\\file.wav", &audio));
}

TEST_F(CorruptTest, WavNullOutput) {
    EXPECT_FALSE(DecodeAudioFile("C:\\nonexistent\\file.wav", NULL));
}

// ============================================================================
// Config edge cases
// ============================================================================

TEST_F(CorruptTest, ConfigEmptyFile) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\binkw32.cfg", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "w");
    ASSERT_NE(f, (FILE*)NULL);
    fclose(f);

    lstrcpynA(g_dllDir, TEST_DATA_DIR, MAX_PATH);
    strcat_s(g_dllDir, "\\");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 0);
    EXPECT_EQ(g_exceptionCount, 0);
    DeleteFileA(path);
}

TEST_F(CorruptTest, ConfigOnlyComments) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\binkw32.cfg", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "w");
    ASSERT_NE(f, (FILE*)NULL);
    fprintf(f, "; This is a comment\n# Another comment\n");
    fclose(f);

    lstrcpynA(g_dllDir, TEST_DATA_DIR, MAX_PATH);
    strcat_s(g_dllDir, "\\");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 0);
    DeleteFileA(path);
}

TEST_F(CorruptTest, ConfigNoTrailingNewline) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\binkw32.cfg", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "w");
    ASSERT_NE(f, (FILE*)NULL);
    fprintf(f, "[audio]\ntest.bik = test.wav");
    fclose(f);

    lstrcpynA(g_dllDir, TEST_DATA_DIR, MAX_PATH);
    strcat_s(g_dllDir, "\\");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
    EXPECT_STREQ(g_audioMaps[0].wavPath, "test.wav");
    DeleteFileA(path);
}

TEST_F(CorruptTest, ConfigBOMPrefixed) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\binkw32.cfg", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    uint8_t bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, f);
    fprintf(f, "[audio]\ntest.bik = test.wav");
    fclose(f);

    lstrcpynA(g_dllDir, TEST_DATA_DIR, MAX_PATH);
    strcat_s(g_dllDir, "\\");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 1);
    EXPECT_STREQ(g_audioMaps[0].bikName, "test.bik");
    DeleteFileA(path);
}

TEST_F(CorruptTest, ConfigShortBOM) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\binkw32.cfg", TEST_DATA_DIR);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    uint8_t data[] = {0xEF, 0xBB};
    fwrite(data, 1, 2, f);
    fclose(f);

    lstrcpynA(g_dllDir, TEST_DATA_DIR, MAX_PATH);
    strcat_s(g_dllDir, "\\");
    LoadAudioConfig();
    EXPECT_EQ(g_audioMapCount, 0);
    DeleteFileA(path);
}
