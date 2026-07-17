#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// test_binkioprocessor.cpp — Tests for BINKIOPROCESSOR (0x02000000) flag handling
//
// When BinkOpen is called with BINKIOPROCESSOR flag, the first parameter
// is a custom IO context (e.g. CCFileClass*), NOT a filename string or HANDLE.
// The proxy must not try to interpret it as a string.
// ============================================================================

// ============================================================================
// ExtractFileName tests for BINKIOPROCESSOR
// ============================================================================

TEST(ExtractFileNameTest, BinkIOProcessorReturnsEmpty) {
    char out[MAX_PATH] = {0};
    // Simulate BINKIOPROCESSOR: first param is arbitrary pointer, not a string
    void* fakeContext = (void*)0x12345678;
    DWORD flags = 0x02000000;

    ExtractFileName(fakeContext, flags, out, sizeof(out));

    // Should NOT try to read fakeContext as a string
    EXPECT_EQ(out[0], '\0');
}

TEST(ExtractFileNameTest, BinkIOProcessorWithOtherFlags) {
    char out[MAX_PATH] = {0};
    void* fakeContext = (void*)0xDEADBEEF;
    // BINKIOPROCESSOR | some other flag
    DWORD flags = 0x02000000 | 0x00000001;

    ExtractFileName(fakeContext, flags, out, sizeof(out));

    EXPECT_EQ(out[0], '\0');
}

TEST(ExtractFileNameTest, FromMemoryReturnsEmpty) {
    char out[MAX_PATH] = {0};
    void* memBuf = (void*)0xAABBCCDD;
    DWORD flags = 0x04000000; // BINK_FROM_MEMORY

    ExtractFileName(memBuf, flags, out, sizeof(out));

    EXPECT_EQ(out[0], '\0');
}

TEST(ExtractFileNameTest, FromMemoryWithIOProcessor) {
    char out[MAX_PATH] = {0};
    void* memBuf = (void*)0xAABBCCDD;
    // BINK_FROM_MEMORY | BINKIOPROCESSOR
    DWORD flags = 0x04000000 | 0x02000000;

    ExtractFileName(memBuf, flags, out, sizeof(out));

    EXPECT_EQ(out[0], '\0');
}

TEST(ExtractFileNameTest, FileNameStringStillWorks) {
    char out[MAX_PATH] = {0};
    const char* filename = "test.bik";
    DWORD flags = 0; // No special flags

    ExtractFileName((void*)filename, flags, out, sizeof(out));

    EXPECT_STREQ(out, "test.bik");
}

TEST(ExtractFileNameTest, NullPointerWithNoFlags) {
    char out[MAX_PATH] = {0};
    out[0] = 'X'; // Pre-fill to ensure it gets cleared

    ExtractFileName(NULL, 0, out, sizeof(out));

    EXPECT_EQ(out[0], '\0');
}

// ============================================================================
// BinkOpen flag parsing logic tests (extracted from sBinkOpen)
//
// These test the flag-checking logic without calling the real BinkOpen.
// We verify that the proxy correctly identifies parameter types.
// ============================================================================

TEST(BinkOpenFlagsTest, IOProcessorFlagDetected) {
    DWORD flags = 0x02000000;

    // Simulate the logic from sBinkOpen
    bool isFileHandle = (flags & 0x00800000) != 0;
    bool isFromMemory = (flags & 0x04000000) != 0;
    bool isIOProcessor = (flags & 0x02000000) != 0;

    EXPECT_FALSE(isFileHandle);
    EXPECT_FALSE(isFromMemory);
    EXPECT_TRUE(isIOProcessor);
}

TEST(BinkOpenFlagsTest, FileHandleFlagDetected) {
    DWORD flags = 0x00800000;

    bool isFileHandle = (flags & 0x00800000) != 0;
    bool isFromMemory = (flags & 0x04000000) != 0;
    bool isIOProcessor = (flags & 0x02000000) != 0;

    EXPECT_TRUE(isFileHandle);
    EXPECT_FALSE(isFromMemory);
    EXPECT_FALSE(isIOProcessor);
}

TEST(BinkOpenFlagsTest, FromMemoryFlagDetected) {
    DWORD flags = 0x04000000;

    bool isFileHandle = (flags & 0x00800000) != 0;
    bool isFromMemory = (flags & 0x04000000) != 0;
    bool isIOProcessor = (flags & 0x02000000) != 0;

    EXPECT_FALSE(isFileHandle);
    EXPECT_TRUE(isFromMemory);
    EXPECT_FALSE(isIOProcessor);
}

