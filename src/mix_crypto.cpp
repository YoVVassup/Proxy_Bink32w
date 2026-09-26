// ============================================================================
// mix_crypto.cpp — Blowfish engine + Westwood ComputeBlowfishKey
// Ported from RA2YR_ReSource src/system/mix_blowfish.cpp (verified vs game).
// ============================================================================

#include "mix_crypto.h"
#include <string.h>
#include <windows.h>

// Logging lives in logging.cpp. Declared here so this module stays free of the
// rest of the proxy headers (it is a standalone port of the RA2YR Blowfish).
void LogF(const char* fmt, ...);

// ----------------------------------------------------------------------------
// P-array and S-boxes (standard Blowfish constants)
// ----------------------------------------------------------------------------

static const uint32_t bf_P0[18] = {
    0x243F6A88,0x85A308D3,0x13198A2E,0x03707344,0xA4093822,0x299F31D0,
    0x082EFA98,0xEC4E6C89,0x452821E6,0x38D01377,0xBE5466CF,0x34E90C6C,
    0xC0AC29B7,0xC97C50DD,0x3F84D5B5,0xB5470917,0x9216D5D9,0x8979FB1B
};

static const uint32_t bf_S0[4][256] = {
{
#include "mix_blowfish_s0.inl"
},{
#include "mix_blowfish_s1.inl"
},{
#include "mix_blowfish_s2.inl"
},{
#include "mix_blowfish_s3.inl"
}
};

// ----------------------------------------------------------------------------
// Blowfish primitives
// ----------------------------------------------------------------------------

static inline uint32_t reverse32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

static uint32_t BF_S(const MixBlowfishCtx* c, uint32_t x, int i) {
    return c->s[i][(x >> ((3 - i) << 3)) & 0xFF];
}

static uint32_t BF_F(const MixBlowfishCtx* c, uint32_t x) {
    return ((BF_S(c, x, 0) + BF_S(c, x, 1)) ^ BF_S(c, x, 2)) + BF_S(c, x, 3);
}

static void BF_Round(const MixBlowfishCtx* c, uint32_t* a, uint32_t b, int n) {
    *a ^= BF_F(c, b) ^ c->p[n];
}

static void BF_Encipher(const MixBlowfishCtx* c, uint32_t* xl, uint32_t* xr) {
    uint32_t Xl = *xl, Xr = *xr;
    Xl ^= c->p[0];
    BF_Round(c, &Xr, Xl, 1);  BF_Round(c, &Xl, Xr, 2);
    BF_Round(c, &Xr, Xl, 3);  BF_Round(c, &Xl, Xr, 4);
    BF_Round(c, &Xr, Xl, 5);  BF_Round(c, &Xl, Xr, 6);
    BF_Round(c, &Xr, Xl, 7);  BF_Round(c, &Xl, Xr, 8);
    BF_Round(c, &Xr, Xl, 9);  BF_Round(c, &Xl, Xr, 10);
    BF_Round(c, &Xr, Xl, 11); BF_Round(c, &Xl, Xr, 12);
    BF_Round(c, &Xr, Xl, 13); BF_Round(c, &Xl, Xr, 14);
    BF_Round(c, &Xr, Xl, 15); BF_Round(c, &Xl, Xr, 16);
    Xr ^= c->p[17];
    *xr = Xl; *xl = Xr;
}

static void BF_Decipher(const MixBlowfishCtx* c, uint32_t* xl, uint32_t* xr) {
    uint32_t Xl = *xl, Xr = *xr;
    Xl ^= c->p[17];
    BF_Round(c, &Xr, Xl, 16); BF_Round(c, &Xl, Xr, 15);
    BF_Round(c, &Xr, Xl, 14); BF_Round(c, &Xl, Xr, 13);
    BF_Round(c, &Xr, Xl, 12); BF_Round(c, &Xl, Xr, 11);
    BF_Round(c, &Xr, Xl, 10); BF_Round(c, &Xl, Xr, 9);
    BF_Round(c, &Xr, Xl, 8);  BF_Round(c, &Xl, Xr, 7);
    BF_Round(c, &Xr, Xl, 6);  BF_Round(c, &Xl, Xr, 5);
    BF_Round(c, &Xr, Xl, 4);  BF_Round(c, &Xl, Xr, 3);
    BF_Round(c, &Xr, Xl, 2);  BF_Round(c, &Xl, Xr, 1);
    Xr ^= c->p[0];
    *xl = Xr; *xr = Xl;
}

