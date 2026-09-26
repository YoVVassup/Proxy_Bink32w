#pragma once
// ============================================================================
// mix_crypto.h — Blowfish + Westwood key derivation for encrypted .mix headers
//
// Ported from RA2YR_ReSource mix_blowfish.{hpp,cpp} (game-faithful).
// Layout (flags & 2):
//   sig(2)=0 | flags(2) | key_source(80) | Blowfish-ECB: count(2)+body_size(4)+index pad-to-8
// ============================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 18 P-words + 4x256 S-box words (~4.2 KB)
typedef struct MixBlowfishCtx {
    uint32_t p[18];
    uint32_t s[4][256];
} MixBlowfishCtx;

// key_source[80] -> key_out[56] (RSA / Westwood pubkey path)
void MixComputeBlowfishKey(const uint8_t* key_source, uint32_t key_source_len, uint8_t* key_out);

// Expand key into ctx (standard Blowfish key schedule)
void MixBlowfishInit(MixBlowfishCtx* ctx, const uint8_t* key, int cb_key);

// ECB encrypt/decrypt size bytes (must be multiple of 8), reverse32 word order
void MixBlowfishEncipher(const MixBlowfishCtx* ctx, void* buf, int size);
void MixBlowfishDecipher(const MixBlowfishCtx* ctx, void* buf, int size);

// Single 8-byte block (same reverse32 convention as multi-block)
void MixBlowfishEncipherBlock(const MixBlowfishCtx* ctx, void* block8);
void MixBlowfishDecipherBlock(const MixBlowfishCtx* ctx, void* block8);

#ifdef __cplusplus
}
#endif
