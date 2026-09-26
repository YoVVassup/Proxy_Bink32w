#include <gtest/gtest.h>
#include "test_helpers.h"
#include "mix_crypto.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================================
// test_mix_blowfish.cpp — Blowfish engine, Westwood key derivation,
//                         encrypted ParseMixFile integration
// ============================================================================

extern BOOL g_logEnabled;

// key_source (80B) from real RA2 language.mix (flags=0x0003)
static const uint8_t kLangKeySource[80] = {
    0xFC,0x32,0xF2,0x6B,0x9A,0x13,0x43,0x43,0x75,0x96,0x41,0x50,0x49,0xDD,0xD9,0x3F,
    0x7B,0x0F,0xFE,0x67,0xD8,0xBA,0x79,0xF8,0x7C,0xC7,0xD7,0xDA,0xA1,0xAC,0x1A,0x79,
    0xB8,0xA8,0xA7,0x87,0x18,0x52,0xCB,0x46,0xA4,0xC1,0xD8,0x0F,0x93,0x1F,0xE7,0xFA,
    0xDD,0x8B,0xB2,0xC6,0x2A,0xDF,0xBC,0x52,0x68,0xFB,0xF8,0x8B,0xA0,0x3C,0x17,0xB1,
    0xC5,0x83,0x0F,0xA2,0xF1,0x31,0xAC,0x45,0x49,0x2C,0xD9,0x67,0x76,0xBD,0x4F,0x12
};

class MixCryptoTest : public ::testing::Test {
protected:
    BOOL savedLog;
    void SetUp() override {
        savedLog = g_logEnabled;
        g_mixCacheCount = 0;
        g_logEnabled = FALSE;
    }
    void TearDown() override {
        g_logEnabled = savedLog;
        g_mixCacheCount = 0;
    }
};

// ----------------------------------------------------------------------------
// Blowfish known-answer tests (Schneier / standard vectors)
// ----------------------------------------------------------------------------

TEST_F(MixCryptoTest, BlowfishZeroKeyZeroPlain) {
    // key = 00..00 (8 bytes used in classic vector as 1..8 zero bytes — use 1 byte? )
    // Classic: key=0x0000000000000000, plain=0x0000000000000000 -> 4EF997456198DD78
    // Our SetKey cycles key bytes; zero key of length 8.
    uint8_t key[8] = {0};
    MixBlowfishCtx ctx;
    MixBlowfishInit(&ctx, key, 8);

    // Internal P after schedule depends on key; standard test vector uses
    // little-endian word order differently. Use roundtrip + non-zero diffusion
    // plus a published vector with explicit byte order.
    uint8_t block[8] = {0};
    MixBlowfishEncipherBlock(&ctx, block);
    // Not all-zero after encrypt
    bool allZero = true;
    for (int i = 0; i < 8; i++) if (block[i]) allZero = false;
    EXPECT_FALSE(allZero);

    MixBlowfishDecipherBlock(&ctx, block);
    for (int i = 0; i < 8; i++) EXPECT_EQ(block[i], 0);
}

TEST_F(MixCryptoTest, BlowfishRoundtripKnownKey) {
    uint8_t key[56];
    for (int i = 0; i < 56; i++) key[i] = (uint8_t)(i * 7 + 3);

    MixBlowfishCtx ctx;
    MixBlowfishInit(&ctx, key, 56);

    uint8_t plain[32];
    for (int i = 0; i < 32; i++) plain[i] = (uint8_t)(0xA5 ^ i);

    uint8_t work[32];
    memcpy(work, plain, 32);
    MixBlowfishEncipher(&ctx, work, 32);
    EXPECT_NE(memcmp(work, plain, 32), 0);

    MixBlowfishDecipher(&ctx, work, 32);
    EXPECT_EQ(memcmp(work, plain, 32), 0);
}

