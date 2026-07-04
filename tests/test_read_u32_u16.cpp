#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// Tests for ReadU32/ReadU16 — Safe memory read helpers via memcpy
//
// These functions avoid alignment issues and strict aliasing violations
// by using memcpy instead of pointer casts.
// ============================================================================

TEST(ReadU32, BasicValues) {
    uint8_t buf[4] = {0x01, 0x02, 0x03, 0x04};
    EXPECT_EQ(ReadU32(buf), 0x04030201u);  // little-endian
}

TEST(ReadU32, AllZeros) {
    uint8_t buf[4] = {0, 0, 0, 0};
    EXPECT_EQ(ReadU32(buf), 0u);
}

TEST(ReadU32, AllOnes) {
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(ReadU32(buf), 0xFFFFFFFF);
}

TEST(ReadU32, KnownBinkMarker) {
    // Bink marker 'fKIB' = 0x42494B66 in LE: 66 4B 49 42
    uint8_t buf[4] = {0x66, 0x4B, 0x49, 0x42};
    EXPECT_EQ(ReadU32(buf), 0x42494B66u);
}

TEST(ReadU32, LmdCrc) {
    // Known LMD CRC: 0x366E051F
    uint8_t buf[4] = {0x1F, 0x05, 0x6E, 0x36};
    EXPECT_EQ(ReadU32(buf), 0x366E051Fu);
}

TEST(ReadU16, BasicValues) {
    uint8_t buf[2] = {0x01, 0x02};
    EXPECT_EQ(ReadU16(buf), 0x0201u);  // little-endian
}

TEST(ReadU16, AllZeros) {
    uint8_t buf[2] = {0, 0};
    EXPECT_EQ(ReadU16(buf), 0u);
}

TEST(ReadU16, AllOnes) {
    uint8_t buf[2] = {0xFF, 0xFF};
    EXPECT_EQ(ReadU16(buf), 0xFFFF);
}

TEST(ReadU16, MixFileCount) {
    // From log: expandmo11.mix has fileCount=18 = 0x0012
    uint8_t buf[2] = {0x12, 0x00};
    EXPECT_EQ(ReadU16(buf), 18u);
}

TEST(ReadU32, UnalignedRead) {
    // Test reading from an offset within a larger buffer (unaligned)
    uint8_t buf[16] = {0};
    buf[3] = 0xAA;
    buf[4] = 0xBB;
    buf[5] = 0xCC;
    buf[6] = 0xDD;
    EXPECT_EQ(ReadU32(buf + 3), 0xDDCCBBAAu);
}

TEST(ReadU16, UnalignedRead) {
    uint8_t buf[8] = {0};
    buf[1] = 0x34;
    buf[2] = 0x12;
    EXPECT_EQ(ReadU16(buf + 1), 0x1234u);
}
