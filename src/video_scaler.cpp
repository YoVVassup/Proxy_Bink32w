#include <cmath>
#include <cstring>
#include <algorithm>
#include "video_scaler.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char* ScaleModeName(ScaleMode m) {
    static const char* names[] = {
        "nearest", "bilinear", "area", "sharpen-area", "sharpen-bilinear",
        "scanline", "sharpen",
        "color-dither", "crt-scanline"
    };
    return (m >= 0 && m < SCALE_COUNT) ? names[m] : "bilinear";
}

ScaleMode ScaleModeFromName(const char* name) {
    if (!name || !name[0]) return SCALE_BILINEAR;
    for (int i = 0; i < SCALE_COUNT; i++) {
        if (_stricmp(name, ScaleModeName((ScaleMode)i)) == 0) return (ScaleMode)i;
    }
    return SCALE_BILINEAR;
}

// ============================================================================
// Helpers
// ============================================================================
static inline int Clamp(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
static inline void Unpack565(uint16_t p, int& r, int& g, int& b) {
    r = (p >> 11) & 0x1F; g = (p >> 5) & 0x3F; b = p & 0x1F;
}
static inline uint16_t Pack565(int r, int g, int b) {
    return (uint16_t)((Clamp(r,0,31) << 11) | (Clamp(g,0,63) << 5) | Clamp(b,0,31));
}
static inline void Unpack888(const uint8_t* p, int& r, int& g, int& b) {
    r = p[0]; g = p[1]; b = p[2];
}
static inline void Pack888(uint8_t* p, int r, int g, int b) {
    p[0]=(uint8_t)Clamp(r,0,255); p[1]=(uint8_t)Clamp(g,0,255); p[2]=(uint8_t)Clamp(b,0,255);
}
static inline uint16_t S565(const uint8_t* s, int sp, int sw, int sh, int x, int y) {
    return *(const uint16_t*)(s + Clamp(y,0,sh-1)*sp + Clamp(x,0,sw-1)*2);
}
static inline void S888(const uint8_t* s, int sp, int sw, int sh, int x, int y, int& r, int& g, int& b) {
    const uint8_t* p = s + Clamp(y,0,sh-1)*sp + Clamp(x,0,sw-1)*3;
    r=p[0]; g=p[1]; b=p[2];
}

// ============================================================================
// 1. Nearest-neighbor
// ============================================================================
static void SNearest_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        int sy = Clamp(y*sh/dh, 0, sh-1);
        const uint16_t* sr = (const uint16_t*)(s + sy*sp);
        uint16_t* dr = (uint16_t*)(d + y*dp);
        for (int x = 0; x < dw; x++) dr[x] = sr[Clamp(x*sw/dw, 0, sw-1)];
    }
}
static void SNearest_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        int sy = Clamp(y*sh/dh, 0, sh-1);
        uint8_t* dr = d + y*dp;
        for (int x = 0; x < dw; x++) memcpy(dr+x*3, s+sy*sp+Clamp(x*sw/dw,0,sw-1)*3, 3);
    }
}

