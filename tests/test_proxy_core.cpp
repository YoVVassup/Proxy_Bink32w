#include <gtest/gtest.h>
#include "test_helpers.h"
#include <cstring>

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
    g_vids[0].scale = NULL;
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
    g_vids[0].scale = NULL;
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
    g_vids[0].scale = NULL;
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
    g_vids[0].scale = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].scale = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vids[2].handle = (void*)0x3000;
    g_vids[2].scale = NULL;
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
    g_vids[0].scale = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].scale = NULL;
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
    g_vids[0].scale = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].scale = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vidCount = 2;

    UntrackVideo((void*)0x2000);
    EXPECT_EQ(g_vidCount, 1);
    EXPECT_EQ(g_vids[0].handle, (void*)0x1000);
    g_vidCount = saved;
}

TEST(UntrackVideoTest, UntrackDropsOnlySlotReference) {
    // The slot holds the Create() reference. An external reference (an
    // in-flight sBinkCopyToBuffer) must keep the buffers alive across
    // UntrackVideo — that is exactly what lets a blit outlive compaction.
    int saved = g_vidCount;
    g_vidCount = 0;
    ScaleBufs* sb = ScaleBufsCreate();
    ASSERT_NE(sb, (ScaleBufs*)NULL);
    sb->tempBuf = VirtualAlloc(NULL, 1024, MEM_COMMIT, PAGE_READWRITE);
    sb->lookupX = (int*)malloc(100 * sizeof(int));
    sb->lookupY = (int*)malloc(100 * sizeof(int));
    ASSERT_NE(sb->tempBuf, (void*)NULL);
    ASSERT_NE(sb->lookupX, (int*)NULL);
    ASSERT_NE(sb->lookupY, (int*)NULL);

    g_vids[0].handle = (void*)0x1000;
    g_vids[0].scale = sb;
    g_vids[0].wavPlayer = NULL;
    g_vidCount = 1;

    ScaleBufsRef(sb); // refs = 2: slot + "in-flight copy"
    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);
    EXPECT_EQ(sb->refs, 1) << "UntrackVideo must drop exactly the slot's reference";
    // Still alive: the buffers were not freed underneath the pinned copy.
    EXPECT_NE(sb->tempBuf, (void*)NULL);
    EXPECT_NE(sb->lookupX, (int*)NULL);
    EXPECT_NE(sb->lookupY, (int*)NULL);
    ScaleBufsUnref(sb); // refs = 0 -> everything is released here
    g_vidCount = saved;
}

TEST(UntrackVideoTest, LastUnrefFreesEverything) {
    int saved = g_vidCount;
    g_vidCount = 0;
    ScaleBufs* sb = ScaleBufsCreate();
    ASSERT_NE(sb, (ScaleBufs*)NULL);
    sb->tempBuf = VirtualAlloc(NULL, 1024, MEM_COMMIT, PAGE_READWRITE);
    ASSERT_NE(sb->tempBuf, (void*)NULL);

    g_vids[0].handle = (void*)0x1000;
    g_vids[0].scale = sb;
    g_vids[0].wavPlayer = NULL;
    g_vidCount = 1;

    // Only the slot reference exists: UntrackVideo is the last Unref and
    // must free the buffers and the ScaleBufs itself (crash/leak check).
    UntrackVideo((void*)0x1000);
    EXPECT_EQ(g_vidCount, 0);
    g_vidCount = saved;
}

TEST(ScaleBufsTest, NullSafe) {
    ScaleBufsRef(NULL);
    ScaleBufsUnref(NULL); // must not crash
    SUCCEED();
}

TEST(ScaleBufsTest, RefUnrefReachesZero) {
    ScaleBufs* sb = ScaleBufsCreate();
    ASSERT_NE(sb, (ScaleBufs*)NULL);
    EXPECT_EQ(sb->refs, 1);
    ScaleBufsRef(sb);
    EXPECT_EQ(sb->refs, 2);
    ScaleBufsUnref(sb);
    EXPECT_EQ(sb->refs, 1);
    ScaleBufsUnref(sb); // frees sb — must not crash or double-free
    SUCCEED();
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
    g_vids[0].scale = NULL;
    g_vids[0].wavPlayer = NULL;
    g_vids[1].handle = (void*)0x2000;
    g_vids[1].scale = NULL;
    g_vids[1].wavPlayer = NULL;
    g_vidCount = 2;

    UntrackVideo((void*)0x1000);
    EXPECT_EQ(FindVideo((void*)0x1000), (VideoInfo*)NULL);
    VideoInfo* vi = FindVideo((void*)0x2000);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->handle, (void*)0x2000);
    g_vidCount = saved;
}

// ============================================================================
// TrackVideo tests (Agent.md 16.4): BINKSUMMARY dimensions are the one input
// the proxy never validates - everything downstream (scaling buffer size,
// pitch math) assumes they are bounded. TrackVideo used to accept any nonzero
// pair, so a lying/faulting callee could push 0xFFFFFFFF into g_vids[].
// ============================================================================

extern void* pBinkGetSummary;   // non-static under BINK_TEST_BUILD

static void __stdcall FakeSummarySane(void* h, void* out) {
    (void)h;
    memset(out, 0, 124);
    *(uint32_t*)((char*)out + 0) = 640;
    *(uint32_t*)((char*)out + 4) = 480;
    *(uint32_t*)((char*)out + 20) = 15;
    *(uint32_t*)((char*)out + 24) = 1;
}

static void __stdcall FakeSummaryHuge(void* h, void* out) {
    (void)h;
    memset(out, 0, 124);
    *(uint32_t*)((char*)out + 0) = 0xFFFFFFFFu;
    *(uint32_t*)((char*)out + 4) = 0xFFFFFFFFu;
}

class TrackVideoSummaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_ = pBinkGetSummary;
        g_vidCount = 0;
    }
    void TearDown() override {
        while (g_vidCount > 0) UntrackVideo(g_vids[g_vidCount - 1].handle);
        pBinkGetSummary = saved_;
    }
    void* saved_ = NULL;
};

TEST_F(TrackVideoSummaryTest, RejectsImplausibleDimensions) {
    pBinkGetSummary = &FakeSummaryHuge;
    void* h = (void*)0x4400;

    TrackVideo(h, "huge.bik", "test.mix");

    EXPECT_EQ(g_vidCount, 0) << "garbage summary must not create a slot";
    EXPECT_EQ(FindVideo(h), (VideoInfo*)NULL);
}

TEST_F(TrackVideoSummaryTest, TracksSaneDimensions) {
    pBinkGetSummary = &FakeSummarySane;
    void* h = (void*)0x4401;

    TrackVideo(h, "sane.bik", "test.mix");

    VideoInfo* vi = FindVideo(h);
    ASSERT_NE(vi, (VideoInfo*)NULL);
    EXPECT_EQ(vi->width, 640u);
    EXPECT_EQ(vi->height, 480u);
}
