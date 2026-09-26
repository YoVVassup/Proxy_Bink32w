#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// Tests for MixCrc32 — RA2 .mix filename CRC32 computation
//
// The RA2/TS convention (OpenRA PackageEntry.HashFilename):
//   1. Uppercase the name.
//   2. If len % 4 != 0: append the byte (len % 4), then repeat
//      name[len & ~3] until the length is a multiple of 4.
//   3. Compute CRC32 over the padded string.
//
// Reference: real test.mix LMD entry "local mix database.dat" -> 0x366E051F.
// ============================================================================

TEST(MixCrc32, EmptyString) {
    // CRC32 of empty string is well-defined: 0xFFFFFFFF (initial) → ~0xFFFFFFFF = 0
    uint32_t crc = MixCrc32("");
    EXPECT_EQ(crc, 0u);
}

TEST(MixCrc32, KnownLmdFilename) {
    // Real test.mix LMD: "local mix database.dat" (22 chars, mod4=2) -> 0x366E051F.
    // This exercises the len%4!=0 padding path against a real on-disk hash.
    // (The filename is stored lowercase in the LMD; CRC uses uppercase.)
    uint32_t crc = MixCrc32("local mix database.dat");
    EXPECT_EQ(crc, 0x366E051Fu);
}

TEST(MixCrc32, CaseInsensitive) {
    // RA2 converts to uppercase before CRC32
    uint32_t lower = MixCrc32("abcdef");
    uint32_t upper = MixCrc32("ABCDEF");
    EXPECT_EQ(lower, upper);
}

TEST(MixCrc32, PaddingCorrectness) {
    // TS padding: pad byte = (len % 4), then repeat boundary char to multiple of 4.
    // "a"  (len=1): padTotal=3 -> "A" + 0x01 + "AA"      (4 bytes)
    // "aa" (len=2): padTotal=2 -> "AA" + 0x02 + "A"       (4 bytes)
    // "aaa"(len=3): padTotal=1 -> "AAA" + 0x03            (4 bytes)
    // "aaaa"(len=4): no pad   -> "AAAA"                   (4 bytes)
    // Each padded input differs (length byte / boundary char), so all CRCs differ.
    uint32_t a1 = MixCrc32("a");
    uint32_t a2 = MixCrc32("aa");
    uint32_t a3 = MixCrc32("aaa");
    uint32_t a4 = MixCrc32("aaaa");
    EXPECT_NE(a1, a2);
    EXPECT_NE(a2, a3);
    EXPECT_NE(a1, a3);
    EXPECT_NE(a3, a4);  // "AAA"+0x03 vs "AAAA"
}

TEST(MixCrc32, ConsistencyWithLmdEntries) {
    // From the log output, expandmo11.mix has these known CRC->name mappings:
    // CRC=0x92A0FBFC -> a04_f03e.bik (12 chars, mod4=0, no padding)
    // We can verify our MixCrc32 produces the same CRC for the same name.
    uint32_t crc = MixCrc32("a04_f03e.bik");
    EXPECT_EQ(crc, 0x92A0FBFCu);
}

TEST(MixCrc32, ConsistencyMultipleEntries) {
    // More known entries from expandmo11.mix log:
    uint32_t crc1 = MixCrc32("a12_f00e.bik");
    EXPECT_EQ(crc1, 0xD8A426D5u);

    uint32_t crc2 = MixCrc32("a00_f00e.bik");
    EXPECT_EQ(crc2, 0x1DDF2928u);

    uint32_t crc3 = MixCrc32("a01_f00e.bik");
    EXPECT_EQ(crc3, 0xF21D4216u);
}

TEST(MixCrc32, AllUpperCase) {
    // Verify uppercase conversion
    uint32_t mixed = MixCrc32("AbCdEf");
    uint32_t upper = MixCrc32("ABCDEF");
    EXPECT_EQ(mixed, upper);
}

TEST(MixCrc32, NullTerminatorNotIncluded) {
    // CRC is computed on the string content, not the null terminator.
    // "AB" -> "AB"+0x02+"A" vs "ABC" -> "ABC"+0x03: different inputs.
    uint32_t ab = MixCrc32("AB");
    uint32_t abc = MixCrc32("ABC");
    EXPECT_NE(ab, abc);
}

TEST(MixCrc32, MaxNameLength) {
    // Names up to 255 chars should work
    char longName[256];
    memset(longName, 'A', 255);
    longName[255] = '\0';
    uint32_t crc = MixCrc32(longName);
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, SingleChar) {
    uint32_t crc = MixCrc32("A");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, TwoChars) {
    uint32_t crc = MixCrc32("AB");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, ThreeChars) {
    uint32_t crc = MixCrc32("ABC");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, FourChars) {
    uint32_t crc = MixCrc32("ABCD");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, DigitOnly) {
    uint32_t crc = MixCrc32("12345");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, SpecialChars) {
    uint32_t crc = MixCrc32("a01_f00e.bik");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, DotInName) {
    uint32_t crc = MixCrc32("test.bik");
    EXPECT_NE(crc, 0u);
}

TEST(MixCrc32, BinkExtension) {
    // All .bik files should produce valid CRCs
    uint32_t crc1 = MixCrc32("westlogo.bik");
    uint32_t crc2 = MixCrc32("a00_f00e.bik");
    uint32_t crc3 = MixCrc32("s01_f00e.bik");
    EXPECT_NE(crc1, 0u);
    EXPECT_NE(crc2, 0u);
    EXPECT_NE(crc3, 0u);
    EXPECT_NE(crc1, crc2);
    EXPECT_NE(crc2, crc3);
}

TEST(MixCrc32, LmdCrcKnownValue) {
    // The LMD file itself hashes to the well-known 0x366E051F (see KnownLmdFilename).
    uint32_t crc1 = MixCrc32("local mix database.dat");
    uint32_t crc2 = MixCrc32("LOCAL MIX DATABASE.DAT");
    EXPECT_EQ(crc1, 0x366E051Fu);
    EXPECT_EQ(crc1, crc2);
}

TEST(MixCrc32, PaddingLengths) {
    // All padding cases land on a multiple of 4:
    // len&3=0: no pad (e.g. "ABCD" -> 4)
    // len&3=1: pad 3 (e.g. "A" -> "A"+0x01+"AA" -> 4)
    // len&3=2: pad 2 (e.g. "AB" -> "AB"+0x02+"A" -> 4)
    // len&3=3: pad 1 (e.g. "ABC" -> "ABC"+0x03 -> 4)
    uint32_t crc4 = MixCrc32("ABCD");  // len=4, pad=0
    uint32_t crc8 = MixCrc32("ABCDEFGH");  // len=8, pad=0
    EXPECT_NE(crc4, crc8);  // Different input, different CRC
}

TEST(MixCrc32, DigitsVsLetters) {
    // Same length, different content → different CRC
    uint32_t crc1 = MixCrc32("AAAA");
    uint32_t crc2 = MixCrc32("BBBB");
    EXPECT_NE(crc1, crc2);
}

TEST(MixCrc32, ReverseOrder) {
    // Reversed string should produce different CRC
    uint32_t fwd = MixCrc32("ABCDEF");
    uint32_t rev = MixCrc32("FEDCBA");
    EXPECT_NE(fwd, rev);
}
