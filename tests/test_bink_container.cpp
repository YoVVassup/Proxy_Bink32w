#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstring>
#include <cstdio>

// ============================================================================
// Bink container format tests — using real .bik file
//
// Format reference: https://wiki.multimedia.cx/index.php/Bink_Container
//
// Header (44 bytes):
//   0-2:   'BIK' signature
//   3:     codec revision (b/d/f/g/h/i)
//   4-7:   file size - 8
//   8-11:  number of frames
//  12-15:  largest frame size
//  16-19:  number of frames (again)
//  20-23:  video width
//  24-27:  video height
//  28-31:  fps dividend
//  32-35:  fps divider
//  36-39:  video flags
//  40-43:  number of audio tracks
//
// s03_f00e.bik: 41,671,396 bytes, 1400×1080, 30fps, codec i, 1047 frames, 1 audio track
// ============================================================================

static std::string ThirdPartyPath(const char* filename) {
    return std::string(THIRD_PARTY_DIR) + "\\" + filename;
}

// ============================================================================
// ReadBinkHeaderFromPath tests with real .bik file
// ============================================================================

TEST(BinkContainer, ParseRealBikFile) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    BinkFileInfo info = ReadBinkHeaderFromPath(path.c_str());

    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.width, 1400u);
    EXPECT_EQ(info.height, 1080u);
    EXPECT_EQ(info.frameCount, 1047u);
    // fps = 30/1
    EXPECT_EQ(info.frameRate, 30u);
    EXPECT_EQ(info.frameRateDiv, 1u);
}

TEST(BinkContainer, HeaderSignature) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 0-2: 'BIK' = 0x42, 0x49, 0x4B
    EXPECT_EQ(hdr[0], 0x42);  // 'B'
    EXPECT_EQ(hdr[1], 0x49);  // 'I'
    EXPECT_EQ(hdr[2], 0x4B);  // 'K'
}

TEST(BinkContainer, CodecRevision) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Byte 3: codec revision — 'i' = 0x69
    EXPECT_EQ(hdr[3], 0x69);  // 'i'
}

TEST(BinkContainer, FileSizeField) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 4-7: file size - 8 (little-endian)
    uint32_t sizeField = ReadU32(hdr + 4);
    // Verify the field is reasonable (file size - 8)
    EXPECT_GT(sizeField, 1000000u);  // At least 1MB
    EXPECT_LT(sizeField, 200000000u); // Less than 200MB
}

TEST(BinkContainer, FrameCount) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 8-11: number of frames
    uint32_t frameCount = ReadU32(hdr + 8);
    EXPECT_EQ(frameCount, 1047u);

    // Bytes 16-19: number of frames (again — duplicate)
    uint32_t frameCount2 = ReadU32(hdr + 16);
    EXPECT_EQ(frameCount2, 1047u);
    EXPECT_EQ(frameCount, frameCount2);
}

TEST(BinkContainer, LargestFrameSize) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 12-15: largest frame size
    uint32_t largestFrame = ReadU32(hdr + 12);
    EXPECT_GT(largestFrame, 0u);
    EXPECT_LT(largestFrame, 1000000u);  // Less than 1MB for a single frame
}

TEST(BinkContainer, VideoDimensions) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 20-23: width
    uint32_t width = ReadU32(hdr + 20);
    EXPECT_EQ(width, 1400u);
    EXPECT_LE(width, 32767u);

    // Bytes 24-27: height
    uint32_t height = ReadU32(hdr + 24);
    EXPECT_EQ(height, 1080u);
    EXPECT_LE(height, 32767u);
}

TEST(BinkContainer, FrameRate) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 28-31: fps dividend
    uint32_t fpsDividend = ReadU32(hdr + 28);
    EXPECT_EQ(fpsDividend, 30u);

    // Bytes 32-35: fps divider
    uint32_t fpsDivider = ReadU32(hdr + 32);
    EXPECT_EQ(fpsDivider, 1u);

    // Effective fps = 30/1 = 30
    EXPECT_EQ(fpsDividend / fpsDivider, 30u);
}

TEST(BinkContainer, VideoFlags) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 36-39: video flags
    uint32_t flags = ReadU32(hdr + 36);
    // For this file, flags should be 0 (no scaling, no alpha, no grayscale)
    EXPECT_EQ(flags, 0u);
}

TEST(BinkContainer, AudioTrackCount) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    // Bytes 40-43: number of audio tracks
    uint32_t audioTracks = ReadU32(hdr + 40);
    EXPECT_EQ(audioTracks, 1u);
    EXPECT_LE(audioTracks, 256u);
}

