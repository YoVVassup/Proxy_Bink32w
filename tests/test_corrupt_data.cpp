#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstdio>
#include <cstring>
#include <string>

// ============================================================================
// test_corrupt_data.cpp — Negative tests for corrupt/malformed files
// ============================================================================

extern BOOL g_logEnabled;

// --- Test-only hooks from logging.cpp (see WarnLogOpenFailed) ---
extern const char* LogOpenFailureForTest();
extern int LogOpenWarnCountForTest();
extern void LogResetOpenFailureForTest();

namespace {

// Whole file as a string ("" when it does not exist).
std::string ReadWholeFile(const char* path) {
    FILE* f = NULL;
    fopen_s(&f, path, "rb");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT (little-endian first DWORD).
const unsigned char kPcmGuid[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
const unsigned char kFloatGuid[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

// Structurally valid RIFF/WAVE with caller-chosen fmt fields (correct chunk
// headers, correct offsets). The old hand-rolled arrays skipped the "fmt " /
// "data" chunk ids entirely, so the parser bailed on !foundFmt and never
// reached the guard under test. `ext`/`extLen` append the WAVEFORMATEXTENSIBLE
// extension to the fmt chunk (40-byte chunk total with extLen == 24).
BOOL WriteRawWav(const char* path, uint16_t tag, uint16_t channels, uint32_t rate,
                 uint16_t bits, const void* ext, uint32_t extLen) {
    uint32_t blockAlign = (uint32_t)channels * bits / 8;
    uint32_t avgBytesPerSec = rate * blockAlign;
    uint32_t fmtSize = 16 + extLen;
    uint32_t dataSize = 96;
    uint32_t riffSize = 4 + (8 + fmtSize) + (8 + dataSize);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    if (!f) return FALSE;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&tag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&avgBytesPerSec, 4, 1, f);
    uint16_t align = (uint16_t)blockAlign;
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    if (ext && extLen) fwrite(ext, 1, extLen, f);
    fwrite("data", 1, 4, f);
    uint32_t ds = dataSize;
    fwrite(&ds, 4, 1, f);
    char payload[96] = {0};
    fwrite(payload, 1, dataSize, f);
    fclose(f);
    return TRUE;
}

// 24-byte WAVEFORMATEXTENSIBLE extension: cbSize, wValidBitsPerSample,
// dwChannelMask, SubFormat GUID.
void MakeExtensibleExt(char ext24[24], uint16_t validBits, uint32_t channelMask,
                       const unsigned char guid[16]) {
    memset(ext24, 0, 24);
    uint16_t cbSize = 22;
    memcpy(ext24 + 0, &cbSize, 2);
    memcpy(ext24 + 2, &validBits, 2);
    memcpy(ext24 + 4, &channelMask, 4);
    memcpy(ext24 + 8, guid, 16);
}

// Routes Log()/LogF() into tests\data\binkw32_proxy.log and counts entries
// (same trick as LogCapture in test_uncovered.cpp; that one is not reachable
// from this translation unit).
class LogCapture {
public:
    LogCapture() {
        savedEnabled = g_logEnabled;
        memcpy(savedDir, g_dllDir, MAX_PATH);
        ShutdownLog();                       // close whatever the previous test left open
        _snprintf_s(dir, sizeof(dir), _TRUNCATE, "%s\\", TestDataDir());
        lstrcpynA(g_dllDir, dir, MAX_PATH);
        path = std::string(g_dllDir) + "binkw32_proxy.log";
        DeleteFileA(path.c_str());
        g_logEnabled = TRUE;
        LogResetOpenFailureForTest();
        InitLog();
    }
    ~LogCapture() {
        ShutdownLog();
        g_logEnabled = savedEnabled;
        lstrcpynA(g_dllDir, savedDir, MAX_PATH);
        LogResetOpenFailureForTest();
    }
    int Count(const char* needle) const {
        // The logger holds the file open for writing; CRT fopen() would refuse
        // that share mode, so read through CreateFile with read+write sharing.
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return -1;
        std::string content;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n > 0) content.append(buf, n);
        CloseHandle(h);
        int count = 0;
        size_t pos = 0;
        while ((pos = content.find(needle, pos)) != std::string::npos) {
            count++;
            pos += strlen(needle);
        }
        return count;
    }

private:
    BOOL savedEnabled;
    char savedDir[MAX_PATH];
    char dir[MAX_PATH];
    std::string path;
};

} // namespace

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
    // Non-zero fileCount: the @0-1 signature check is the only thing that can
    // reject this archive (the count check alone used to make this test pass).
    data[4] = 0x01;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 100, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

// Legacy TS/TD layout (count@0) or garbage: @0-1 must be the 0x0000 extended
// signature, otherwise the index would be read from the wrong place.
TEST_F(CorruptTest, MixLegacyHeaderRejected) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\legacy_hdr.mix", TEST_DATA_DIR);
    uint8_t data[100] = {0};
    data[0] = 0x10;   // firstField != 0 — not an extended signature
    data[4] = 0x01;   // fileCount = 1 (would otherwise be accepted)
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 100, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

// Declared body_size reaching past EOF must NOT reject the archive: protection
// deliberately corrupts header fields (incl. body_size) while the game ignores
// @6 entirely. The archive parses; entries that do not fit the real file are
// dropped individually (MixEntryPastEofDropped).
TEST_F(CorruptTest, MixBodySizePastEofStillParses) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\body_past_eof.mix", TEST_DATA_DIR);
    uint8_t data[34] = {0};
    data[4] = 0x01;   // fileCount = 1 (index: 10 + 12 = 22 <= 34)
    data[6] = 0xFF;   // body_size = 0xFFFFFFFF -> ends far past EOF
    data[7] = 0xFF;
    data[8] = 0xFF;
    data[9] = 0xFF;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    ASSERT_NE(result, (MixArchive*)NULL) << "bogus body_size must only warn";
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->fileCount, 1);
    DeleteFileA(path);
}