TEST_F(MixCryptoTest, BlowfishSchneierVector) {
    // Schneier test: key ASCII "abcdefghijklmnopqrstuvwxyz" (26 bytes),
    // plain 0x0123456789ABCDEF in big-endian dword words as reverse32 input.
    // Published cipher for that exact setup with reverse32 is engine-specific;
    // verify Encipher/Decipher inverses and block-swap property instead:
    // Encipher then Decipher == identity for every pattern.
    uint8_t key[26];
    for (int i = 0; i < 26; i++) key[i] = (uint8_t)('a' + i);

    MixBlowfishCtx ctx;
    MixBlowfishInit(&ctx, key, 26);

    uint8_t block[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    uint8_t orig[8];
    memcpy(orig, block, 8);

    MixBlowfishEncipherBlock(&ctx, block);
    MixBlowfishDecipherBlock(&ctx, block);
    EXPECT_EQ(memcmp(block, orig, 8), 0);
}

TEST_F(MixCryptoTest, BlowfishDeterministic) {
    uint8_t key[16];
    memset(key, 0x42, 16);
    MixBlowfishCtx a, b;
    MixBlowfishInit(&a, key, 16);
    MixBlowfishInit(&b, key, 16);
    EXPECT_EQ(memcmp(&a, &b, sizeof(a)), 0);

    uint8_t ba[8] = {1,2,3,4,5,6,7,8};
    uint8_t bb[8] = {1,2,3,4,5,6,7,8};
    MixBlowfishEncipherBlock(&a, ba);
    MixBlowfishEncipherBlock(&b, bb);
    EXPECT_EQ(memcmp(ba, bb, 8), 0);
}

static int HexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void HexBytes(const char* hex, uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; i++)
        out[i] = (uint8_t)((HexNib(hex[2 * i]) << 4) | HexNib(hex[2 * i + 1]));
}

