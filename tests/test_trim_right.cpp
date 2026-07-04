#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// Tests for TrimRight — Removes trailing whitespace from a string
//
// Handles: space, tab, \r, \n
// ============================================================================

TEST(TrimRight, NoTrailingWhitespace) {
    char s[] = "hello";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, TrailingSpaces) {
    char s[] = "hello   ";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, TrailingTabs) {
    char s[] = "hello\t\t";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, TrailingCrLf) {
    char s[] = "hello\r\n";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, TrailingNewline) {
    char s[] = "hello\n";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, TrailingCarriageReturn) {
    char s[] = "hello\r";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, MixedTrailingWhitespace) {
    char s[] = "hello \t\r\n";
    TrimRight(s);
    EXPECT_STREQ(s, "hello");
}

TEST(TrimRight, OnlyWhitespace) {
    char s[] = "   ";
    TrimRight(s);
    EXPECT_STREQ(s, "");
}

TEST(TrimRight, EmptyString) {
    char s[] = "";
    TrimRight(s);
    EXPECT_STREQ(s, "");
}

TEST(TrimRight, WhitespaceInMiddle) {
    char s[] = "hel lo";
    TrimRight(s);
    EXPECT_STREQ(s, "hel lo");
}

TEST(TrimRight, ConfigIniLine) {
    // Typical config line with trailing whitespace
    char s[] = "enabled = false\r\n";
    TrimRight(s);
    EXPECT_STREQ(s, "enabled = false");
}

TEST(TrimRight, SectionHeader) {
    char s[] = "[audio]\r\n";
    TrimRight(s);
    EXPECT_STREQ(s, "[audio]");
}