// ============================================================================
// 2. Bilinear
// ============================================================================
static void SBilinear_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        float syf = (float)y*sh/dh;
        int sy = (int)syf, sy2 = Clamp(sy+1, 0, sh-1);
        float fy = syf - sy, ify = 1.0f - fy;
        uint16_t* dr = (uint16_t*)(d + y*dp);
        for (int x = 0; x < dw; x++) {
            float sxf = (float)x*sw/dw;
            int sx = (int)sxf, sx2 = Clamp(sx+1, 0, sw-1);
            float fx = sxf - sx, ifx = 1.0f - fx;
            int r00,g00,b00,r10,g10,b10,r01,g01,b01,r11,g11,b11;
            Unpack565(S565(s,sp,sw,sh,sx,sy), r00,g00,b00);
            Unpack565(S565(s,sp,sw,sh,sx,sy2), r10,g10,b10);
            Unpack565(S565(s,sp,sw,sh,sx2,sy), r01,g01,b01);
            Unpack565(S565(s,sp,sw,sh,sx2,sy2), r11,g11,b11);
            int rv=(int)((r00*ifx+r10*fx)*ify+(r01*ifx+r11*fx)*fy);
            int gv=(int)((g00*ifx+g10*fx)*ify+(g01*ifx+g11*fx)*fy);
            int bv=(int)((b00*ifx+b10*fx)*ify+(b01*ifx+b11*fx)*fy);
            dr[x] = Pack565(rv, gv, bv);
        }
    }
}
static void SBilinear_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        float syf = (float)y*sh/dh;
        int sy = (int)syf, sy2 = Clamp(sy+1, 0, sh-1);
        float fy = syf - sy, ify = 1.0f - fy;
        uint8_t* dr = d + y*dp;
        for (int x = 0; x < dw; x++) {
            float sxf = (float)x*sw/dw;
            int sx = (int)sxf, sx2 = Clamp(sx+1, 0, sw-1);
            float fx = sxf - sx, ifx = 1.0f - fx;
            int r00,g00,b00,r10,g10,b10,r01,g01,b01,r11,g11,b11;
            S888(s,sp,sw,sh,sx,sy,r00,g00,b00);
            S888(s,sp,sw,sh,sx,sy2,r10,g10,b10);
            S888(s,sp,sw,sh,sx2,sy,r01,g01,b01);
            S888(s,sp,sw,sh,sx2,sy2,r11,g11,b11);
            Pack888(dr+x*3, (int)((r00*ifx+r10*fx)*ify+(r01*ifx+r11*fx)*fy),
                         (int)((g00*ifx+g10*fx)*ify+(g01*ifx+g11*fx)*fy),
                         (int)((b00*ifx+b10*fx)*ify+(b01*ifx+b11*fx)*fy));
        }
    }
}

// ============================================================================
// 3. Area averaging (box filter — best for downscaling)
// ============================================================================
static void SArea_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        int sy0 = y * sh / dh, sy1 = Clamp(sy0 + 1, 0, sh);
        uint16_t* dr = (uint16_t*)(d + y*dp);
        for (int x = 0; x < dw; x++) {
            int sx0 = x * sw / dw, sx1 = Clamp(sx0 + 1, 0, sw);
            int rr=0, gg=0, bb=0, cnt=0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint16_t* row = (const uint16_t*)(s + sy * sp);
                for (int sx = sx0; sx < sx1; sx++) {
                    int r,g,b; Unpack565(row[sx], r,g,b); rr+=r; gg+=g; bb+=b; cnt++;
                }
            }
            dr[x] = cnt > 0 ? Pack565(rr/cnt, gg/cnt, bb/cnt) : 0;
        }
    }
}
static void SArea_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        int sy0 = y * sh / dh, sy1 = Clamp(sy0 + 1, 0, sh);
        uint8_t* dr = d + y*dp;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * sw / dw, sx1 = Clamp(sx0 + 1, 0, sw);
            int rr=0, gg=0, bb=0, cnt=0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    const uint8_t* p = s + sy*sp + sx*3;
                    rr+=p[0]; gg+=p[1]; bb+=p[2]; cnt++;
                }
            Pack888(dr+x*3, cnt > 0 ? rr/cnt : 0, cnt > 0 ? gg/cnt : 0, cnt > 0 ? bb/cnt : 0);
        }
    }
}

