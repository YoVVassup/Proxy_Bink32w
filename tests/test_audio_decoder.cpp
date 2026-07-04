#include <gtest/gtest.h>
#include "test_helpers.h"
#include "audio_decoder.h"
#include <cstring>
#include <cstdio>

// ============================================================================
// Tests for WAV/OGG audio decoder
//
// RA2/YR WAV format: PCM 22050/44100 Hz, 16-bit, mono/stereo
// Also supports: 8-bit, 24-bit, stereo, various sample rates.
// ============================================================================

class AudioDecoderTest : public ::testing::Test {
protected:
    char tempDir[MAX_PATH];

    void SetUp() override {
        GetTempPathA(MAX_PATH, tempDir);
        strcat_s(tempDir, "bink32w_audio_test\\");
        CreateDirectoryA(tempDir, NULL);
    }

    void TearDown() override {
        char pattern[MAX_PATH];
        _snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s*", tempDir);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char filePath[MAX_PATH];
                _snprintf_s(filePath, sizeof(filePath), _TRUNCATE, "%s%s", tempDir, fd.cFileName);
                DeleteFileA(filePath);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        RemoveDirectoryA(tempDir);
    }

    // Write a WAV file with specified parameters
    void WriteWav(const char* filename, WORD channels, DWORD sampleRate,
                  WORD bitsPerSample, const void* data, DWORD dataSize) {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s%s", tempDir, filename);

        WORD blockAlign = channels * bitsPerSample / 8;
        DWORD avgBytesPerSec = sampleRate * blockAlign;
        uint32_t riffSize = 4 + (8 + 16) + (8 + dataSize);

        FILE* f = NULL;
        fopen_s(&f, path, "wb");
        if (!f) return;

        // RIFF header
        fwrite("RIFF", 1, 4, f);
        fwrite(&riffSize, 4, 1, f);
        fwrite("WAVE", 1, 4, f);

        // fmt chunk
        fwrite("fmt ", 1, 4, f);
        uint32_t fmtSize = 16;
        fwrite(&fmtSize, 4, 1, f);
        uint16_t formatTag = 1; // PCM
        fwrite(&formatTag, 2, 1, f);
        fwrite(&channels, 2, 1, f);
        fwrite(&sampleRate, 4, 1, f);
        fwrite(&avgBytesPerSec, 4, 1, f);
        fwrite(&blockAlign, 2, 1, f);
        fwrite(&bitsPerSample, 2, 1, f);

        // data chunk
        fwrite("data", 1, 4, f);
        fwrite(&dataSize, 4, 1, f);
        if (data && dataSize > 0) fwrite(data, 1, dataSize, f);

        fclose(f);
    }

    std::string GetPath(const char* filename) {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s%s", tempDir, filename);
        return std::string(path);
    }
};

// ============================================================================
// Basic WAV decode tests
// ============================================================================