// Structurally valid archive whose single entry points past EOF: the archive
// still parses, but the entry must be dropped (size=0) so no filePos can land
// in a range that the file does not actually contain.
TEST_F(CorruptTest, MixEntryPastEofDropped) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\entry_past_eof.mix", TEST_DATA_DIR);
    uint8_t data[34] = {0};
    data[4] = 0x01;   // fileCount = 1, body_size = 0 (fits)
    // entry @0xA: crc + offset + size (little-endian)
    data[10] = 0x11; data[11] = 0x22; data[12] = 0x33; data[13] = 0x44;
    data[14] = 0x00; data[15] = 0xFF; data[16] = 0xFF; data[17] = 0xFF; // offset = 0xFFFFFF00
    data[18] = 0x00; data[19] = 0x10; data[20] = 0x00; data[21] = 0x00; // size   = 0x1000
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    ASSERT_NE(result, (MixArchive*)NULL);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->fileCount, 1);
    ASSERT_NE(result->entries, (MixEntry*)NULL);
    EXPECT_EQ(result->entries[0].crc, 0x44332211u);
    EXPECT_EQ(result->entries[0].size, 0u) << "out-of-range entry must be dropped";

    char outName[128] = "not-empty";
    BOOL found = FindBikNameInMix(path, 34, outName, sizeof(outName));
    EXPECT_FALSE(found);
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
    // Bink 1 revisions in the wild: b, d, f, g, h, i, k (ffmpeg demuxer and
    // vgmstream agree; 'j' is Bink 2 only). b/d/k were rejected until the
    // marker fix, so they are listed here on purpose.
    uint8_t markers[][4] = {
        {0x42, 0x49, 0x4B, 0x62},  // BIKb
        {0x42, 0x49, 0x4B, 0x64},  // BIKd
        {0x42, 0x49, 0x4B, 0x66},
        {0x42, 0x49, 0x4B, 0x67},
        {0x42, 0x49, 0x4B, 0x68},
        {0x42, 0x49, 0x4B, 0x69},
        {0x42, 0x49, 0x4B, 0x6B},  // BIKk
    };
    const int markerCount = (int)(sizeof(markers) / sizeof(markers[0]));
    for (int m = 0; m < markerCount; m++) {
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
    // Still refused after the marker fix: 'a' and 'j' are Bink 2 revisions
    // (Bink 2 uses the 'KB2' magic), 0xFF is not a revision at all, and the
    // remaining two are not BIK at all. BIKb used to be listed here — it is a
    // real Bink 1 revision and moved to BikAllValidMarkers.
    uint8_t markers[][4] = {
        {0x42, 0x49, 0x4B, 0x61},
        {0x42, 0x49, 0x4B, 0x6A},
        {0x42, 0x49, 0x4B, 0xFF},
        {0x00, 0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0xFF, 0xFF},
    };
    const int markerCount = (int)(sizeof(markers) / sizeof(markers[0]));
    for (int m = 0; m < markerCount; m++) {
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
    // Properly formed WAV: RIFF/fmt/data chunk headers present, channels=9 at
    // the real fmt offset (+22). The previous version wrote 9 at offset 24
    // (the sample rate) with no chunk ids at all, so the parser bailed on
    // !foundFmt and the channels>8 guard never ran.
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\many_ch.wav", TEST_DATA_DIR);
    ASSERT_TRUE(WriteRawWav(path, 1 /*PCM*/, 9, 22050, 16, NULL, 0));

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio))
        << "9 channels must be refused by the decoder's channels>8 guard";
    EXPECT_EQ(audio.pcmData, (char*)NULL) << "FALSE must leave the output empty";
    DeleteFileA(path);

    // Twin through the same writer: a legal stereo file must decode, proving
    // the refusal above is about the channel count and not about the writer.
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\many_ch_ok.wav", TEST_DATA_DIR);
    ASSERT_TRUE(WriteRawWav(path, 1, 2, 22050, 16, NULL, 0));
    DecodedAudio ok = {0};
    EXPECT_TRUE(DecodeAudioFile(path, &ok));
    if (ok.pcmData) VirtualFree(ok.pcmData, 0, MEM_RELEASE);
    DeleteFileA(path);
}