// ============================================================================
// 4. Sharpen-area — area + edge-aware sharpening
// ============================================================================
static void SSharpenArea_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SArea_16(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 0; y < dh; y++) {
        int sy = Clamp(y * sh / dh, 0, sh - 1);
        uint16_t* dr = (uint16_t*)(d + y * dp);
        for (int x = 0; x < dw; x++) {
            int sx = Clamp(x * sw / dw, 0, sw - 1);
            int cr, cg, cb;
            Unpack565(S565(s, sp, sw, sh, sx, sy), cr, cg, cb);
            int rl,gl,bl,rr,gr,br,rt,gt,bt,rb2,gb2,bb2;
            Unpack565(S565(s, sp, sw, sh, sx - 1, sy), rl, gl, bl);
            Unpack565(S565(s, sp, sw, sh, sx + 1, sy), rr, gr, br);
            Unpack565(S565(s, sp, sw, sh, sx, sy - 1), rt, gt, bt);
            Unpack565(S565(s, sp, sw, sh, sx, sy + 1), rb2, gb2, bb2);
            int gx = abs(cr-rl)+abs(cr-rr)+abs(cg-gl)+abs(cg-gr)+abs(cb-bl)+abs(cb-br);
            int gy = abs(cr-rt)+abs(cr-rb2)+abs(cg-gt)+abs(cg-gb2)+abs(cb-bt)+abs(cb-bb2);
            int grad = gx + gy;
            if (grad > 24) {
                dr[x] = S565(s, sp, sw, sh, sx, sy);
            } else if (grad > 8) {
                int ar, ag, ab;
                Unpack565(dr[x], ar, ag, ab);
                dr[x] = Pack565((cr+ar)/2, (cg+ag)/2, (cb+ab)/2);
            }
        }
    }
}
static void SSharpenArea_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SArea_24(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 0; y < dh; y++) {
        int sy = Clamp(y * sh / dh, 0, sh - 1);
        uint8_t* dr = d + y * dp;
        for (int x = 0; x < dw; x++) {
            int sx = Clamp(x * sw / dw, 0, sw - 1);
            int cr, cg, cb;
            int rl,gl,bl,rr,gr,br,rt,gt,bt,rb2,gb2,bb2;
            S888(s, sp, sw, sh, sx, sy, cr, cg, cb);
            S888(s, sp, sw, sh, sx-1, sy, rl, gl, bl);
            S888(s, sp, sw, sh, sx+1, sy, rr, gr, br);
            S888(s, sp, sw, sh, sx, sy-1, rt, gt, bt);
            S888(s, sp, sw, sh, sx, sy+1, rb2, gb2, bb2);
            int gx = abs(cr-rl)+abs(cr-rr)+abs(cg-gl)+abs(cg-gr)+abs(cb-bl)+abs(cb-br);
            int gy = abs(cr-rt)+abs(cr-rb2)+abs(cg-gt)+abs(cg-gb2)+abs(cb-bt)+abs(cb-bb2);
            int grad = gx + gy;
            if (grad > 24) {
                dr[x*3] = (uint8_t)cr; dr[x*3+1] = (uint8_t)cg; dr[x*3+2] = (uint8_t)cb;
            } else if (grad > 8) {
                dr[x*3] = (uint8_t)((dr[x*3] + cr) / 2);
                dr[x*3+1] = (uint8_t)((dr[x*3+1] + cg) / 2);
                dr[x*3+2] = (uint8_t)((dr[x*3+2] + cb) / 2);
            }
        }
    }
}

// ============================================================================
// 5. Sharpen-bilinear (pre-sharpen + bilinear)
// ============================================================================
static void SSharpenBilinear_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    int tmpPitch = ((sw * 2 + 15) & ~15);
    uint8_t* tmp = (uint8_t*)malloc((size_t)tmpPitch * sh);
    if (!tmp) { SBilinear_16(s, sw, sh, sp, d, dw, dh, dp); return; }
    for (int y = 0; y < sh; y++) {
        const uint16_t* src = (const uint16_t*)(s + y * sp);
        uint16_t* dst = (uint16_t*)(tmp + y * tmpPitch);
        for (int x = 0; x < sw; x++) {
            int r,g,b; Unpack565(src[x], r,g,b);
            if (y > 0 && y < sh-1 && x > 0 && x < sw-1) {
                const uint16_t* above = (const uint16_t*)(s + (y-1)*sp);
                const uint16_t* below = (const uint16_t*)(s + (y+1)*sp);
                int ra,ga,ba,rb,gb,bb,rl,gl,bl,rr,gr,br;
                Unpack565(above[x], ra,ga,ba);
                Unpack565(below[x], rb,gb,bb);
                Unpack565(src[x-1], rl,gl,bl);
                Unpack565(src[x+1], rr,gr,br);
                int br2=(ra+rb+rl+rr)/4, bg2=(ga+gb+gl+gr)/4, bb2=(ba+bb+bl+br)/4;
                dst[x] = Pack565((int)(r+0.5f*(r-br2)), (int)(g+0.5f*(g-bg2)), (int)(b+0.5f*(b-bb2)));
            } else {
                dst[x] = src[x];
            }
        }
    }
    SBilinear_16(tmp, sw, sh, tmpPitch, d, dw, dh, dp);
    free(tmp);
}
static void SSharpenBilinear_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    int tmpPitch = ((sw * 3 + 15) & ~15);
    uint8_t* tmp = (uint8_t*)malloc((size_t)tmpPitch * sh);
    if (!tmp) { SBilinear_24(s, sw, sh, sp, d, dw, dh, dp); return; }
    for (int y = 0; y < sh; y++) {
        const uint8_t* src = s + y * sp;
        uint8_t* dst = tmp + y * tmpPitch;
        for (int x = 0; x < sw; x++) {
            int r=src[x*3], g=src[x*3+1], b=src[x*3+2];
            if (y > 0 && y < sh-1 && x > 0 && x < sw-1) {
                const uint8_t* above = s + (y-1)*sp;
                const uint8_t* below = s + (y+1)*sp;
                int blur = (above[x*3]+below[x*3]+src[(x-1)*3]+src[(x+1)*3])/4;
                int gblur = (above[x*3+1]+below[x*3+1]+src[(x-1)*3+1]+src[(x+1)*3+1])/4;
                int bblur = (above[x*3+2]+below[x*3+2]+src[(x-1)*3+2]+src[(x+1)*3+2])/4;
                dst[x*3] = (uint8_t)Clamp((int)(r + 0.5f*(r-blur)), 0, 255);
                dst[x*3+1] = (uint8_t)Clamp((int)(g + 0.5f*(g-gblur)), 0, 255);
                dst[x*3+2] = (uint8_t)Clamp((int)(b + 0.5f*(b-bblur)), 0, 255);
            } else {
                memcpy(dst+x*3, src+x*3, 3);
            }
        }
    }
    SBilinear_24(tmp, sw, sh, tmpPitch, d, dw, dh, dp);
    free(tmp);
}