TEST_F(AudioDecoderTest, DecodeRa2StandardWav) {
    // RA2 standard: 22050 Hz, 16-bit, mono
    int16_t samples[100];
    for (int i = 0; i < 100; i++) samples[i] = (int16_t)(i * 100);
    WriteWav("ra2_standard.wav", 1, 22050, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("ra2_standard.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_NE(audio.pcmData, (char*)NULL);
    EXPECT_GT(audio.pcmSize, 0u);
    EXPECT_EQ(audio.format.wFormatTag, WAVE_FORMAT_PCM);
    EXPECT_EQ(audio.format.nChannels, 1);
    EXPECT_EQ(audio.format.nSamplesPerSec, 22050u);
    EXPECT_EQ(audio.format.wBitsPerSample, 16);
    EXPECT_EQ(audio.format.nBlockAlign, 2);
    EXPECT_EQ(audio.format.nAvgBytesPerSec, 44100u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, Decode8bitMono) {
    uint8_t samples[50];
    memset(samples, 0x80, sizeof(samples));
    WriteWav("8bit.wav", 1, 8000, 8, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("8bit.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nChannels, 1);
    EXPECT_EQ(audio.format.nSamplesPerSec, 8000u);
    EXPECT_EQ(audio.format.wBitsPerSample, 8);
    EXPECT_EQ(audio.format.nBlockAlign, 1);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, Decode24bitMonoRejected) {
    // 24-bit WAV is rejected (WaveOut doesn't support 24-bit PCM)
    uint8_t samples[300]; // 100 samples * 3 bytes
    memset(samples, 0, sizeof(samples));
    WriteWav("24bit.wav", 1, 44100, 24, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("24bit.wav").c_str(), &audio);

    EXPECT_FALSE(result);
    EXPECT_EQ(audio.pcmData, (char*)NULL);
}

TEST_F(AudioDecoderTest, DecodeStereo) {
    int16_t samples[200]; // 100 frames * 2 channels
    WriteWav("stereo.wav", 2, 44100, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("stereo.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nChannels, 2);
    EXPECT_EQ(audio.format.nBlockAlign, 4);
    EXPECT_EQ(audio.format.nAvgBytesPerSec, 176400u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, Decode44100Hz) {
    int16_t samples[441]; // ~10ms at 44100 Hz
    WriteWav("44100.wav", 1, 44100, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("44100.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nSamplesPerSec, 44100u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, Decode11025Hz) {
    int16_t samples[110];
    WriteWav("11025.wav", 1, 11025, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("11025.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nSamplesPerSec, 11025u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, Decode48000Hz) {
    int16_t samples[480];
    WriteWav("48000.wav", 1, 48000, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("48000.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nSamplesPerSec, 48000u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

// ============================================================================
// PCM data correctness tests
// ============================================================================

TEST_F(AudioDecoderTest, PcmDataCorrectness) {
    int16_t samples[10];
    for (int i = 0; i < 10; i++) samples[i] = (int16_t)(i * 1000 + 42);
    WriteWav("pcm_check.wav", 1, 22050, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("pcm_check.wav").c_str(), &audio);
    ASSERT_TRUE(result);
    ASSERT_NE(audio.pcmData, (char*)NULL);
    ASSERT_EQ(audio.pcmSize, sizeof(samples));

    int16_t* decoded = (int16_t*)audio.pcmData;
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(decoded[i], samples[i]) << "Sample " << i << " mismatch";
    }

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, PcmDataAllZeros) {
    uint8_t samples[100] = {0};
    WriteWav("zeros.wav", 1, 22050, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("zeros.wav").c_str(), &audio);
    ASSERT_TRUE(result);
    ASSERT_NE(audio.pcmData, (char*)NULL);

    for (DWORD i = 0; i < audio.pcmSize; i++) {
        EXPECT_EQ(audio.pcmData[i], 0) << "Byte " << i << " should be 0";
    }

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, PcmDataMaxValues) {
    int16_t samples[5] = {32767, -32768, 0, 1, -1};
    WriteWav("maxval.wav", 1, 22050, 16, samples, sizeof(samples));

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("maxval.wav").c_str(), &audio);
    ASSERT_TRUE(result);
    ASSERT_NE(audio.pcmData, (char*)NULL);

    int16_t* decoded = (int16_t*)audio.pcmData;
    EXPECT_EQ(decoded[0], 32767);
    EXPECT_EQ(decoded[1], -32768);
    EXPECT_EQ(decoded[2], 0);
    EXPECT_EQ(decoded[3], 1);
    EXPECT_EQ(decoded[4], -1);

    VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(AudioDecoderTest, DecodeMinimalWav) {
    uint8_t sample = 0x80;
    WriteWav("minimal.wav", 1, 8000, 8, &sample, 1);

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("minimal.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.pcmSize, 1u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, DecodeLargeWav) {
    // 1 second of 22050 Hz 16-bit mono = 44100 bytes
    DWORD samples = 22050;
    int16_t* data = (int16_t*)malloc(samples * 2);
    ASSERT_NE(data, (int16_t*)NULL);
    for (DWORD i = 0; i < samples; i++) data[i] = (int16_t)(i % 32768);

    WriteWav("large.wav", 1, 22050, 16, data, samples * 2);
    free(data);

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(GetPath("large.wav").c_str(), &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.pcmSize, samples * 2);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

// ============================================================================
// Error cases
// ============================================================================

TEST_F(AudioDecoderTest, InvalidRiffHeader) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    WriteWav("bad_riff.wav", 1, 22050, 16, data, sizeof(data));
    // Overwrite RIFF with garbage
    FILE* f = NULL;
    fopen_s(&f, GetPath("bad_riff.wav").c_str(), "rb+");
    if (f) { fwrite("NOPE", 1, 4, f); fclose(f); }

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(GetPath("bad_riff.wav").c_str(), &audio));
}

TEST_F(AudioDecoderTest, WrongWaveMarker) {
    // RIFF but not WAVE
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%snotwave.wav", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);
    uint32_t riffSize = 4;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("AVI ", 1, 4, f); // not WAVE
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
}

TEST_F(AudioDecoderTest, NonPcmFormatTag) {
    // wFormatTag = 3 (IEEE Float)
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sfloat.wav", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);

    uint32_t riffSize = 4 + (8 + 16) + (8 + 4);
    uint32_t fmtSize = 16;
    uint32_t dataSize = 4;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, 4, 1, f);
    uint16_t formatTag = 3; // IEEE Float
    uint16_t channels = 1;
    uint32_t sampleRate = 44100;
    uint16_t bitsPerSample = 32;
    uint16_t blockAlign = 4;
    uint32_t avgBytesPerSec = 176400;
    fwrite(&formatTag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&avgBytesPerSec, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    uint8_t zeros[4] = {0};
    fwrite(zeros, 1, 4, f);
    fclose(f);

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
}

TEST_F(AudioDecoderTest, NonExistentFile) {
    DecodedAudio audio = {0};
    char badPath[MAX_PATH];
    _snprintf_s(badPath, sizeof(badPath), _TRUNCATE, "%s\\nonexistent.wav", TEST_DATA_DIR);
    EXPECT_FALSE(DecodeAudioFile(badPath, &audio));
}

TEST_F(AudioDecoderTest, NonWavNonOgg) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sreadme.txt", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "w");
    if (f) { fwrite("hello", 1, 5, f); fclose(f); }

    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
}

TEST_F(AudioDecoderTest, NullInputs) {
    DecodedAudio audio = {0};
    EXPECT_FALSE(DecodeAudioFile(NULL, &audio));
    EXPECT_FALSE(DecodeAudioFile("test.wav", NULL));
}

TEST_F(AudioDecoderTest, ZeroLengthDataChunk) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%szero.wav", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);

    uint32_t riffSize = 4 + (8 + 16) + (8 + 0);
    uint32_t fmtSize = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, 4, 1, f);
    uint16_t formatTag = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = 22050;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = 2;
    uint32_t avgBytesPerSec = 44100;
    fwrite(&formatTag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&avgBytesPerSec, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    uint32_t zeroSize = 0;
    fwrite(&zeroSize, 4, 1, f);
    fclose(f);

    DecodedAudio audio = {0};
    // Zero-length data chunk should fail (no audio data)
    EXPECT_FALSE(DecodeAudioFile(path, &audio));
}

TEST_F(AudioDecoderTest, ExtraFmtBytes) {
    // fmt chunk with extra bytes (cbSize > 0) — should skip extra bytes
    int16_t samples[10] = {0};
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sextra_fmt.wav", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);

    uint32_t fmtSize = 18; // 16 + 2 extra bytes
    uint32_t dataSize = sizeof(samples);
    uint32_t riffSize = 4 + (8 + fmtSize) + (8 + dataSize);

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, 4, 1, f);
    uint16_t formatTag = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = 22050;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = 2;
    uint32_t avgBytesPerSec = 44100;
    fwrite(&formatTag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&avgBytesPerSec, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    uint16_t extraByte = 0; // extra fmt data
    fwrite(&extraByte, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    fwrite(samples, 1, sizeof(samples), f);
    fclose(f);

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(path, &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nSamplesPerSec, 22050u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}

TEST_F(AudioDecoderTest, UnknownChunkBeforeFmt) {
    // Unknown chunk between RIFF and fmt — parser should skip it
    int16_t samples[10] = {0};
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sunknown_chunk.wav", tempDir);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    ASSERT_NE(f, (FILE*)NULL);

    uint32_t junkSize = 64;
    uint32_t fmtSize = 16;
    uint32_t dataSize = sizeof(samples);
    uint32_t riffSize = 4 + (8 + junkSize) + (8 + fmtSize) + (8 + dataSize);

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // Junk chunk
    fwrite("JUNK", 1, 4, f);
    fwrite(&junkSize, 4, 1, f);
    uint8_t junk[64] = {0};
    fwrite(junk, 1, 64, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, 4, 1, f);
    uint16_t formatTag = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = 22050;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = 2;
    uint32_t avgBytesPerSec = 44100;
    fwrite(&formatTag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&avgBytesPerSec, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);

    // data chunk
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    fwrite(samples, 1, sizeof(samples), f);
    fclose(f);

    DecodedAudio audio = {0};
    BOOL result = DecodeAudioFile(path, &audio);

    EXPECT_TRUE(result);
    EXPECT_EQ(audio.format.nSamplesPerSec, 22050u);

    if (audio.pcmData) VirtualFree(audio.pcmData, 0, MEM_RELEASE);
}