TEST(BinkOpenFlagsTest, CombinedFlags) {
    // Simulate: BINKIOPROCESSOR | BINK_FROM_MEMORY
    DWORD flags = 0x02000000 | 0x04000000;

    bool isFileHandle = (flags & 0x00800000) != 0;
    bool isFromMemory = (flags & 0x04000000) != 0;
    bool isIOProcessor = (flags & 0x02000000) != 0;

    EXPECT_FALSE(isFileHandle);
    EXPECT_TRUE(isFromMemory);
    EXPECT_TRUE(isIOProcessor);
}

TEST(BinkOpenFlagsTest, ZeroFlagsIsNormalFile) {
    DWORD flags = 0;

    bool isFileHandle = (flags & 0x00800000) != 0;
    bool isFromMemory = (flags & 0x04000000) != 0;
    bool isIOProcessor = (flags & 0x02000000) != 0;

    EXPECT_FALSE(isFileHandle);
    EXPECT_FALSE(isFromMemory);
    EXPECT_FALSE(isIOProcessor);
}

// ============================================================================
// Parameter interpretation test
//
// Verifies that with BINKIOPROCESSOR, the first parameter is NOT treated
// as a filename string (which would cause a crash or garbage read).
// ============================================================================

TEST(BinkOpenParamTest, IOProcessorParamNotTreatedAsString) {
    DWORD flags = 0x02000000;
    void* param = (void*)0x00000001; // Not a valid string pointer

    // The corrected logic: only treat param as string if no special flags
    const char* bikName = NULL;
    if (param && !(flags & 0x04000000) && !(flags & 0x02000000)) {
        bikName = (const char*)param;
    }

    // With BINKIOPROCESSOR, bikName should remain NULL
    EXPECT_EQ(bikName, (const char*)NULL);
}

TEST(BinkOpenParamTest, NormalFlagTreatedAsString) {
    DWORD flags = 0;
    const char* filename = "movie.bik";
    void* param = (void*)filename;

    const char* bikName = NULL;
    if (param && !(flags & 0x04000000) && !(flags & 0x02000000)) {
        bikName = (const char*)param;
    }

    EXPECT_STREQ(bikName, "movie.bik");
}

TEST(BinkOpenParamTest, FileHandleNotTreatedAsString) {
    DWORD flags = 0x00800000;
    void* param = (void*)0x1234; // HANDLE value

    // In sBinkOpen, BINK_FILE_HANDLE goes to a separate branch
    // and param is never used as a string. Simulate that logic:
    bool isFileHandle = (flags & 0x00800000) != 0;
    const char* bikName = NULL;
    if (!isFileHandle && param && !(flags & 0x04000000) && !(flags & 0x02000000)) {
        bikName = (const char*)param;
    }

    // FileHandle flag is handled separately, not as string
    EXPECT_EQ(bikName, (const char*)NULL);
}

// ============================================================================
// ExtractNameFromCCFileClass tests
// ============================================================================

TEST(ExtractNameFromCCFileClassTest, NullPointerReturnsFalse) {
    char out[MAX_PATH] = {0};
    EXPECT_FALSE(ExtractNameFromCCFileClass(NULL, out, sizeof(out)));
    EXPECT_EQ(out[0], '\0');
}

TEST(ExtractNameFromCCFileClassTest, InvalidPointerReturnsFalse) {
    char out[MAX_PATH] = {0};
    EXPECT_FALSE(ExtractNameFromCCFileClass((void*)0x00000001, out, sizeof(out)));
    EXPECT_EQ(out[0], '\0');
}

TEST(ExtractNameFromCCFileClassTest, NullOutputReturnsFalse) {
    EXPECT_FALSE(ExtractNameFromCCFileClass((void*)0x12345678, NULL, MAX_PATH));
}

TEST(ExtractNameFromCCFileClassTest, ZeroSizeReturnsFalse) {
    char out[MAX_PATH] = {0};
    EXPECT_FALSE(ExtractNameFromCCFileClass((void*)0x12345678, out, 0));
}

TEST(ExtractNameFromCCFileClassTest, GarbagePointerReturnsFalse) {
    char out[MAX_PATH] = {0};
    // Unmapped memory
    EXPECT_FALSE(ExtractNameFromCCFileClass((void*)0x00000010, out, sizeof(out)));
    EXPECT_EQ(out[0], '\0');
}