// ============================================================================
// Audio track info parsing (after 44-byte header)
// ============================================================================

TEST(BinkContainer, AudioTrackInfo) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);

    uint32_t audioTracks = ReadU32(hdr + 40);
    ASSERT_EQ(audioTracks, 1u);

    // Audio track info follows immediately after the 44-byte header
    // Each track has: 2 bytes unknown + 2 bytes channels
    uint8_t trackInfo[4];
    fread(trackInfo, 1, 4, f);

    uint16_t channels = ReadU16(trackInfo + 2);
    EXPECT_GE(channels, 1);
    EXPECT_LE(channels, 2);

    fclose(f);
}

// ============================================================================
// Frame index table
// ============================================================================

TEST(BinkContainer, FrameIndexTable) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE);

    uint8_t hdr[44];
    DWORD read;
    ReadFile(hFile, hdr, 44, &read, NULL);

    uint32_t frameCount = ReadU32(hdr + 8);
    uint32_t audioTracks = ReadU32(hdr + 40);

    // Audio track info: 12 bytes per track (4 unknown+channels, 4 sampleRate+flags, 4 trackID)
    DWORD audioInfoSize = audioTracks * 12;
    DWORD indexOffset = 44 + audioInfoSize;

    // Frame index table: frameCount + 1 entries, each 4 bytes
    SetFilePointer(hFile, indexOffset, NULL, FILE_BEGIN);

    // First entry should be offset to first frame data
    uint8_t firstEntry[4];
    ReadFile(hFile, firstEntry, 4, &read, NULL);
    uint32_t firstOffset = ReadU32(firstEntry) & 0xFFFFFFFE; // mask keyframe bit
    // First frame should start after header + audio info + index table
    uint32_t minOffset = indexOffset + (frameCount + 1) * 4;
    EXPECT_GE(firstOffset, minOffset)
        << "First frame offset should be after header+index";

    // Last entry should equal file size
    WIN32_FILE_ATTRIBUTE_DATA fad;
    GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad);
    DWORD fileSize = fad.nFileSizeLow;

    SetFilePointer(hFile, indexOffset + frameCount * 4, NULL, FILE_BEGIN);
    uint8_t lastEntry[4];
    ReadFile(hFile, lastEntry, 4, &read, NULL);
    uint32_t lastOffset = ReadU32(lastEntry) & 0xFFFFFFFE;
    EXPECT_NEAR(lastOffset, fileSize, 8u);

    // Entries should be monotonically non-decreasing
    SetFilePointer(hFile, indexOffset, NULL, FILE_BEGIN);
    uint32_t prevOffset = 0;
    for (uint32_t i = 0; i <= frameCount; i++) {
        uint8_t entry[4];
        ReadFile(hFile, entry, 4, &read, NULL);
        uint32_t offset = ReadU32(entry) & 0xFFFFFFFE;
        EXPECT_GE(offset, prevOffset)
            << "Frame index not monotonic at entry " << i;
        prevOffset = offset;
    }

    CloseHandle(hFile);
}

TEST(BinkContainer, KeyframeBit) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);

    uint8_t hdr[44];
    fread(hdr, 1, 44, f);

    uint32_t frameCount = ReadU32(hdr + 8);
    uint32_t audioTracks = ReadU32(hdr + 40);
    DWORD headerSize = 44 + audioTracks * 12;

    // Read first few frame index entries and check keyframe bit
    SetFilePointer(f, headerSize, NULL, FILE_BEGIN);
    int keyframeCount = 0;
    int nonKeyframeCount = 0;

    for (uint32_t i = 0; i < 10 && i <= frameCount; i++) {
        uint8_t entry[4];
        fread(entry, 1, 4, f);
        uint32_t raw = ReadU32(entry);
        if (raw & 1) keyframeCount++;
        else nonKeyframeCount++;
    }

    // At least first frame should be keyframe
    EXPECT_GT(keyframeCount, 0);
}

// ============================================================================
// ReadBinkHeaderFromFile — file pointer restoration
// ============================================================================

TEST(BinkContainer, FilePointerRestored) {
    std::string path = ThirdPartyPath("s03_f00e.bik");
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE);

    // Seek to a known position
    SetFilePointer(hFile, 100, NULL, FILE_BEGIN);
    DWORD posBefore = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);

    // Call ReadBinkHeaderFromFile
    BinkFileInfo info = ReadBinkHeaderFromFile(hFile);

    // File pointer should be restored
    DWORD posAfter = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
    EXPECT_EQ(posBefore, posAfter);

    CloseHandle(hFile);
}