// Known-answer vectors against the published Blowfish test suite (first two
// are the classic Schneier vectors). Generated with PyCryptodome ECB
// (C:\Temp\opencode\bf_vectors.py) and cross-checked against a transcription
// of mix_crypto.cpp + the shipped S-box files (bf_verify.py prints
// ALL MATCH) — so these pin the engine to the standard byte order, not just
// to itself: every roundtrip test above still passes with a wrong S-box or
// word order.
TEST_F(MixCryptoTest, BlowfishKnownAnswerVectors) {
    auto check = [](const char* what, const uint8_t* key, int keyLen,
                    const uint8_t* plain, int len, const char* cipherHex) {
        uint8_t expect[64];
        HexBytes(cipherHex, expect, (size_t)len);
        std::vector<uint8_t> work(plain, plain + len);
        MixBlowfishCtx ctx;
        MixBlowfishInit(&ctx, key, keyLen);
        MixBlowfishEncipher(&ctx, work.data(), len);
        EXPECT_EQ(memcmp(work.data(), expect, (size_t)len), 0) << what;
    };

    static const uint8_t kZero8[8] = {0};
    check("zero key / zero plain", kZero8, 8, kZero8, 8, "4EF997456198DD78");

    static const uint8_t kFf8[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    check("ff key / ff plain", kFf8, 8, kFf8, 8, "51866FD5B85ECB8A");

    static const uint8_t k30Key[8] = {0x30,0,0,0,0,0,0,0};
    static const uint8_t k10Plain[8] = {0x10,0,0,0,0,0,0,1};
    check("key 30.. / plain 10..01", k30Key, 8, k10Plain, 8, "7D856F9A613063F2");

    uint8_t abcKey[26];
    for (int i = 0; i < 26; i++) abcKey[i] = (uint8_t)('a' + i);
    static const uint8_t kAbcPlain[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    check("a-z key / 0123456789ABCDEF", abcKey, 26, kAbcPlain, 8, "FEE93BE40A60AF5C");

    uint8_t key56[56];
    for (int i = 0; i < 56; i++) key56[i] = (uint8_t)i;
    uint8_t plain16[16];
    for (int i = 0; i < 16; i++) plain16[i] = (uint8_t)(0xA5 ^ i);
    check("key=0..55 / plain=A5^i x16", key56, 56, plain16, 16,
          "FFF37EB7E81B547EF94DC95AD00C4FAA");

    uint8_t keyI7[56];
    for (int i = 0; i < 56; i++) keyI7[i] = (uint8_t)((i * 7 + 3) & 0xFF);
    uint8_t plain32[32];
    for (int i = 0; i < 32; i++) plain32[i] = (uint8_t)(0xA5 ^ i);
    check("key=(7i+3)&0xFF / plain=A5^i x32", keyI7, 56, plain32, 32,
          "84F57AA224CD2A4D33A7F4089ADF07FAFA9F5D9E6F73CCA60AF1BAE81A5EBF8F");

    uint8_t key16[16];
    for (int i = 0; i < 16; i++) key16[i] = (uint8_t)i;
    static const uint8_t kSeqPlain[8] = {0xAB,0xCD,0xEF,0x01,0x23,0x45,0x67,0x89};
    check("key=0..15 / plain=ABCDEF0123456789", key16, 16, kSeqPlain, 8,
          "4D84465B232081A6");

    static const uint8_t kWestKey[9] = {0x57,0x65,0x73,0x74,0x77,0x6F,0x6F,0x64,0x31};
    static const uint8_t kBikPlain[8] = {0x42,0x49,0x4B,0x44,0x41,0x54,0x41,0x21};
    check("Westwood1 / BIKDATA!", kWestKey, 9, kBikPlain, 8, "4D384104E03CE47E");
}

// ----------------------------------------------------------------------------
// ComputeBlowfishKey
// ----------------------------------------------------------------------------

TEST_F(MixCryptoTest, ComputeKeyDeterministicAndNonZero) {
    uint8_t k1[56], k2[56];
    MixComputeBlowfishKey(kLangKeySource, sizeof(kLangKeySource), k1);
    MixComputeBlowfishKey(kLangKeySource, sizeof(kLangKeySource), k2);
    EXPECT_EQ(memcmp(k1, k2, 56), 0);

    bool allZero = true;
    for (int i = 0; i < 56; i++) if (k1[i]) allZero = false;
    EXPECT_FALSE(allZero) << "derived key is all zeros";
}

TEST_F(MixCryptoTest, ComputeKeySensitiveToKeySource) {
    uint8_t ks2[80];
    memcpy(ks2, kLangKeySource, 80);
    ks2[10] ^= 0xFF;

    uint8_t k1[56], k2[56];
    MixComputeBlowfishKey(kLangKeySource, sizeof(kLangKeySource), k1);
    MixComputeBlowfishKey(ks2, sizeof(ks2), k2);
    EXPECT_NE(memcmp(k1, k2, 56), 0);
}

// Known-answer test for the Westwood key derivation on the real 80-byte
// key_source. Without it, ComputeKeyDeterministicAndNonZero only proves the
// routine is self-consistent: a botched init_pubkey()/calc_a_key() would
// still be deterministic, still be non-zero, and still decrypt nothing. This
// vector pins the actual output.
TEST_F(MixCryptoTest, ComputeKeyKnownAnswerLanguageKeySource) {
    static const uint8_t kExpected[56] = {
        0x0F,0x5C,0x95,0x30,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
        0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
        0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xDD,0x1D,0xE7,0xCB,
        0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,
        0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,
    };

    uint8_t k[56];
    MixComputeBlowfishKey(kLangKeySource, sizeof(kLangKeySource), k);
    EXPECT_EQ(memcmp(k, kExpected, 56), 0)
        << "derived key changed — real encrypted MIX archives will stop parsing";

    // Must hold on a repeat call too, after other tests have scribbled all
    // over the shared bignum scratch space.
    uint8_t k2[56];
    MixComputeBlowfishKey(kLangKeySource, sizeof(kLangKeySource), k2);
    EXPECT_EQ(memcmp(k2, kExpected, 56), 0);
}

TEST_F(MixCryptoTest, DerivedKeyDecryptsRealLanguageMixHeader) {
    // Real encrypted language.mix must yield a sane fileCount (1..256)
    const char* candidates[] = {
        "K:\\Git\\Opencode\\MO-Installer\\Original_Files\\language.mix",
        "K:\\MOVision[1.3]\\language.mix",
        "K:\\MO Vision [ModManager Dirty Edition]\\Mods\\MO-RA2Depends\\Files\\language.mix",
    };
    const char* path = NULL;
    for (const char* c : candidates) {
        if (GetFileAttributesA(c) != INVALID_FILE_ATTRIBUTES) { path = c; break; }
    }
    if (!path) GTEST_SKIP() << "language.mix not found on this machine";

    MixArchive* mix = ParseMixFile(path);
    ASSERT_NE(mix, (MixArchive*)NULL) << "ParseMixFile failed for " << path;
    EXPECT_TRUE(mix->valid);
    EXPECT_EQ(mix->encrypted, 1);
    EXPECT_GT(mix->fileCount, 0);
    EXPECT_LE(mix->fileCount, 65535u);
    EXPECT_GT(mix->bodyOffset, 84u);

    WIN32_FILE_ATTRIBUTE_DATA fad;
    ASSERT_TRUE(GetFileAttributesExA(path, GetFileExInfoStandard, &fad));
    uint64_t fileSize = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;

    // fileCount/bodyOffset alone are near-vacuous: a wrong key still yields a
    // random count that lands inside 1..65535 with odds ~1:1. The index is
    // what must actually hold together — no entry past EOF, no two entries
    // overlapping, and together they must account for (almost) the whole body.
    uint64_t covered = 0;
    for (uint16_t i = 0; i < mix->fileCount; i++) {
        const MixEntry& e = mix->entries[i];
        if (e.size == 0) continue;
        uint64_t start = (uint64_t)mix->bodyOffset + e.offset;
        uint64_t end = start + e.size;
        EXPECT_LE(end, fileSize) << "entry " << i << " ends past EOF";
        covered += e.size;

        for (uint16_t j = (uint16_t)(i + 1); j < mix->fileCount; j++) {
            const MixEntry& o = mix->entries[j];
            if (o.size == 0) continue;
            uint64_t oStart = (uint64_t)mix->bodyOffset + o.offset;
            uint64_t oEnd = oStart + o.size;
            EXPECT_FALSE((start < oEnd) && (oStart < end))
                << "entries " << i << " and " << j << " overlap";
        }
    }
    ASSERT_GT(covered, 0u) << "every entry was dropped: decryption produced garbage";

    uint64_t body = fileSize - mix->bodyOffset;
    EXPECT_GE(covered, body - body / 16)
        << "entries cover " << covered << " of " << body
        << " body bytes — index does not match the file layout";
}

// ----------------------------------------------------------------------------
// Synthetic encrypted MIX — full pipeline without external assets
// ----------------------------------------------------------------------------

static void WriteEncryptedMix(const char* path,
                              const uint8_t* key_source,
                              const std::vector<uint8_t>& body,
                              uint16_t count,
                              const std::vector<std::pair<uint32_t,uint32_t>>& offsSizes,
                              const std::vector<uint32_t>& crcs) {
    // Build plaintext header: count(2)+body_size(4)+index(count*12)
    uint32_t bodySize = (uint32_t)body.size();
    uint32_t totalEnc = 6 + count * 12;
    uint32_t padded = (totalEnc + 7u) & ~7u;
    std::vector<uint8_t> plain(padded, 0);
    plain[0] = (uint8_t)(count & 0xFF);
    plain[1] = (uint8_t)((count >> 8) & 0xFF);
    plain[2] = (uint8_t)(bodySize & 0xFF);
    plain[3] = (uint8_t)((bodySize >> 8) & 0xFF);
    plain[4] = (uint8_t)((bodySize >> 16) & 0xFF);
    plain[5] = (uint8_t)((bodySize >> 24) & 0xFF);
    for (uint16_t i = 0; i < count; i++) {
        size_t o = 6 + i * 12;
        uint32_t crc = crcs[i];
        uint32_t off = offsSizes[i].first;
        uint32_t sz = offsSizes[i].second;
        plain[o+0] = (uint8_t)(crc); plain[o+1] = (uint8_t)(crc>>8);
        plain[o+2] = (uint8_t)(crc>>16); plain[o+3] = (uint8_t)(crc>>24);
        plain[o+4] = (uint8_t)(off); plain[o+5] = (uint8_t)(off>>8);
        plain[o+6] = (uint8_t)(off>>16); plain[o+7] = (uint8_t)(off>>24);
        plain[o+8] = (uint8_t)(sz); plain[o+9] = (uint8_t)(sz>>8);
        plain[o+10] = (uint8_t)(sz>>16); plain[o+11] = (uint8_t)(sz>>24);
    }

    uint8_t bf_key[56];
    MixComputeBlowfishKey(key_source, 80, bf_key);
    MixBlowfishCtx ctx;
    MixBlowfishInit(&ctx, bf_key, 56);
    MixBlowfishEncipher(&ctx, plain.data(), (int)plain.size());

    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    uint8_t sig[4] = {0, 0, 2, 0}; // first=0, flags=2
    fwrite(sig, 1, 4, f);
    fwrite(key_source, 1, 80, f);
    fwrite(plain.data(), 1, plain.size(), f);
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
}

TEST_F(MixCryptoTest, SyntheticEncryptedMixParses) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_synth.mix", TEST_DATA_DIR);

    // 3 files in body
    std::vector<uint8_t> body;
    // file0: 16 bytes "HELLO_BIK_DATA!!"
    const char* f0 = "HELLO_BIK_DATA!!";
    uint32_t off0 = 0, sz0 = 16;
    body.insert(body.end(), f0, f0 + 16);
    // file1: 8 bytes
    uint32_t off1 = 16, sz1 = 8;
    for (int i = 0; i < 8; i++) body.push_back((uint8_t)(0x10 + i));
    // file2: 4 bytes
    uint32_t off2 = 24, sz2 = 4;
    body.push_back('A'); body.push_back('B'); body.push_back('C'); body.push_back('D');

    uint16_t count = 3;
    std::vector<uint32_t> crcs = { 0x11111111, 0x22222222, 0x33333333 };
    std::vector<std::pair<uint32_t,uint32_t>> os = { {off0,sz0}, {off1,sz1}, {off2,sz2} };

    WriteEncryptedMix(path, kLangKeySource, body, count, os, crcs);

    MixArchive* mix = ParseMixFile(path);
    ASSERT_NE(mix, (MixArchive*)NULL);
    EXPECT_TRUE(mix->valid);
    EXPECT_EQ(mix->encrypted, 1);
    EXPECT_EQ(mix->fileCount, 3);
    EXPECT_EQ(mix->entries[0].crc, 0x11111111u);
    EXPECT_EQ(mix->entries[0].offset, 0u);
    EXPECT_EQ(mix->entries[0].size, 16u);
    EXPECT_EQ(mix->entries[1].crc, 0x22222222u);
    EXPECT_EQ(mix->entries[2].size, 4u);

    // bodyOffset = 4 + 80 + pad(6+36=42 -> 48) = 132
    EXPECT_EQ(mix->bodyOffset, 4u + 80u + 48u);

    DeleteFileA(path);
}

TEST_F(MixCryptoTest, SyntheticEncryptedFindBikName) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_find.mix", TEST_DATA_DIR);

    const char* name = "S03_F00E.BIK";

    // entry0 = bik payload, entry1 = LMD (52-byte header + names)
    std::vector<uint8_t> lmd(52, 0);
    lmd.insert(lmd.end(), name, name + strlen(name) + 1);

    std::vector<uint8_t> body;
    const char* bik = "BIKDATA!";
    uint32_t bikOff = 0, bikSz = 8;
    body.insert(body.end(), bik, bik + 8);
    uint32_t lmdOff = 8, lmdSz = (uint32_t)lmd.size();
    body.insert(body.end(), lmd.begin(), lmd.end());

    uint16_t count = 2;
    std::vector<uint32_t> crcs = { MixCrc32(name), 0x366E051F };
    std::vector<std::pair<uint32_t,uint32_t>> os = { {bikOff,bikSz}, {lmdOff,lmdSz} };

    WriteEncryptedMix(path, kLangKeySource, body, count, os, crcs);

    MixArchive* mix = ParseMixFile(path);
    ASSERT_NE(mix, (MixArchive*)NULL);
    EXPECT_EQ(mix->encrypted, 1);
    EXPECT_EQ(mix->fileCount, 2);
    EXPECT_EQ(mix->entries[1].crc, 0x366E051Fu);
    EXPECT_STREQ(mix->entries[0].name, name);

    char outName[128] = "";
    DWORD pos = mix->bodyOffset + bikOff;
    BOOL found = FindBikNameInMix(path, pos, outName, sizeof(outName));
    EXPECT_TRUE(found);
    EXPECT_STREQ(outName, name);

    DeleteFileA(path);
}

