#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstring>

// ============================================================================
// Third-party data tests — OGG decode, real WAV decode, .mix parsing
//
// Uses real game data files from third-party/ directory:
//   - a04_f00e.ogg  (1.7 MB) — OGG Vorbis audio
//   - a04_f00e.wav  (6.3 MB) — PCM WAV audio
//   - test.mix      (42.6 MB) — RA2 .mix archive
// ============================================================================

static std::string ThirdPartyPath(const char* filename) {
    return std::string(THIRD_PARTY_DIR) + "\\" + filename;
}

// ============================================================================
// OGG Decode Tests
// ============================================================================

TEST(ThirdParty_Ogg, DecodeRealOggFile) {
    std::string path = ThirdPartyPath("a04_f00e.ogg");
    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(path.c_str(), &audio);

    EXPECT_TRUE(result) << "Failed to decode: " << path;
    EXPECT_NE(audio.pcmData, (char*)NULL);
    EXPECT_GT(audio.pcmSize, 0u);
    EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
    EXPECT_GT(audio.format.nSamplesPerSec, 0u);
    EXPECT_GE(audio.format.nChannels, 1);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST(ThirdParty_Ogg, OggFormatDetails) {
    std::string path = ThirdPartyPath("a04_f00e.ogg");
    DecodedAudio audio = {0};
    if (!DecodeAudioFile(path.c_str(), &audio)) {
        GTEST_SKIP() << "Cannot decode " << path;
    }

    // OGG output is always 16-bit PCM
    EXPECT_EQ(audio.format.wBitsPerSample, 16);
    EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
    EXPECT_EQ(audio.format.cbSize, 0);

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST(ThirdParty_Ogg, OggPcmDataNonZero) {
    std::string path = ThirdPartyPath("a04_f00e.ogg");
    DecodedAudio audio = {0};
    if (!DecodeAudioFile(path.c_str(), &audio)) {
        GTEST_SKIP() << "Cannot decode " << path;
    }

    // Audio data should contain non-zero samples (not silence)
    ASSERT_GT(audio.pcmSize, 0u);
    int16_t* samples = (int16_t*)audio.pcmData;
    int numSamples = audio.pcmSize / 2;
    int nonZeroCount = 0;
    for (int i = 0; i < numSamples; i++) {
        if (samples[i] != 0) nonZeroCount++;
    }
    EXPECT_GT(nonZeroCount, numSamples / 10) << "Audio is mostly silence";

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST(ThirdParty_Ogg, OggPcmSizeConsistent) {
    std::string path = ThirdPartyPath("a04_f00e.ogg");
    DecodedAudio audio1 = {0}, audio2 = {0};
    if (!DecodeAudioFile(path.c_str(), &audio1)) {
        GTEST_SKIP() << "Cannot decode " << path;
    }
    DecodeAudioFile(path.c_str(), &audio2);

    // Two decodes of the same file should produce identical sizes
    EXPECT_EQ(audio1.pcmSize, audio2.pcmSize);

    // And identical data
    if (audio1.pcmData && audio2.pcmData) {
        EXPECT_EQ(memcmp(audio1.pcmData, audio2.pcmData, audio1.pcmSize), 0);
    }

    if (audio1.pcmData) VirtualFree(audio1.pcmData, 0, MEM_RELEASE);
    if (audio2.pcmData) VirtualFree(audio2.pcmData, 0, MEM_RELEASE);
}

// ============================================================================
// Real WAV Decode Tests
// ============================================================================

TEST(ThirdParty_Wav, DecodeRealWavFile) {
    std::string path = ThirdPartyPath("a04_f00e.wav");
    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(path.c_str(), &audio);

    EXPECT_TRUE(result) << "Failed to decode: " << path;
    EXPECT_NE(audio.pcmData, (char*)NULL);
    EXPECT_GT(audio.pcmSize, 0u);
    EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST(ThirdParty_Wav, WavFormatDetails) {
    std::string path = ThirdPartyPath("a04_f00e.wav");
    DecodedAudio audio = {0};
    if (!DecodeAudioFile(path.c_str(), &audio)) {
        GTEST_SKIP() << "Cannot decode " << path;
    }

    // Should be valid PCM format
    EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
    EXPECT_GT(audio.format.nSamplesPerSec, 0u);
    EXPECT_GE(audio.format.nChannels, 1);
    EXPECT_GE(audio.format.wBitsPerSample, 8);

    // blockAlign should be channels * bitsPerSample / 8
    EXPECT_EQ(audio.format.nBlockAlign,
              audio.format.nChannels * audio.format.wBitsPerSample / 8);

    // avgBytesPerSec should be sampleRate * blockAlign
    EXPECT_EQ(audio.format.nAvgBytesPerSec,
              audio.format.nSamplesPerSec * audio.format.nBlockAlign);

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST(ThirdParty_Wav, WavPcmDataNonZero) {
    std::string path = ThirdPartyPath("a04_f00e.wav");
    DecodedAudio audio = {0};
    if (!DecodeAudioFile(path.c_str(), &audio)) {
        GTEST_SKIP() << "Cannot decode " << path;
    }

    ASSERT_GT(audio.pcmSize, 0u);

    // Check that audio contains non-zero samples
    if (audio.format.wBitsPerSample == 16) {
        int16_t* samples = (int16_t*)audio.pcmData;
        int numSamples = audio.pcmSize / 2;
        int nonZeroCount = 0;
        for (int i = 0; i < numSamples && i < 44100; i++) {
            if (samples[i] != 0) nonZeroCount++;
        }
        EXPECT_GT(nonZeroCount, 0) << "First second is all silence";
    }

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST(ThirdParty_Wav, WavPcmSizeConsistent) {
    std::string path = ThirdPartyPath("a04_f00e.wav");
    DecodedAudio audio1 = {0}, audio2 = {0};
    if (!DecodeAudioFile(path.c_str(), &audio1)) {
        GTEST_SKIP() << "Cannot decode " << path;
    }
    DecodeAudioFile(path.c_str(), &audio2);

    EXPECT_EQ(audio1.pcmSize, audio2.pcmSize);
    if (audio1.pcmData && audio2.pcmData) {
        EXPECT_EQ(memcmp(audio1.pcmData, audio2.pcmData, audio1.pcmSize), 0);
    }

    if (audio1.pcmData) VirtualFree(audio1.pcmData, 0, MEM_RELEASE);
    if (audio2.pcmData) VirtualFree(audio2.pcmData, 0, MEM_RELEASE);
}

// ============================================================================
// WAV vs OGG comparison — same content, different containers
// ============================================================================

TEST(ThirdParty_CrossFormat, WavAndOggProduceSameContent) {
    std::string wavPath = ThirdPartyPath("a04_f00e.wav");
    std::string oggPath = ThirdPartyPath("a04_f00e.ogg");

    DecodedAudio wav = {0}, ogg = {0};
    if (!DecodeAudioFile(wavPath.c_str(), &wav)) {
        GTEST_SKIP() << "Cannot decode WAV";
    }
    if (!DecodeAudioFile(oggPath.c_str(), &ogg)) {
        GTEST_SKIP() << "Cannot decode OGG";
    }

    // Both should decode to 16-bit PCM
    EXPECT_EQ(wav.format.wBitsPerSample, 16);
    EXPECT_EQ(ogg.format.wBitsPerSample, 16);

    // Both should have the same sample rate
    EXPECT_EQ(wav.format.nSamplesPerSec, ogg.format.nSamplesPerSec);

    // Both should have the same channel count
    EXPECT_EQ(wav.format.nChannels, ogg.format.nChannels);

    // PCM sizes should be similar (OGG may have slight rounding)
    double sizeRatio = (double)ogg.pcmSize / wav.pcmSize;
    EXPECT_GT(sizeRatio, 0.95) << "OGG PCM size too different from WAV";
    EXPECT_LT(sizeRatio, 1.05) << "OGG PCM size too different from WAV";

    if (wav.pcmData) VirtualFree(wav.pcmData, 0, MEM_RELEASE);
    if (ogg.pcmData) VirtualFree(ogg.pcmData, 0, MEM_RELEASE);
}

// ============================================================================
// .mix Archive Parsing Tests
// ============================================================================

TEST(ThirdParty_Mix, ParseRealMixFile) {
    std::string path = ThirdPartyPath("test.mix");
    MixArchive* mix = ParseMixFile(path.c_str());

    ASSERT_NE(mix, (MixArchive*)NULL) << "Failed to parse: " << path;
    EXPECT_TRUE(mix->valid);
    EXPECT_GT(mix->fileCount, 0);
    EXPECT_LE(mix->fileCount, 256);

    // Should contain LMD entry (CRC 0x366E051F)
    BOOL hasLmd = FALSE;
    for (uint16_t i = 0; i < mix->fileCount; i++) {
        if (mix->entries[i].crc == 0x366E051F) {
            hasLmd = TRUE;
            EXPECT_GT(mix->entries[i].size, 52u) << "LMD too small";
            break;
        }
    }
    EXPECT_TRUE(hasLmd) << "LMD entry not found in .mix file";
}

TEST(ThirdParty_Mix, MixFileCount) {
    std::string path = ThirdPartyPath("test.mix");
    MixArchive* mix = ParseMixFile(path.c_str());
    ASSERT_NE(mix, (MixArchive*)NULL);

    // test.mix should have at least 1 file (excluding LMD)
    EXPECT_GE(mix->fileCount, 1);
}

TEST(ThirdParty_Mix, MixCacheSecondCall) {
    std::string path = ThirdPartyPath("test.mix");
    MixArchive* mix1 = ParseMixFile(path.c_str());
    MixArchive* mix2 = ParseMixFile(path.c_str());

    ASSERT_NE(mix1, (MixArchive*)NULL);
    ASSERT_NE(mix2, (MixArchive*)NULL);

    // Second call should return cached result (same pointer)
    EXPECT_EQ(mix1, mix2);
}

TEST(ThirdParty_Mix, FindBikNameResolves) {
    std::string path = ThirdPartyPath("test.mix");
    MixArchive* mix = ParseMixFile(path.c_str());
    ASSERT_NE(mix, (MixArchive*)NULL);

    // Find an entry that has a known offset
    for (uint16_t i = 0; i < mix->fileCount; i++) {
        if (mix->entries[i].crc == 0x366E051F) continue; // Skip LMD
        if (mix->entries[i].size == 0) continue;

        char name[MAX_PATH] = "";
        BOOL found = FindBikNameInMix(path.c_str(),
                                       0xA + mix->fileCount * 12 + mix->entries[i].offset,
                                       name, sizeof(name));
        if (found) {
            EXPECT_GT(strlen(name), 0u) << "Empty name resolved";
            // Should end with .bik
            const char* ext = strrchr(name, '.');
            EXPECT_NE(ext, (const char*)NULL) << "No extension in: " << name;
            if (ext) EXPECT_STREQ(ext, ".bik");
            return; // Test passes on first successful resolve
        }
    }

    // If no entry resolved, skip (some .mix files may not have LMD)
    GTEST_SKIP() << "No .bik names resolved from test.mix";
}

TEST(ThirdParty_Mix, LmdCrcMatchesEntries) {
    std::string path = ThirdPartyPath("test.mix");
    MixArchive* mix = ParseMixFile(path.c_str());
    ASSERT_NE(mix, (MixArchive*)NULL);

    // Resolve all names via FindBikNameInMix and verify CRC32 matches
    uint32_t bodyOffset = 0xA + mix->fileCount * 12;
    int resolved = 0;
    for (uint16_t i = 0; i < mix->fileCount; i++) {
        if (mix->entries[i].crc == 0x366E051F) continue;
        if (mix->entries[i].size == 0) continue;

        char name[MAX_PATH] = "";
        BOOL found = FindBikNameInMix(path.c_str(),
                                       bodyOffset + mix->entries[i].offset,
                                       name, sizeof(name));
        if (found && name[0]) {
            uint32_t computed = MixCrc32(name);
            EXPECT_EQ(computed, mix->entries[i].crc)
                << "CRC mismatch for resolved name: " << name
                << " (expected 0x" << std::hex << mix->entries[i].crc
                << " got 0x" << computed << ")" << std::dec;
            resolved++;
        }
    }

    EXPECT_GT(resolved, 0) << "No names resolved from test.mix";
}

TEST(ThirdParty_Mix, AllEntriesHaveValidOffsets) {
    std::string path = ThirdPartyPath("test.mix");
    MixArchive* mix = ParseMixFile(path.c_str());
    ASSERT_NE(mix, (MixArchive*)NULL);

    WIN32_FILE_ATTRIBUTE_DATA fad;
    ASSERT_TRUE(GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad));
    uint32_t fileSize = fad.nFileSizeLow;

    uint32_t bodyOffset = 0xA + mix->fileCount * 12;
    for (uint16_t i = 0; i < mix->fileCount; i++) {
        if (mix->entries[i].crc == 0x366E051F) continue;
        if (mix->entries[i].size == 0) continue;

        uint32_t entryEnd = bodyOffset + mix->entries[i].offset + mix->entries[i].size;
        EXPECT_LE(entryEnd, fileSize)
            << "Entry " << i << " extends past EOF (offset=" << mix->entries[i].offset
            << " size=" << mix->entries[i].size << ")";
    }
}