void MixBlowfishInit(MixBlowfishCtx* ctx, const uint8_t* key, int cb_key) {
    memcpy(ctx->p, bf_P0, sizeof(ctx->p));
    memcpy(ctx->s, bf_S0, sizeof(ctx->s));
    if (!key || cb_key <= 0) return;

    int j = 0;
    for (int i = 0; i < 18; ++i) {
        uint32_t d = 0;
        for (int k = 0; k < 4; ++k) {
            d = (d << 8) | key[j];
            j = (j + 1) % cb_key;
        }
        ctx->p[i] ^= d;
    }

    uint32_t dl = 0, dr = 0;
    for (int i = 0; i < 18; i += 2) {
        BF_Encipher(ctx, &dl, &dr);
        ctx->p[i] = dl;
        ctx->p[i + 1] = dr;
    }
    for (int i = 0; i < 4; ++i) {
        for (int jj = 0; jj < 256; jj += 2) {
            BF_Encipher(ctx, &dl, &dr);
            ctx->s[i][jj] = dl;
            ctx->s[i][jj + 1] = dr;
        }
    }
}

void MixBlowfishEncipherBlock(const MixBlowfishCtx* ctx, void* block8) {
    uint32_t* w = (uint32_t*)block8;
    uint32_t a = reverse32(w[0]);
    uint32_t b = reverse32(w[1]);
    BF_Encipher(ctx, &a, &b);
    w[0] = reverse32(a);
    w[1] = reverse32(b);
}

void MixBlowfishDecipherBlock(const MixBlowfishCtx* ctx, void* block8) {
    uint32_t* w = (uint32_t*)block8;
    uint32_t a = reverse32(w[0]);
    uint32_t b = reverse32(w[1]);
    BF_Decipher(ctx, &a, &b);
    w[0] = reverse32(a);
    w[1] = reverse32(b);
}

void MixBlowfishEncipher(const MixBlowfishCtx* ctx, void* buf, int size) {
    uint8_t* p = (uint8_t*)buf;
    for (int off = 0; off + 8 <= size; off += 8)
        MixBlowfishEncipherBlock(ctx, p + off);
}

void MixBlowfishDecipher(const MixBlowfishCtx* ctx, void* buf, int size) {
    uint8_t* p = (uint8_t*)buf;
    for (int off = 0; off + 8 <= size; off += 8)
        MixBlowfishDecipherBlock(ctx, p + off);
}

// ----------------------------------------------------------------------------
// Westwood key derivation (RSA modpow with embedded pubkey) — ported 1:1
// ----------------------------------------------------------------------------