// ============================================================================
// 6. Scanline (bilinear + alternating row darkening)
// ============================================================================
static void SScanline_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SBilinear_16(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 0; y < dh; y += 2) {
        uint16_t* dr = (uint16_t*)(d + y*dp);
        for (int x = 0; x < dw; x++) {
            int r,g,b; Unpack565(dr[x],r,g,b);
            dr[x] = Pack565(r*3/4, g*3/4, b*3/4);
        }
    }
}
static void SScanline_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SBilinear_24(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 0; y < dh; y += 2) {
        uint8_t* dr = d + y*dp;
        for (int x = 0; x < dw; x++) {
            dr[x*3]   = (uint8_t)(dr[x*3]   * 3 / 4);
            dr[x*3+1] = (uint8_t)(dr[x*3+1] * 3 / 4);
            dr[x*3+2] = (uint8_t)(dr[x*3+2] * 3 / 4);
        }
    }
}

// ============================================================================
// 9. Sharpen (bilinear + unsharp mask)
// ============================================================================
static void SSharpen_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SBilinear_16(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 1; y < dh-1; y++) {
        uint16_t* dr = (uint16_t*)(d + y*dp);
        const uint16_t* above = (const uint16_t*)(d + (y-1)*dp);
        const uint16_t* below = (const uint16_t*)(d + (y+1)*dp);
        for (int x = 1; x < dw-1; x++) {
            int r,g,b; Unpack565(dr[x],r,g,b);
            int ra,ga,ba,rb,gb,bb,rl,gl,bl,rr,gr,br;
            Unpack565(above[x],ra,ga,ba); Unpack565(below[x],rb,gb,bb);
            Unpack565(dr[x-1],rl,gl,bl); Unpack565(dr[x+1],rr,gr,br);
            float a = 0.5f;
            int br2=(ra+rb+rl+rr)/4, bg2=(ga+gb+gl+gr)/4, bb2=(ba+bb+bl+br)/4;
            dr[x] = Pack565((int)(r+a*(r-br2)), (int)(g+a*(g-bg2)), (int)(b+a*(b-bb2)));
        }
    }
}
static void SSharpen_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SBilinear_24(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 1; y < dh-1; y++) {
        uint8_t* dr = d + y*dp;
        const uint8_t* above = d + (y-1)*dp, *below = d + (y+1)*dp;
        for (int x = 1; x < dw-1; x++) {
            for (int c = 0; c < 3; c++) {
                int v = dr[x*3+c];
                int blur = (above[x*3+c]+below[x*3+c]+dr[(x-1)*3+c]+dr[(x+1)*3+c])/4;
                dr[x*3+c] = (uint8_t)Clamp((int)(v+0.5f*(v-blur)), 0, 255);
            }
        }
    }
}

