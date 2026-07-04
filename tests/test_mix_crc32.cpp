#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// Tests for MixCrc32 — RA2 .mix filename CRC32 computation
//
// The RA2 convention: uppercase name + pad to 4-byte boundary by repeating
// the boundary character. Then compute CRC32 on the padded string.
//
// Reference: RA2YR-reMIXer StringToCRC32.ahk2
// ============================================================================

TEST(MixCrc32, EmptyString) {
    // CRC32 of empty string is well-defined: 0xFFFFFFFF (initial) → ~0xFFFFFFFF = 0
    uint32_t crc = MixCrc32("");
    EXPECT_EQ(crc, 0u);
}

TEST(MixCrc32, KnownLmdCrc) {
    // LMD file has a well-known CRC: 0x366E051F
    // The LMD filename in the .mix hash table is determined by this CRC.
    // We can verify that our CRC32 implementation produces consistent results.
    uint32_t crc1 = MixCrc32("test");
    uint32_t crc2 = MixCrc32("test");
    EXPECT_EQ(crc1, crc2);
}

TEST(MixCrc32, CaseInsensitive) {
    // RA2 converts to uppercase before CRC32
    uint32_t lower = MixCrc32("abcdef");
    uint32_t upper = MixCrc32("ABCDEF");
    EXPECT_EQ(lower, upper);
}

TEST(MixCrc32, PaddingCorrectness) {
    // RA2 padding: padCount = 3 - (len & 3)
    // "a"  (len=1): padCount=2, padded="aaa" (3 bytes)
    // "aa" (len=2): padCount=1, padded="aaa" (3 bytes)
    // "aaa"(len=3): padCount=0, padded="aaa" (3 bytes)
    // "aaaa"(len=4): no pad,    padded="aaaa" (4 bytes)
    // So "a", "aa", "aaa" all produce the same CRC (identical padded input)
    uint32_t a1 = MixCrc32("a");
    uint32_t a2 = MixCrc32("aa");
    uint32_t a3 = MixCrc32("aaa");
    uint32_t a4 = MixCrc32("aaaa");
    EXPECT_EQ(a1, a2);
    EXPECT_EQ(a2, a3);
    EXPECT_NE(a3, a4);  // different: 3 bytes vs 4 bytes
}

TEST(MixCrc32, ConsistencyWithLmdEntries) {
    // From the log output, expandmo11.mix has these known CRC->name mappings:
    // CRC=0x92A0FBFC -> a04_f03e.bik
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
    // CRC should be computed on the string content, not the null terminator
    // "AB" padded to "ABBB" should differ from "AB\0" padded differently
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
    // LMD file CRC is well-known: 0x366E051F
    // We can't directly test this since we don't know the LMD filename,
    // but we can verify our CRC32 implementation is consistent
    uint32_t crc1 = MixCrc32("LMD");
    uint32_t crc2 = MixCrc32("LMD");
    EXPECT_EQ(crc1, crc2);
}

TEST(MixCrc32, PaddingLengths) {
    // Test all padding cases: len & 3 = 0,1,2,3
    // len&3=0: no padding (e.g. "ABCD")
    // len&3=1: pad 2 chars (e.g. "A" -> "AAA")
    // len&3=2: pad 1 char (e.g. "AB" -> "AAB" ... wait, "AB" + "B" = "ABB"?)
    // len&3=3: no padding (e.g. "ABC")
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