namespace {

typedef uint32_t bignum4[4];
typedef uint32_t bignum[64];
typedef uint32_t bignum130[130];

const char* pubkey_str = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V";

const int8_t char2num[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

struct { bignum key1; bignum key2; uint32_t len; } pubkey;
bool pubkey_initialized = false;

bignum glob1;
uint32_t glob1_bitlen, glob1_len_x2;
bignum130 glob2;
bignum4 glob1_hi, glob1_hi_inv;
uint32_t glob1_hi_bitlen;
uint32_t glob1_hi_inv_lo, glob1_hi_inv_hi;

void init_bignum(bignum n, uint32_t val, uint32_t len) {
    memset(n, 0, len * 4);
    n[0] = val;
}

uint32_t len_bignum(bignum n, uint32_t len) {
    int i = (int)len - 1;
    while (i >= 0 && n[i] == 0) i--;
    return (uint32_t)(i + 1);
}

uint32_t bitlen_bignum(bignum n, uint32_t len) {
    uint32_t ddlen = len_bignum(n, len);
    if (ddlen == 0) return 0;
    uint32_t bitlen = ddlen * 32;
    uint32_t mask = 0x80000000;
    while ((mask & n[ddlen - 1]) == 0) { mask >>= 1; bitlen--; }
    return bitlen;
}

void mov_bignum(bignum dest, bignum src, uint32_t len) {
    memmove(dest, src, len * 4);
}

void shr_bignum(bignum n, uint32_t bits, int32_t len) {
    uint32_t i2 = bits / 32;
    if (i2 > 0) {
        for (uint32_t i = 0; i < (uint32_t)(len - (int32_t)i2); i++) n[i] = n[i + i2];
        for (uint32_t i = (uint32_t)(len - (int32_t)i2); i < (uint32_t)len; i++) n[i] = 0;
        bits = bits % 32;
    }
    if (bits == 0) return;
    for (uint32_t i = 0; i < (uint32_t)(len - 1); i++)
        n[i] = (n[i] >> bits) | (n[i + 1] << (32 - bits));
    n[(uint32_t)(len - 1)] >>= bits;
}

void shl_bignum(bignum n, uint32_t bits, uint32_t len) {
    uint32_t i2 = bits / 32;
    if (i2 > 0) {
        for (int32_t i = (int32_t)len - 1; i > (int32_t)i2; i--) n[i] = n[i - i2];
        for (int32_t i = (int32_t)i2; i > 0; i--) n[i] = 0;
        bits = bits % 32;
    }
    if (bits == 0) return;
    for (int32_t i = (int32_t)len - 1; i > 0; i--)
        n[i] = (n[i] << bits) | (n[i - 1] >> (32 - bits));
    n[0] <<= bits;
}

int cmp_bignum(bignum n1, bignum n2, uint32_t len) {
    n1 += len - 1;
    n2 += len - 1;
    while (len > 0) {
        if (*n1 < *n2) return -1;
        if (*n1 > *n2) return 1;
        n1--; n2--; len--;
    }
    return 0;
}

uint32_t sub_bignum(bignum dest, bignum src1, bignum src2, uint32_t carry, uint32_t len) {
    len += len;
    while (--len != 0xFFFFFFFF) {
        uint32_t i1 = *(uint16_t*)src1;
        uint32_t i2 = *(uint16_t*)src2;
        *(uint16_t*)dest = (uint16_t)(i1 - i2 - carry);
        src1 = (uint32_t*)((uint16_t*)src1 + 1);
        src2 = (uint32_t*)((uint16_t*)src2 + 1);
        dest = (uint32_t*)((uint16_t*)dest + 1);
        if ((i1 - i2 - carry) & 0x10000) carry = 1; else carry = 0;
    }
    return carry;
}

void inc_bignum(bignum n, uint32_t len) {
    while ((++*n == 0) && (--len > 0)) n++;
}

void dec_bignum(bignum n, uint32_t len) {
    while ((--*n == 0xFFFFFFFF) && (--len > 0)) n++;
}

void not_bignum(bignum n, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) n[i] = ~n[i];
}

void neg_bignum(bignum n, uint32_t len) {
    not_bignum(n, len);
    inc_bignum(n, len);
}

void inv_bignum(bignum n1, bignum n2, uint32_t len) {
    bignum n_tmp;
    init_bignum(n_tmp, 0, len);
    init_bignum(n1, 0, len);
    int32_t n2_bitlen = (int32_t)bitlen_bignum(n2, len);
    uint32_t bit = ((uint32_t)1) << (n2_bitlen % 32);
    n1 += ((n2_bitlen + 32) / 32) - 1;
    uint32_t n2_bytelen = ((n2_bitlen - 1) / 32) * 4;
    n_tmp[n2_bytelen / 4] |= ((uint32_t)1) << ((n2_bitlen - 1) & 0x1F);
    while (n2_bitlen > 0) {
        n2_bitlen--;
        shl_bignum(n_tmp, 1, len);
        if (cmp_bignum(n_tmp, n2, len) != -1) {
            sub_bignum(n_tmp, n_tmp, n2, 0, len);
            *n1 |= bit;
        }
        bit >>= 1;
        if (bit == 0) { n1--; bit = 0x80000000; }
    }
    init_bignum(n_tmp, 0, len);
}

void mul_bignum_word(bignum n1, bignum n2, uint32_t mul, uint32_t len) {
    uint32_t tmp = 0;
    for (uint32_t i = 0; i < len; i++) {
        tmp = mul * *(uint16_t*)n2 + *(uint16_t*)n1 + tmp;
        *(uint16_t*)n1 = (uint16_t)tmp;
        n1 = (uint32_t*)((uint16_t*)n1 + 1);
        n2 = (uint32_t*)((uint16_t*)n2 + 1);
        tmp >>= 16;
    }
    *(uint16_t*)n1 += (uint16_t)tmp;
}

void mul_bignum(bignum dest, bignum src1, bignum src2, uint32_t len) {
    init_bignum(dest, 0, len * 2);
    for (uint32_t i = 0; i < len * 2; i++) {
        mul_bignum_word(dest, src1, *(uint16_t*)src2, len * 2);
        src2 = (uint32_t*)((uint16_t*)src2 + 1);
        dest = (uint32_t*)((uint16_t*)dest + 1);
    }
}

void init_two_dw(bignum n, uint32_t len) {
    mov_bignum(glob1, n, len);
    glob1_bitlen = bitlen_bignum(glob1, len);
    glob1_len_x2 = (glob1_bitlen + 15) / 16;
    mov_bignum(glob1_hi, glob1 + len_bignum(glob1, len) - 2, 2);
    glob1_hi_bitlen = bitlen_bignum(glob1_hi, 2) - 32;
    shr_bignum(glob1_hi, glob1_hi_bitlen, 2);
    inv_bignum(glob1_hi_inv, glob1_hi, 2);
    shr_bignum(glob1_hi_inv, 1, 2);
    glob1_hi_bitlen = (glob1_hi_bitlen + 15) % 16 + 1;
    inc_bignum(glob1_hi_inv, 2);
    if (bitlen_bignum(glob1_hi_inv, 2) > 32) {
        shr_bignum(glob1_hi_inv, 1, 2);
        glob1_hi_bitlen--;
    }
    glob1_hi_inv_lo = *(uint16_t*)glob1_hi_inv;
    glob1_hi_inv_hi = *((uint16_t*)glob1_hi_inv + 1);
}

uint32_t get_mulword(bignum n) {
    uint16_t* wn = (uint16_t*)n;
    uint32_t i = (((((((((*(wn-1) ^ 0xFFFF) & 0xFFFF) * glob1_hi_inv_lo + 0x10000) >> 1)
        + (((*(wn-2) ^ 0xFFFF) * glob1_hi_inv_hi + glob1_hi_inv_hi) >> 1) + 1)
        >> 16) + ((((*(wn-1) ^ 0xFFFF) & 0xFFFF) * glob1_hi_inv_hi) >> 1) +
        (((*wn ^ 0xFFFF) * glob1_hi_inv_lo) >> 1) + 1) >> 14) + glob1_hi_inv_hi
        * (*wn ^ 0xFFFF) * 2) >> glob1_hi_bitlen;
    if (i > 0xFFFF) i = 0xFFFF;
    return i & 0xFFFF;
}

void calc_a_bignum(bignum n1, bignum n2, bignum n3, uint32_t len) {
    mul_bignum(glob2, n2, n3, len);
    glob2[len * 2] = 0;
    uint32_t g2_len_x2 = len_bignum(glob2, len * 2 + 1) * 2;
    if (g2_len_x2 >= glob1_len_x2) {
        inc_bignum(glob2, len * 2 + 1);
        neg_bignum(glob2, len * 2 + 1);
        uint32_t len_diff = g2_len_x2 + 1 - glob1_len_x2;
        uint16_t* esi = ((uint16_t*)glob2) + (1 + g2_len_x2 - glob1_len_x2);
        uint16_t* edi = ((uint16_t*)glob2) + (g2_len_x2 + 1);
        for (; len_diff != 0; len_diff--) {
            edi--;
            uint32_t tmp = get_mulword((uint32_t*)edi);
            esi--;
            if (tmp > 0) {
                mul_bignum_word((uint32_t*)esi, glob1, tmp, 2 * len);
                if ((*edi & 0x8000) == 0) {
                    if (sub_bignum((uint32_t*)esi, (uint32_t*)esi, glob1, 0, len)) (*edi)--;
                }
            }
        }
        neg_bignum(glob2, len);
        dec_bignum(glob2, len);
    }
    mov_bignum(n1, glob2, len);
}

void clear_tmp_vars(uint32_t len) {
    init_bignum(glob1, 0, len);
    init_bignum(glob2, 0, len * 2 + 1);
    init_bignum(glob1_hi_inv, 0, 4);
    init_bignum(glob1_hi, 0, 4);
    glob1_bitlen = 0;
    glob1_hi_bitlen = 0;
    glob1_len_x2 = 0;
    glob1_hi_inv_lo = 0;
    glob1_hi_inv_hi = 0;
}

void move_key_to_big(bignum n, const uint8_t* key, uint32_t klen, uint32_t blen) {
    uint32_t sign = (key[0] & 0x80) ? 0xFF : 0;
    uint32_t i;
    for (i = blen * 4; i > klen; i--)
        ((uint8_t*)n)[i - 1] = (uint8_t)sign;
    for (; i > 0; i--)
        ((uint8_t*)n)[i - 1] = key[klen - i];
}

void key_to_bignum(bignum n, const uint8_t* key, uint32_t len) {
    if (key[0] != 2) return;
    key++;
    uint32_t keylen;
    if (key[0] & 0x80) {
        keylen = 0;
        for (uint32_t i = 0; i < (uint32_t)(key[0] & 0x7F); i++)
            keylen = (keylen << 8) | key[i + 1];
        key += (key[0] & 0x7F) + 1;
    } else {
        keylen = key[0];
        key++;
    }
    if (keylen <= len * 4)
        move_key_to_big(n, key, keylen, len);
}

void init_pubkey() {
    init_bignum(pubkey.key2, 0x10001, 64);
    uint8_t keytmp[256];
    uint32_t i = 0, i2 = 0;
    while (i < strlen(pubkey_str)) {
        uint32_t tmp = (uint32_t)char2num[(uint8_t)pubkey_str[i++]];
        tmp <<= 6; tmp |= (uint32_t)char2num[(uint8_t)pubkey_str[i++]];
        tmp <<= 6; tmp |= (uint32_t)char2num[(uint8_t)pubkey_str[i++]];
        tmp <<= 6; tmp |= (uint32_t)char2num[(uint8_t)pubkey_str[i++]];
        keytmp[i2++] = (uint8_t)((tmp >> 16) & 0xFF);
        keytmp[i2++] = (uint8_t)((tmp >> 8) & 0xFF);
        keytmp[i2++] = (uint8_t)(tmp & 0xFF);
    }
    key_to_bignum(pubkey.key1, keytmp, 64);
    pubkey.len = bitlen_bignum(pubkey.key1, 64) - 1;
}

uint32_t len_predata() {
    uint32_t a = (pubkey.len - 1) / 8;
    return (55 / a + 1) * (a + 1);
}

void calc_a_key(bignum n1, bignum n2, bignum n3, bignum n4, uint32_t len) {
    bignum n_tmp;
    uint32_t n4_len = len_bignum(n4, len);
    init_bignum(n1, 1, len);
    init_two_dw(n4, n4_len);
    uint32_t n3_bitlen = bitlen_bignum(n3, n4_len);
    uint32_t n3_len = (n3_bitlen + 31) / 32;
    uint32_t bit_mask = (((uint32_t)1) << ((n3_bitlen - 1) % 32)) >> 1;
    n3 += n3_len - 1;
    n3_bitlen--;
    mov_bignum(n1, n2, n4_len);
    while (--n3_bitlen != 0xFFFFFFFF) {
        if (bit_mask == 0) { bit_mask = 0x80000000; n3--; }
        calc_a_bignum(n_tmp, n1, n1, n4_len);
        if (*n3 & bit_mask)
            calc_a_bignum(n1, n_tmp, n2, n4_len);
        else
            mov_bignum(n1, n_tmp, n4_len);
        bit_mask >>= 1;
    }
    init_bignum(n_tmp, 0, n4_len);
    clear_tmp_vars(len);
}

void process_predata(const uint8_t* pre, uint32_t pre_len, uint8_t* buf) {
    bignum n2, n3;
    const uint32_t a = (pubkey.len - 1) / 8;
    while (a + 1 <= pre_len) {
        init_bignum(n2, 0, 64);
        memmove(n2, pre, a + 1);
        calc_a_key(n3, n2, pubkey.key2, pubkey.key1, 64);
        memmove(buf, n3, a);
        pre_len -= a + 1;
        pre += a + 1;
        buf += a;
    }
}

} // namespace