// ============================================================================
// 10. Color dither (Bayer 4x4)
// ============================================================================
static const int Bayer4[4][4] = {
    { 0,  8,  2, 10}, {12,  4, 14,  6},
    { 3, 11,  1,  9}, {15,  7, 13,  5}
};
static void SColorDither_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        int sy = Clamp(y*sh/dh, 0, sh-1);
        uint16_t* dr = (uint16_t*)(d + y*dp);
        for (int x = 0; x < dw; x++) {
            int sx = Clamp(x*sw/dw, 0, sw-1);
            int r,g,b; Unpack565(S565(s,sp,sw,sh,sx,sy),r,g,b);
            int th = Bayer4[y%4][x%4];
            dr[x] = Pack565(r + (th > 8 ? 1 : 0), g + (th > 4 ? 1 : 0), b + (th > 8 ? 1 : 0));
        }
    }
}
static void SColorDither_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    for (int y = 0; y < dh; y++) {
        int sy = Clamp(y*sh/dh, 0, sh-1);
        uint8_t* dr = d + y*dp;
        for (int x = 0; x < dw; x++) {
            int sx = Clamp(x*sw/dw, 0, sw-1);
            int th = Bayer4[y%4][x%4];
            for (int c = 0; c < 3; c++)
                dr[x*3+c] = (uint8_t)Clamp(s[sy*sp+sx*3+c] + (th > 8 ? 1 : 0), 0, 255);
        }
    }
}

// ============================================================================
// 11. CRT scanline (bilinear + alternating row darkening, softer)
// ============================================================================
static void SCrtScanline_16(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SBilinear_16(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 0; y < dh; y++) {
        uint16_t* dr = (uint16_t*)(d + y*dp);
        float scanline = (y % 2 == 0) ? 0.85f : 0.65f;
        for (int x = 0; x < dw; x++) {
            int r,g,b; Unpack565(dr[x], r,g,b);
            dr[x] = Pack565((int)(r*scanline), (int)(g*scanline), (int)(b*scanline));
        }
    }
}
static void SCrtScanline_24(const uint8_t* s, int sw, int sh, int sp, uint8_t* d, int dw, int dh, int dp) {
    SBilinear_24(s, sw, sh, sp, d, dw, dh, dp);
    for (int y = 0; y < dh; y++) {
        uint8_t* dr = d + y*dp;
        float scanline = (y % 2 == 0) ? 0.85f : 0.65f;
        for (int x = 0; x < dw; x++) {
            dr[x*3]   = (uint8_t)(dr[x*3]   * scanline);
            dr[x*3+1] = (uint8_t)(dr[x*3+1] * scanline);
            dr[x*3+2] = (uint8_t)(dr[x*3+2] * scanline);
        }
    }
}

// ============================================================================
// Dispatcher
// ============================================================================
void ScaleFrame(const uint8_t* src, int sw, int sh, int sp, int bpp,
                uint8_t* dst, int dw, int dh, int dp, ScaleMode mode) {
    if (dw <= 0 || dh <= 0) return;

    typedef void (*SF16)(const uint8_t*, int, int, int, uint8_t*, int, int, int);
    typedef void (*SF24)(const uint8_t*, int, int, int, uint8_t*, int, int, int);

    static const SF16 fn16[] = {
        SNearest_16, SBilinear_16, SArea_16, SSharpenArea_16, SSharpenBilinear_16,
        SScanline_16, SSharpen_16, SColorDither_16, SCrtScanline_16
    };
    static const SF24 fn24[] = {
        SNearest_24, SBilinear_24, SArea_24, SSharpenArea_24, SSharpenBilinear_24,
        SScanline_24, SSharpen_24, SColorDither_24, SCrtScanline_24
    };

    int idx = (mode >= 0 && mode < SCALE_COUNT) ? mode : SCALE_BILINEAR;
    if (bpp == 2) fn16[idx](src, sw, sh, sp, dst, dw, dh, dp);
    else fn24[idx](src, sw, sh, sp, dst, dw, dh, dp);
}