TEST_F(MixCryptoTest, EncryptedTruncatedHeaderFails) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_trunc.mix", TEST_DATA_DIR);

    // sig+flags+partial key_source
    uint8_t data[20] = {0,0,2,0};
    for (int i = 4; i < 20; i++) data[i] = (uint8_t)i;
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, 20, f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    EXPECT_EQ(result, (MixArchive*)NULL);
    DeleteFileA(path);
}

TEST_F(MixCryptoTest, EncryptedGarbageKeySourceInvalidCount) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_garbage.mix", TEST_DATA_DIR);

    // Random key_source + random ciphertext -> decrypted count almost certainly invalid
    uint8_t data[4 + 80 + 64];
    data[0]=0; data[1]=0; data[2]=2; data[3]=0;
    for (int i = 4; i < (int)sizeof(data); i++) data[i] = (uint8_t)(i * 37 + 11);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    MixArchive* result = ParseMixFile(path);
    // May rarely pass count check; if it "parses", encrypted flag must be set
    if (result) {
        EXPECT_EQ(result->encrypted, 1);
        EXPECT_GT(result->fileCount, 0);
    }
    DeleteFileA(path);
}

TEST_F(MixCryptoTest, UnencryptedStillWorks) {
    // Regression: third-party test.mix unencrypted path unchanged
    std::string path = std::string(THIRD_PARTY_DIR) + "\\test.mix";
    MixArchive* mix = ParseMixFile(path.c_str());
    ASSERT_NE(mix, (MixArchive*)NULL);
    EXPECT_TRUE(mix->valid);
    EXPECT_EQ(mix->encrypted, 0);
    EXPECT_GT(mix->fileCount, 0);
    EXPECT_GT(mix->bodyOffset, 0u);
}