static LONG g_cryptoCsOnce = 0;
static CRITICAL_SECTION g_cryptoCs;

void MixComputeBlowfishKey(const uint8_t* key_source, uint32_t key_source_len, uint8_t* key_out) {
    // pubkey + glob1/glob2 scratch bignums are shared mutable state.
    if (InterlockedCompareExchange(&g_cryptoCsOnce, 1, 0) == 0) {
        InitializeCriticalSection(&g_cryptoCs);
        InterlockedExchange(&g_cryptoCsOnce, 2);
    } else {
        while (InterlockedCompareExchange(&g_cryptoCsOnce, 2, 2) != 2) { SwitchToThread(); }
    }
    EnterCriticalSection(&g_cryptoCs);
    if (!pubkey_initialized) {
        init_pubkey();
        pubkey_initialized = true;
    }
    uint8_t key[256];
    memset(key, 0, sizeof(key));
    // len_predata() follows the built-in pubkey, not the caller's buffer.
    // With the current key both are 80, but a swapped-in pubkey would make
    // process_predata() read past the end of an 80-byte key_source.
    uint32_t pre = len_predata();
    if (pre > key_source_len) {
        LogF("MixComputeBlowfishKey: key_source too small (%u bytes, need %u); "
             "derived key will not match", key_source_len, pre);
        pre = key_source_len;
    }
    process_predata(key_source, pre, key);
    memcpy(key_out, key, 56);
    LeaveCriticalSection(&g_cryptoCs);
}