// Both refusal sites must name their cause: the decoder guard (channels>8 /
// non 8/16-bit) used to reject silently, so an operator could not tell a
// decoder limit from a waveOut format limit.
TEST_F(CorruptTest, WavChannelGuardsNameTheirCause) {
    LogCapture log;
    char path[MAX_PATH];

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\guard9.wav", TEST_DATA_DIR);
    ASSERT_TRUE(WriteRawWav(path, 1, 9, 22050, 16, NULL, 0));
    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    DeleteFileA(path);
    EXPECT_GE(log.Count("WAV decoder limit"), 1)
        << "the decoder guard must log why it refused (channels>8)";

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\guard6.wav", TEST_DATA_DIR);
    ASSERT_TRUE(WriteRawWav(path, 1, 6, 22050, 16, NULL, 0));
    DecodedAudio audio2 = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio2));
    DeleteFileA(path);
    EXPECT_GE(log.Count("mono/stereo only"), 1)
        << "6 channels passes the decoder guard, so ValidatePlayable must name its cause";
}

// WAVE_FORMAT_EXTENSIBLE with the PCM sub-format is ordinary PCM and must be
// accepted (it is what most modern encoders emit for 16-bit WAV files); it was
// rejected at the tag check before any GUID was looked at.
TEST_F(CorruptTest, WavExtensiblePcmAccepted) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\ext_pcm.wav", TEST_DATA_DIR);
    char ext[24];
    MakeExtensibleExt(ext, 16, 0x3 /* FL | FR */, kPcmGuid);
    ASSERT_TRUE(WriteRawWav(path, 0xFFFE, 2, 44100, 16, ext, sizeof(ext)));

    DecodedAudio audio = {0};
    EXPECT_TRUE(DecodeAudioFile(path, &audio))
        << "WAVE_FORMAT_EXTENSIBLE + PCM GUID is PCM";
    EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
    EXPECT_EQ(audio.format.nChannels, 2);
    EXPECT_EQ(audio.format.nSamplesPerSec, 44100u);
    EXPECT_EQ(audio.format.wBitsPerSample, 16);
    EXPECT_GT(audio.pcmSize, 0u);
    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
    DeleteFileA(path);
}