TEST_F(MixCryptoTest, LargeIndexAbove256) {
    // Mix archives can hold far more than 256 files — index is heap-allocated.
    const uint16_t count = 1000;
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_large.mix", TEST_DATA_DIR);

    // Minimal body: count*1 zero bytes so offsets stay in range
    std::vector<uint8_t> body(count, 0);
    std::vector<uint32_t> crcs(count);
    std::vector<std::pair<uint32_t,uint32_t>> os(count);
    for (uint16_t i = 0; i < count; i++) {
        crcs[i] = 0x10000000u + i;
        os[i] = { (uint32_t)i, 1u };
    }

    WriteEncryptedMix(path, kLangKeySource, body, count, os, crcs);

    MixArchive* mix = ParseMixFile(path);
    ASSERT_NE(mix, (MixArchive*)NULL);
    EXPECT_TRUE(mix->valid);
    EXPECT_EQ(mix->encrypted, 1);
    EXPECT_EQ(mix->fileCount, count);
    ASSERT_NE(mix->entries, (MixEntry*)NULL);
    EXPECT_EQ(mix->entries[0].crc, 0x10000000u);
    EXPECT_EQ(mix->entries[999].crc, 0x10000000u + 999u);
    EXPECT_EQ(mix->entries[999].offset, 999u);

    DeleteFileA(path);
}

