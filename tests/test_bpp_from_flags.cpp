#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// Tests for BppFromFlags — Extracts bits-per-pixel from BinkCopyToBuffer flags
//
// The Bink API encodes surface type in the lower 3 bits of flags:
//   st = flags & 7
//   st == 0       -> 3 bpp (RGB888, 24-bit)
//   st == 1..4    -> 2 bpp (RGB565, 16-bit) — used by RA2/RA2YR
//   st == 5..7    -> 4 bpp (RGB888+alpha, 32-bit)
// ============================================================================

TEST(BppFromFlags, Rgb565_Bpp2) {
    // RA2/RA2YR typical: flags & 7 == 2 → bpp=2
    EXPECT_EQ(BppFromFlags(0x02), 2);
    EXPECT_EQ(BppFromFlags(0x0A), 2);  // flags = 0x0A = bpp=2
    EXPECT_EQ(BppFromFlags(0x12), 2);
}

TEST(BppFromFlags, SurfaceType0_Bpp3) {
    // Surface type 0 → 3 bpp
    EXPECT_EQ(BppFromFlags(0x00), 3);
    EXPECT_EQ(BppFromFlags(0x08), 3);
}

TEST(BppFromFlags, SurfaceType1to4_Bpp2) {
    // Surface types 1-4 → 2 bpp
    EXPECT_EQ(BppFromFlags(0x01), 2);
    EXPECT_EQ(BppFromFlags(0x02), 2);
    EXPECT_EQ(BppFromFlags(0x03), 2);
    EXPECT_EQ(BppFromFlags(0x04), 2);
}

TEST(BppFromFlags, SurfaceType5to7_Bpp4) {
    // Surface types 5-7 → 4 bpp
    EXPECT_EQ(BppFromFlags(0x05), 4);
    EXPECT_EQ(BppFromFlags(0x06), 4);
    EXPECT_EQ(BppFromFlags(0x07), 4);
}

TEST(BppFromFlags, HighBitsIgnored) {
    // Only lower 3 bits matter
    EXPECT_EQ(BppFromFlags(0xF2), 2);  // 0xF2 & 7 = 2
    EXPECT_EQ(BppFromFlags(0xFF), 4);  // 0xFF & 7 = 7
    EXPECT_EQ(BppFromFlags(0x80), 3);  // 0x80 & 7 = 0
}

TEST(BppFromFlags, NegativeFlags) {
    // Negative flags (signed int) — lower 3 bits still extracted
    int neg = -1;  // all bits set
    EXPECT_EQ(BppFromFlags(neg), 4);  // -1 & 7 = 7
}