// ...but only PCM: a non-PCM sub-format (here: IEEE float) must be refused
// with a message that names it, not silently dropped at the tag check.
TEST_F(CorruptTest, WavExtensibleNonPcmRejected) {
    LogCapture log;
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\ext_float.wav", TEST_DATA_DIR);
    char ext[24];
    MakeExtensibleExt(ext, 32, 0x3, kFloatGuid);
    ASSERT_TRUE(WriteRawWav(path, 0xFFFE, 2, 44100, 16, ext, sizeof(ext)));

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
    EXPECT_EQ(audio.pcmData, (char*)NULL);
    DeleteFileA(path);
    EXPECT_GE(log.Count("WAVE_FORMAT_EXTENSIBLE"), 1)
        << "the refusal must name the format instead of failing silently";
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

// ============================================================================
// Log rotation / open failures (README: "rotates automatically on startup")
// ============================================================================

// Unique per-call temp dir: logging keeps a once-per-path latch in statics, so
// the log tests must not share a path with any other test.
static BOOL MakeTempLogDir(char* dir, size_t dirSize) {
    char tmpBase[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpBase);
    static volatile LONG seq = 0;
    _snprintf_s(dir, dirSize, _TRUNCATE, "%sbink32w_logtest_%u_%u\\",
                tmpBase, GetTickCount(), (unsigned)InterlockedIncrement(&seq));
    return CreateDirectoryA(dir, NULL);
}

static void CloseLogHandle() {
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

static void CleanupDir(const char* dir, const char* logPath) {
    DeleteFileA(logPath);
    for (int i = 1; i <= 10; i++) {
        char p[MAX_PATH];
        _snprintf_s(p, sizeof(p), _TRUNCATE, "%s.%d", logPath, i);
        DeleteFileA(p);
    }
    RemoveDirectoryA(dir);
}

// CREATE_ALWAYS used to truncate binkw32_proxy.log on every process start,
// destroying the previous run's log even though README documents a startup
// rotation into .log.1.
TEST_F(CorruptTest, StartupRotationPreservesPreviousLog) {
    char dir[MAX_PATH];
    ASSERT_TRUE(MakeTempLogDir(dir, sizeof(dir)));

    char logPath[MAX_PATH];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", dir);
    {
        FILE* f = NULL;
        fopen_s(&f, logPath, "wb");
        ASSERT_NE(f, (FILE*)NULL);
        fputs("previous run\n", f);
        fclose(f);
    }

    lstrcpynA(g_dllDir, dir, MAX_PATH);   // CorruptTest::TearDown restores it
    CloseLogHandle();
    g_logEnabled = TRUE;
    LogResetOpenFailureForTest();

    InitLog();
    Log("new run line");
    ShutdownLog();                        // flush + close (also sets enabled=FALSE)

    char rotPath[MAX_PATH];
    _snprintf_s(rotPath, sizeof(rotPath), _TRUNCATE, "%s.1", logPath);
    EXPECT_NE(ReadWholeFile(rotPath).find("previous run"), std::string::npos)
        << "startup must move the previous run's log to .log.1, not truncate it";
    EXPECT_NE(ReadWholeFile(logPath).find("new run line"), std::string::npos)
        << "the new run must write into a fresh .log";

    LogResetOpenFailureForTest();
    CleanupDir(dir, logPath);
}

// A log file that cannot be opened must be reported — once — somewhere the
// user can see (debugger/stderr: the log itself is the thing that failed),
// instead of being retried silently on every Log().
TEST_F(CorruptTest, LogOpenFailureIsReportedOnce) {
    char bogus[MAX_PATH];
    _snprintf_s(bogus, sizeof(bogus), _TRUNCATE,
                "C:\\bink32w_no_such_dir_%u\\", GetTickCount());
    lstrcpynA(g_dllDir, bogus, MAX_PATH);   // CorruptTest::TearDown restores it
    CloseLogHandle();
    g_logEnabled = TRUE;
    LogResetOpenFailureForTest();

    LogF("first open attempt");

    const char* msg = LogOpenFailureForTest();
    ASSERT_NE(msg, (const char*)NULL);
    EXPECT_NE(strstr(msg, "cannot open log file"), (const char*)NULL)
        << "an unwritable log must say so instead of swallowing output silently";
    EXPECT_EQ(LogOpenWarnCountForTest(), 1);

    LogF("second open attempt");
    EXPECT_EQ(LogOpenWarnCountForTest(), 1)
        << "the warning must be emitted once per failure, not once per Log()";

    char probe[MAX_PATH];
    _snprintf_s(probe, sizeof(probe), _TRUNCATE, "%sbinkw32_proxy.log", bogus);
    EXPECT_EQ(GetFileAttributesA(probe), INVALID_FILE_ATTRIBUTES);

    ShutdownLog();
    LogResetOpenFailureForTest();
}

// When the 10 MB log is locked, every Log() used to re-run the rotation chain
// (history slid past .log.9 and vanished while nothing could be written) and
// gave no feedback at all. One rotation attempt + one warning, then stop.
TEST_F(CorruptTest, LockedLogWarnsAndIsNotRepeatedlyRotated) {
    char dir[MAX_PATH];
    ASSERT_TRUE(MakeTempLogDir(dir, sizeof(dir)));

    char logPath[MAX_PATH];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%sbinkw32_proxy.log", dir);
    {
        FILE* f = NULL;
        fopen_s(&f, logPath, "wb");
        ASSERT_NE(f, (FILE*)NULL);
        fputs("main\n", f);
        char buf[65536];
        memset(buf, 'x', sizeof(buf));
        for (int i = 0; i < 160; i++) fwrite(buf, 1, sizeof(buf), f);  // 10 MB
        fclose(f);
    }
    char p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH];
    _snprintf_s(p1, sizeof(p1), _TRUNCATE, "%s.1", logPath);
    _snprintf_s(p2, sizeof(p2), _TRUNCATE, "%s.2", logPath);
    _snprintf_s(p3, sizeof(p3), _TRUNCATE, "%s.3", logPath);
    {
        FILE* f = NULL;
        fopen_s(&f, p1, "wb"); ASSERT_NE(f, (FILE*)NULL);
        fputs("ONE", f); fclose(f);
        fopen_s(&f, p2, "wb"); ASSERT_NE(f, (FILE*)NULL);
        fputs("TWO", f); fclose(f);
    }

    // Hold the log without FILE_SHARE_DELETE/WRITE: rotation and CREATE_ALWAYS
    // both have to fail, exactly like another process owning the file.
    HANDLE lock = CreateFileA(logPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);

    lstrcpynA(g_dllDir, dir, MAX_PATH);   // CorruptTest::TearDown restores it
    CloseLogHandle();
    g_logEnabled = TRUE;
    LogResetOpenFailureForTest();

    LogF("attempt 1");
    LogF("attempt 2");
    LogF("attempt 3");

    EXPECT_EQ(LogOpenWarnCountForTest(), 1)
        << "an unopenable log must be reported once, not on every Log()";
    EXPECT_NE(strstr(LogOpenFailureForTest(), "cannot open log file"), (const char*)NULL);
    EXPECT_NE(ReadWholeFile(p2).find("ONE"), std::string::npos)
        << "the chain must shift exactly once (startup), not on every Log()";
    EXPECT_NE(ReadWholeFile(p3).find("TWO"), std::string::npos)
        << "history must not slide past .log.9 while the log stays unwritable";

    CloseHandle(lock);
    ShutdownLog();
    LogResetOpenFailureForTest();
    CleanupDir(dir, logPath);
}