// Worst-case LMD: 65535 index entries and a 32K-name database. The resolver
// used to linear-scan every name across the whole index — O(names x entries),
// ~2.1e9 comparisons for this fixture — burning seconds of CPU on every
// archive open (in the game: seconds of hitching). The crc -> entry index
// keeps it near-linear; the budget below fails on the linear-scan build.
TEST_F(MixCryptoTest, LmdResolveBoundedWithManyEntries) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_lmd_big.mix", TEST_DATA_DIR);

    const uint16_t count = 65535;
    const uint32_t finalCrc = MixCrc32("ZFINAL.BIK");
    EXPECT_EQ(finalCrc, 0x39770F60u); // python zlib.crc32 over TS-padded name

    // LMD: 52-byte header + 32768 decoy names ("N00000".."N32767") + final name.
    std::vector<uint8_t> lmd(52, 0);
    char decoy[16];
    for (int i = 0; i < 32768; i++) {
        snprintf(decoy, sizeof(decoy), "N%05d", i);
        lmd.insert(lmd.end(), decoy, decoy + strlen(decoy) + 1);
    }
    const char* finalName = "ZFINAL.BIK";
    lmd.insert(lmd.end(), finalName, finalName + strlen(finalName) + 1);

    // Body: 65535 one-byte payloads (offsets 0..65534), LMD right after them.
    std::vector<uint8_t> body(65535, 'F');
    body.insert(body.end(), lmd.begin(), lmd.end());

    std::vector<uint32_t> crcs(count);
    std::vector<std::pair<uint32_t,uint32_t>> os(count);
    for (uint32_t i = 0; i < 65533; i++) {
        uint32_t c = 0x10000000u + i;
        while (c == finalCrc || c == 0x366E051Fu) c++; // keep the sentinel crcs unique
        crcs[i] = c;
        os[i] = { i, 1u };
    }
    crcs[65533] = finalCrc;        os[65533] = { 65533u, 1u };
    crcs[65534] = 0x366E051Fu;     os[65534] = { 65535u, (uint32_t)lmd.size() };

    WriteEncryptedMix(path, kLangKeySource, body, count, os, crcs);

    ULONGLONG t0 = GetTickCount64();
    MixArchive* mix = ParseMixFile(path);
    ULONGLONG elapsed = GetTickCount64() - t0;

    ASSERT_NE(mix, (MixArchive*)NULL);
    EXPECT_TRUE(mix->valid);
    EXPECT_EQ(mix->fileCount, count);
    ASSERT_NE(mix->entries, (MixEntry*)NULL);

    // The decoys must not disturb the one real mapping.
    EXPECT_STREQ(mix->entries[65533].name, finalName);
    EXPECT_EQ(mix->entries[65534].crc, 0x366E051Fu);
    EXPECT_EQ(mix->entries[65534].size, (uint32_t)lmd.size());

    char outName[128] = "";
    BOOL found = FindBikNameInMix(path, mix->bodyOffset + 65533u, outName, sizeof(outName));
    EXPECT_TRUE(found);
    EXPECT_STREQ(outName, finalName);

    EXPECT_LT(elapsed, 1000ull)
        << "LMD resolve took " << elapsed << " ms — O(names x entries) scan is back";

    DeleteFileA(path);
}

