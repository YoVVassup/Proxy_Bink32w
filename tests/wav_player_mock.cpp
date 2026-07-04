// ============================================================================
// wav_player_mock.cpp — wav_player.cpp compiled with mock WaveOut API
//
// This file is compiled INSTEAD of wav_player.cpp for test builds.
// mock_waveout.h redirects all waveOut* calls to mock implementations.
// ============================================================================

#include "mock_waveout.h"

// wav_player.cpp includes binkw32_proxy.h which includes mmsystem.h,
// but since mock_waveout.h is already included, the waveOut* macros
// are active and all calls become mock calls.

#include "../src/wav_player.cpp"