TEST(BinkContainer, FilePointerRestoredOnInvalidFile) {
    // Test with a non-Bink file
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "test_notbik.bin");
    FILE* f = NULL;
    fopen_s(&f, tempPath, "wb");
    if (f) {
        uint8_t garbage[44] = {0};
        fwrite(garbage, 1, 44, f);
        fclose(f);
    }

    HANDLE hFile = CreateFileA(tempPath, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 50, NULL, FILE_BEGIN);
        DWORD posBefore = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);

        BinkFileInfo info = ReadBinkHeaderFromFile(hFile);

        DWORD posAfter = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
        EXPECT_EQ(posBefore, posAfter);

        CloseHandle(hFile);
    }
    DeleteFileA(tempPath);
}

// ============================================================================
// Bink marker validation — different codec revisions
// ============================================================================

TEST(BinkContainer, ValidMarkers) {
    // Valid Bink markers as stored on disk: BIK + revision byte
    // ReadU32 returns little-endian: 'B'=0x42, 'I'=0x49, 'K'=0x4B, rev=0x66-0x69
    // So the uint32 value is 0x6X424942 (e.g. 'fKIB'=0x66424942)
    struct { uint32_t value; uint8_t bytes[4]; } validMarkers[] = {
        {0x66424942, {0x42, 0x49, 0x4B, 0x66}},  // BIKf
        {0x67424942, {0x42, 0x49, 0x4B, 0x67}},  // BIKg
        {0x68424942, {0x42, 0x49, 0x4B, 0x68}},  // BIKh
        {0x69424942, {0x42, 0x49, 0x4B, 0x69}},  // BIKi
    };

    for (const auto& m : validMarkers) {
        uint8_t hdr[44] = {0};
        memcpy(hdr, m.bytes, 4);
        hdr[20] = 0x80; hdr[21] = 0x02;  // width = 640
        hdr[24] = 0xE0; hdr[25] = 0x01;  // height = 480

        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\test_marker_%08X.bik", TEST_DATA_DIR, m.value);
        FILE* f = NULL;
        fopen_s(&f, path, "wb");
        if (f) {
            fwrite(hdr, 1, 44, f);
            fclose(f);

            BinkFileInfo info = ReadBinkHeaderFromPath(path);
            EXPECT_TRUE(info.valid) << "Marker 0x" << std::hex << m.value << " rejected";
            DeleteFileA(path);
        }
    }
}

TEST(BinkContainer, InvalidMarkersRejected) {
    // Invalid markers: bytes that don't match BIK + valid revision
    struct { uint32_t value; uint8_t bytes[4]; } invalidMarkers[] = {
        {0x00000000, {0x00, 0x00, 0x00, 0x00}},  // null
        {0x41524544, {0x44, 0x45, 0x52, 0x41}},  // 'AREA'
        {0x4D504547, {0x47, 0x45, 0x50, 0x4D}},  // 'GEMP'
        {0xFFFFFFFF, {0xFF, 0xFF, 0xFF, 0xFF}},  // all 0xFF
    };

    for (const auto& m : invalidMarkers) {
        uint8_t hdr[44] = {0};
        memcpy(hdr, m.bytes, 4);
        hdr[20] = 0x80; hdr[21] = 0x02;
        hdr[24] = 0xE0; hdr[25] = 0x01;

        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\test_inv_%08X.bik", TEST_DATA_DIR, m.value);
        FILE* f = NULL;
        fopen_s(&f, path, "wb");
        if (f) {
            fwrite(hdr, 1, 44, f);
            fclose(f);

            BinkFileInfo info = ReadBinkHeaderFromPath(path);
            EXPECT_FALSE(info.valid) << "Marker 0x" << std::hex << m.value << " accepted";
            DeleteFileA(path);
        }
    }
}

// ============================================================================
// File size consistency
// ============================================================================

TEST(BinkContainer, FileSizeConsistent) {
    std::string path = ThirdPartyPath("s03_f00e.bik");

    // Get actual file size
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ASSERT_TRUE(GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad));
    ULONGLONG actualSize = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;

    // Read size field from header
    FILE* f = NULL;
    fopen_s(&f, path.c_str(), "rb");
    ASSERT_NE(f, (FILE*)NULL);
    uint8_t hdr[44];
    fread(hdr, 1, 44, f);
    fclose(f);

    uint32_t sizeField = ReadU32(hdr + 4);
    EXPECT_EQ(actualSize, (ULONGLONG)(sizeField + 8));
}
