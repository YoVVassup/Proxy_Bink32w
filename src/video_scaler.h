#pragma once
#include <cstdint>

enum ScaleMode {
    SCALE_NEAREST = 0,
    SCALE_BILINEAR,
    SCALE_AREA,
    SCALE_SHARPEN_AREA,
    SCALE_SHARPEN_BILINEAR,
    SCALE_SCANLINE,
    SCALE_SHARPEN,
    SCALE_COLOR_DITHER,
    SCALE_CRT_SCANLINE,
    SCALE_COUNT
};

const char* ScaleModeName(ScaleMode m);
ScaleMode ScaleModeFromName(const char* name);

void ScaleFrame(const uint8_t* src, int sw, int sh, int sp, int bpp,
                uint8_t* dst, int dw, int dh, int dp, ScaleMode mode);
