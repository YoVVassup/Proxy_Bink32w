#include <gtest/gtest.h>
#include "test_helpers.h"

// ============================================================================
// test_proxy_core.cpp — Tests for FindVideo and UntrackVideo
//
// TrackVideo is tested indirectly through integration tests (requires real Bink DLL).
// FindVideo and UntrackVideo are tested by directly manipulating g_vids[].
// ============================================================================

// ============================================================================
// FindVideo tests
// ============================================================================

TEST(FindVideoTest, EmptyArrayReturnsNull) {
    int saved = g_vidCount;
    g_vidCount = 0;
    EXPECT_EQ(FindVideo((void*)0x1000), (VideoInfo*)NULL);
    g_vidCount = saved;
}

TEST(FindVideoTest, ReturnsTrackedEntry) {
    int saved = g_vidCount;
    g_vidCount = 0;
    void* h = (void*)0x1000;
    g_vids[0].handle = h;
    g_vids[0].width = 640;
    g_vids[0].height = 480;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vidCount = 1;

    VideoInfo* vi = FindVideo(h);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->handle, h);
    EXPECT_EQ(vi->width, 640u);
    EXPECT_EQ(vi->height, 480u);
    g_vidCount = saved;
}

TEST(FindVideoTest, ReturnsNullForUnknownHandle) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vidCount = 1;

    EXPECT_EQ(FindVideo((void*)0x9999), (VideoInfo*)NULL);
    g_vidCount = saved;
}

TEST(FindVideoTest, MultipleHandles) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[1].handle = (void*)0x2000;
    g_vids[2].handle = (void*)0x3000;
    g_vidCount = 3;

    EXPECT_NE(FindVideo((void*)0x1000), (VideoInfo*)NULL);
    EXPECT_NE(FindVideo((void*)0x2000), (VideoInfo*)NULL);
    EXPECT_NE(FindVideo((void*)0x3000), (VideoInfo*)NULL);
    EXPECT_EQ(FindVideo((void*)0x4000), (VideoInfo*)NULL);
    g_vidCount = saved;
}

// ============================================================================
// UntrackVideo tests
// ============================================================================

TEST(UntrackVideoTest, RemovesEntry) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vidCount = 1;

    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, UnknownHandleUnchanged) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vidCount = 1;

    UntrackVideo((void*)0x9999);
    EXPECT_EQ(g_vidCount, 1);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, RemovesMiddleEntry) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].tempBuf = NULL;
    g_vids[1].scaleLookupX = NULL;
    g_vids[1].scaleLookupY = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vids[2].handle = (void*)0x3000;
    g_vids[2].tempBuf = NULL;
    g_vids[2].scaleLookupX = NULL;
    g_vids[2].scaleLookupY = NULL;
    g_vids[2].wavPlayer = NULL;
    g_vidCount = 3;

    UntrackVideo((void*)0x2000);
    EXPECT_EQ(g_vidCount, 2);
    EXPECT_EQ(g_vids[0].handle, (void*)0x1000);
    EXPECT_EQ(g_vids[1].handle, (void*)0x3000);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, RemovesFirstEntry) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].tempBuf = NULL;
    g_vids[1].scaleLookupX = NULL;
    g_vids[1].scaleLookupY = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vidCount = 2;

    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 1);
    EXPECT_EQ(g_vids[0].handle, (void*)0x2000);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, RemovesLastEntry) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].tempBuf = NULL;
    g_vids[1].scaleLookupX = NULL;
    g_vids[1].scaleLookupY = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vidCount = 2;

    UntrackVideo((void*)0x2000);
    EXPECT_EQ(g_vidCount, 1);
    EXPECT_EQ(g_vids[0].handle, (void*)0x1000);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, FreesTempBuf) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = VirtualAlloc(NULL, 1024, MEM_COMMIT, PAGE_READWRITE);
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vidCount = 1;

    ASSERT_NE(g_vids[0].tempBuf, (void*)NULL);
    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, FreesScaleTables) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = (int*)malloc(100 * sizeof(int));
    g_vids[0].scaleLookupY = (int*)malloc(100 * sizeof(int));
    g_vids[0].wavPlayer = NULL;
    g_vidCount = 1;

    ASSERT_NE(g_vids[0].scaleLookupX, (int*)NULL);
    ASSERT_NE(g_vids[0].scaleLookupY, (int*)NULL);
    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, EmptyArray) {
    int saved = g_vidCount;
    g_vidCount = 0;
    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, AfterUntrackFindVideoReturnsNull) {
    int saved = g_vidCount;
    g_vidCount = 0;
    g_vids[0].handle = (void*)0x1000;
    g_vids[0].tempBuf = NULL;
    g_vids[0].scaleLookupX = NULL;
    g_vids[0].scaleLookupY = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].tempBuf = NULL;
    g_vids[1].scaleLookupX = NULL;
    g_vids[1].scaleLookupY = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vidCount = 2;

    UntrackVideo((void*)0x1000);
    EXPECT_EQ(FindVideo((void*)0x1000), (VideoInfo*)NULL);
    VideoInfo* vi = FindVideo((void*)0x2000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->handle, (void*)0x2000);
    g_vidCount = saved;
}