// An LMD name is raw .mix bytes: CR/LF in one splits the "LMD resolved:" log
// line and forges entries. Control bytes must never reach the stored name.
TEST_F(MixCryptoTest, LmdNameControlCharsAreSanitized) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\enc_lmd_ctrl.mix", TEST_DATA_DIR);

    const char* evil = "evil\r\nFAKE LOG LINE";
    const uint32_t evilCrc = MixCrc32(evil);

    std::vector<uint8_t> lmd(52, 0);                     // 52-byte header, no names
    lmd.insert(lmd.end(), evil, evil + strlen(evil) + 1);

    std::vector<uint8_t> body(1, 'F');                   // one payload byte
    body.insert(body.end(), lmd.begin(), lmd.end());      // LMD right after it

    std::vector<uint32_t> crcs = { evilCrc, 0x366E051Fu };
    std::vector<std::pair<uint32_t, uint32_t>> os = {
        { 0u, 1u },
        { 1u, (uint32_t)lmd.size() }
    };

    WriteEncryptedMix(path, kLangKeySource, body, 2, os, crcs);

    MixArchive* mix = ParseMixFile(path);
    ASSERT_NE(mix, (MixArchive*)NULL);
    ASSERT_TRUE(mix->valid);
    ASSERT_NE(mix->entries, (MixEntry*)NULL);

    EXPECT_STREQ(mix->entries[0].name, "evil??FAKE LOG LINE")
        << "each control byte must be replaced before the name is stored";
    EXPECT_EQ(strcspn(mix->entries[0].name, "\r\n"), strlen(mix->entries[0].name))
        << "no CR/LF may survive into the value the log prints";

    DeleteFileA(path);
}
