/* hash_spike.c  —  Hash function comparison spike for picocompress
 *
 * Compares 6 candidate 3-byte hash functions on:
 *   1. Distribution quality  (chi-squared, fill rate, max depth)
 *   2. Raw speed             (million hashes / second)
 *   3. Compression ratio     (full compress + decompress roundtrip)
 *
 * Build (MSVC, from picocompress/src):
 *   cl /O2 /W0 /TC hash_spike.c picocompress.c /Fe:hash_spike.exe
 *
 * Test payloads are read from ../tests/
 */

#include "picocompress.h"

/* PC_HASH_SIZE is internal to picocompress.c — redefine here */
#ifndef PC_HASH_SIZE
#define PC_HASH_SIZE (1u << PC_HASH_BITS)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ====================================================================
 * 1.  Hash function candidates
 * ==================================================================== */

typedef uint16_t (*hash_fn_t)(const uint8_t *p, unsigned mask);

/* A. Current: a*251 + b*11 + c*3 */
static uint16_t hash_A_current(const uint8_t *p, unsigned mask) {
    uint32_t v = (uint32_t)p[0] * 251u
               + (uint32_t)p[1] * 11u
               + (uint32_t)p[2] * 3u;
    return (uint16_t)(v & mask);
}

/* B. FNV-1a style */
static uint16_t hash_B_fnv1a(const uint8_t *p, unsigned mask) {
    uint32_t h = 2166136261u;
    h = (h ^ p[0]) * 16777619u;
    h = (h ^ p[1]) * 16777619u;
    h = (h ^ p[2]) * 16777619u;
    return (uint16_t)(h & mask);
}

/* C. Shift-XOR: (a<<5) ^ b ^ (c<<2) */
static uint16_t hash_C_shift_xor(const uint8_t *p, unsigned mask) {
    uint32_t v = ((uint32_t)p[0] << 5)
               ^  (uint32_t)p[1]
               ^ ((uint32_t)p[2] << 2);
    return (uint16_t)(v & mask);
}

/* D. Multiplicative: (a*31) ^ b ^ (c<<3) */
static uint16_t hash_D_mult(const uint8_t *p, unsigned mask) {
    uint32_t v = ((uint32_t)p[0] * 31u)
               ^  (uint32_t)p[1]
               ^ ((uint32_t)p[2] << 3);
    return (uint16_t)(v & mask);
}

/* E. DJB2 variant: ((a<<5)+a) ^ b ^ ((c<<5)+c) */
static uint16_t hash_E_djb2(const uint8_t *p, unsigned mask) {
    uint32_t v = (((uint32_t)p[0] << 5) + p[0])
               ^  (uint32_t)p[1]
               ^ (((uint32_t)p[2] << 5) + p[2]);
    return (uint16_t)(v & mask);
}

/* F. MurmurHash3-inspired finalizer on 3 bytes packed into uint32 */
static uint16_t hash_F_murmur3(const uint8_t *p, unsigned mask) {
    uint32_t k = (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16);
    k *= 0xcc9e2d51u;
    k  = (k << 15) | (k >> 17);
    k *= 0x1b873593u;
    k ^= k >> 16;
    k *= 0x85ebca6bu;
    k ^= k >> 13;
    return (uint16_t)(k & mask);
}

#define NUM_HASHES 6

static const char *hash_names[NUM_HASHES] = {
    "A:Current",  "B:FNV-1a",   "C:ShiftXOR",
    "D:Mult",     "E:DJB2",     "F:Murmur3",
};

static hash_fn_t hash_fns[NUM_HASHES] = {
    hash_A_current, hash_B_fnv1a,  hash_C_shift_xor,
    hash_D_mult,    hash_E_djb2,   hash_F_murmur3,
};

/* ====================================================================
 * 2.  Distribution analysis
 * ==================================================================== */

typedef struct {
    double   chi_squared;
    double   norm_chi;       /* chi_sq / (buckets-1),  ~1.0 = ideal */
    double   fill_pct;       /* % of buckets occupied */
    unsigned max_depth;
    double   collision_pct;  /* % of insertions that hit an occupied bucket */
} dist_stats_t;

static dist_stats_t measure_distribution(
    hash_fn_t hash, const uint8_t *data, size_t len, unsigned hash_bits)
{
    unsigned hash_size = 1u << hash_bits;
    unsigned mask      = hash_size - 1u;
    unsigned *buckets;
    dist_stats_t st;
    size_t i, n;
    unsigned j, used = 0;
    double expected, chi = 0.0;

    memset(&st, 0, sizeof(st));
    if (len < 3) return st;

    buckets = (unsigned *)calloc(hash_size, sizeof(unsigned));
    if (!buckets) return st;

    n = len - 2;
    for (i = 0; i < n; i++)
        buckets[hash(data + i, mask)]++;

    for (j = 0; j < hash_size; j++) {
        if (buckets[j] > 0)         used++;
        if (buckets[j] > st.max_depth) st.max_depth = buckets[j];
    }

    expected = (double)n / (double)hash_size;
    for (j = 0; j < hash_size; j++) {
        double d = (double)buckets[j] - expected;
        chi += (d * d) / expected;
    }

    st.chi_squared   = chi;
    st.norm_chi      = chi / (double)(hash_size - 1);
    st.fill_pct      = (double)used / (double)hash_size * 100.0;
    st.collision_pct  = n > 0 ? (double)(n - used) / (double)n * 100.0 : 0.0;

    free(buckets);
    return st;
}

/* ====================================================================
 * 3.  Speed benchmark
 * ==================================================================== */

static double benchmark_mhps(
    hash_fn_t hash, const uint8_t *data, size_t len,
    unsigned mask, unsigned iters)
{
    LARGE_INTEGER freq, t0, t1;
    volatile uint32_t sink = 0;
    size_t n, total;
    unsigned it;
    double sec;

    if (len < 3) return 0.0;
    n = len - 2;

    /* warmup */
    {
        size_t i;
        for (i = 0; i < n; i++) sink += hash(data + i, mask);
    }

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    for (it = 0; it < iters; it++) {
        size_t i;
        for (i = 0; i < n; i++)
            sink += hash(data + i, mask);
    }

    QueryPerformanceCounter(&t1);
    (void)sink;

    sec   = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    total = n * iters;
    return sec > 0.0 ? (double)total / sec / 1e6 : 0.0;
}

/* ====================================================================
 * 4.  Compression pipeline replica with pluggable hash
 *
 *     Identical token format to picocompress.c — the ONLY change is
 *     which hash function feeds the match-finder hash table.
 *     Decompression uses the public pc_decompress_buffer() for
 *     roundtrip verification.
 * ==================================================================== */

/* --- global hash selector --- */
static hash_fn_t  g_hash      = NULL;
static unsigned    g_hash_mask = 0;

static uint16_t hs_hash3(const uint8_t *p) {
    return g_hash(p, g_hash_mask);
}

/* --- internal constants (match picocompress.c) --- */
#define HS_HASH_SIZE     PC_HASH_SIZE
#define HS_GOOD_MATCH    8u
#define HS_REPEAT_CACHE  3u

/* --- static dictionary (exact copy from picocompress.c) --- */
typedef struct { const uint8_t *data; uint8_t len; } hs_de_t;

static const uint8_t hd00[]={'{',0};
static const uint8_t hd01[]={'}',0};
static const uint8_t hd02[]={':',0};
static const uint8_t hd03[]={',',0};
static const uint8_t hd04[]={'\r','\n'};
static const uint8_t hd05[]={'i','d',0};
static const uint8_t hd06[]={'"',':'};
static const uint8_t hd07[]={',','"'};
static const uint8_t hd08[]={'"',':','"'};
static const uint8_t hd09[]={'0','0','0'};
static const uint8_t hd10[]={'O','R','D'};
static const uint8_t hd11[]={'t','h','e'};
static const uint8_t hd12[]={'i','n','g'};
static const uint8_t hd13[]={',','"',','};
static const uint8_t hd14[]={'"',':','{'};
static const uint8_t hd15[]={'"',':','['};
static const uint8_t hd16[]={'i','o','n'};
static const uint8_t hd17[]={'e','n','t'};
static const uint8_t hd18[]={'t','e','r'};
static const uint8_t hd19[]={'a','n','d'};
static const uint8_t hd20[]={'0','0'};
static const uint8_t hd21[]={'"','}',','};
static const uint8_t hd22[]={'"',']',','};
static const uint8_t hd23[]={' ',' '};
static const uint8_t hd24[]={'n','o','"',':'};
static const uint8_t hd25[]={'t','r','u','e'};
static const uint8_t hd26[]={'n','u','l','l'};
static const uint8_t hd27[]={'n','a','m','e'};
static const uint8_t hd28[]={'d','a','t','a'};
static const uint8_t hd29[]={'t','i','m','e'};
static const uint8_t hd30[]={'t','y','p','e'};
static const uint8_t hd31[]={'m','o','d','e'};
static const uint8_t hd32[]={'h','t','t','p'};
static const uint8_t hd33[]={'t','i','o','n'};
static const uint8_t hd34[]={'c','o','d','e'};
static const uint8_t hd35[]={'s','i','z','e'};
static const uint8_t hd36[]={'m','e','n','t'};
static const uint8_t hd37[]={'l','i','s','t'};
static const uint8_t hd38[]={'i','t','e','m'};
static const uint8_t hd39[]={'t','e','x','t'};
static const uint8_t hd40[]={'f','a','l','s','e'};
static const uint8_t hd41[]={'e','r','r','o','r'};
static const uint8_t hd42[]={'v','a','l','u','e'};
static const uint8_t hd43[]={'s','t','a','t','e'};
static const uint8_t hd44[]={'a','l','e','r','t'};
static const uint8_t hd45[]={'i','n','p','u','t'};
static const uint8_t hd46[]={'a','t','i','o','n'};
static const uint8_t hd47[]={'o','r','d','e','r'};
static const uint8_t hd48[]={'s','t','a','t','u','s'};
static const uint8_t hd49[]={'n','u','m','b','e','r'};
static const uint8_t hd50[]={'a','c','t','i','v','e'};
static const uint8_t hd51[]={'d','e','v','i','c','e'};
static const uint8_t hd52[]={'r','e','g','i','o','n'};
static const uint8_t hd53[]={'s','t','r','i','n','g'};
static const uint8_t hd54[]={'r','e','s','u','l','t'};
static const uint8_t hd55[]={'l','e','n','g','t','h'};
static const uint8_t hd56[]={'m','e','s','s','a','g','e'};
static const uint8_t hd57[]={'c','o','n','t','e','n','t'};
static const uint8_t hd58[]={'r','e','q','u','e','s','t'};
static const uint8_t hd59[]={'d','e','f','a','u','l','t'};
static const uint8_t hd60[]={'n','u','m','b','e','r','"',':'};
static const uint8_t hd61[]={'o','p','e','r','a','t','o','r'};
static const uint8_t hd62[]={'h','t','t','p','s',':','/','/'}; 
static const uint8_t hd63[]={'r','e','s','p','o','n','s','e'};

static const hs_de_t hs_dict[PC_DICT_COUNT] = {
    {hd00,1},{hd01,1},{hd02,1},{hd03,1},
    {hd04,2},{hd05,2},{hd06,2},{hd07,2},
    {hd08,3},{hd09,3},{hd10,3},{hd11,3},
    {hd12,3},{hd13,3},{hd14,3},{hd15,3},
    {hd16,3},{hd17,3},{hd18,3},{hd19,3},
    {hd20,2},{hd21,3},{hd22,3},{hd23,2},
    {hd24,4},{hd25,4},{hd26,4},{hd27,4},
    {hd28,4},{hd29,4},{hd30,4},{hd31,4},
    {hd32,4},{hd33,4},{hd34,4},{hd35,4},
    {hd36,4},{hd37,4},{hd38,4},{hd39,4},
    {hd40,5},{hd41,5},{hd42,5},{hd43,5},
    {hd44,5},{hd45,5},{hd46,5},{hd47,5},
    {hd48,6},{hd49,6},{hd50,6},{hd51,6},
    {hd52,6},{hd53,6},{hd54,6},{hd55,6},
    {hd56,7},{hd57,7},{hd58,7},{hd59,7},
    {hd60,8},{hd61,8},{hd62,8},{hd63,8},
};

/* --- helper functions (mirror picocompress.c internals) --- */

static uint16_t hs_match_len(const uint8_t *a, const uint8_t *b, uint16_t lim) {
    uint16_t m = 0;
    while (m < lim && a[m] == b[m]) ++m;
    return m;
}

static int hs_emit_lit(const uint8_t *src, uint16_t slen,
                       uint8_t *dst, uint16_t dcap, uint16_t *op) {
    uint16_t pos = 0;
    while (pos < slen) {
        uint16_t ch = (uint16_t)(slen - pos);
        if (ch > PC_LITERAL_EXT_MAX) ch = PC_LITERAL_EXT_MAX;
        if ((uint32_t)(*op) + 1u + ch > dcap) return 0;
        if (ch <= PC_LITERAL_MAX)
            dst[(*op)++] = (uint8_t)(ch - 1u);
        else
            dst[(*op)++] = (uint8_t)(0xE0u | (ch - 65u));
        memcpy(dst + *op, src + pos, ch);
        *op = (uint16_t)(*op + ch);
        pos = (uint16_t)(pos + ch);
    }
    return 1;
}

static void hs_head_ins(int16_t head[PC_HASH_CHAIN_DEPTH][HS_HASH_SIZE],
                        uint16_t h, int16_t pos) {
    int d;
    for (d = (int)PC_HASH_CHAIN_DEPTH - 1; d > 0; --d)
        head[d][h] = head[d - 1][h];
    head[0][h] = pos;
}

/* --- match finder --- */

static int hs_find_best(
    const uint8_t *vb, uint16_t vlen, uint16_t vpos,
    int16_t head[PC_HASH_CHAIN_DEPTH][HS_HASH_SIZE],
    const uint16_t rep[HS_REPEAT_CACHE],
    uint16_t *ol, uint16_t *oo, uint16_t *od, int *oir)
{
    int best = 0;
    uint16_t rem = (uint16_t)(vlen - vpos);
    int d;

    *ol = 0; *oo = 0; *od = UINT16_MAX; *oir = 0;

    /* 1. repeat-offset cache */
    if (rem >= PC_MATCH_MIN) {
        uint16_t mr = rem > PC_MATCH_MAX ? PC_MATCH_MAX : rem;
        for (d = 0; d < (int)HS_REPEAT_CACHE; ++d) {
            uint16_t off = rep[d], len;
            int is_r, tc, s;
            if (off == 0u || off > vpos) continue;
            if (vb[vpos] != vb[vpos - off]) continue;
            if (rem >= 2u && vb[vpos+1] != vb[vpos-off+1]) continue;
            len = hs_match_len(vb+vpos-off, vb+vpos, mr);
            if (len < PC_MATCH_MIN) continue;
            is_r = (d == 0) ? 1 : 0;
            tc = is_r ? 1 : (off <= PC_OFFSET_SHORT_MAX ? 2 : 3);
            s = (int)len - tc;
            if (s > best) {
                best = s; *ol = len; *oo = off;
                *od = UINT16_MAX; *oir = is_r;
                if (len >= HS_GOOD_MATCH) return best;
            }
        }
    }

    /* 2. dictionary */
    {
        uint8_t fb = vb[vpos];
        for (d = 0; d < (int)PC_DICT_COUNT; ++d) {
            uint8_t dl = hs_dict[d].len;
            int s;
            if (dl > rem) continue;
            if ((int)dl - 1 <= best) continue;
            if (hs_dict[d].data[0] != fb) continue;
            if (memcmp(vb+vpos, hs_dict[d].data, dl) != 0) continue;
            s = (int)dl - 1;
            best = s; *od = (uint16_t)d; *ol = dl; *oo = 0; *oir = 0;
            if (dl >= HS_GOOD_MATCH) return best;
        }
    }

    /* 3. LZ hash-chain — the part affected by hash quality */
    if (rem >= 3u) {
        uint16_t h = hs_hash3(vb + vpos);
        uint16_t mls = rem > PC_MATCH_MAX      ? PC_MATCH_MAX      : rem;
        uint16_t mll = rem > PC_LONG_MATCH_MAX  ? PC_LONG_MATCH_MAX  : rem;
        uint8_t  fb  = vb[vpos];

        for (d = 0; d < (int)PC_HASH_CHAIN_DEPTH; ++d) {
            int16_t prev = head[d][h];
            uint16_t pp, off, len, ml;
            int s, tc;
            if (prev < 0) continue;
            pp = (uint16_t)prev;
            if (pp >= vpos) continue;
            off = (uint16_t)(vpos - pp);
            if (off == 0u || off > PC_OFFSET_LONG_MAX) continue;
            if (vb[pp] != fb) continue;
            ml  = (off <= PC_OFFSET_SHORT_MAX) ? mls : mll;
            len = hs_match_len(vb+pp, vb+vpos, ml);
            if (len < PC_MATCH_MIN) continue;
            tc = (off <= PC_OFFSET_SHORT_MAX) ? 2 : 3;
            s  = (int)len - tc;
            if (s > best
                || (s == best && len > *ol)
                || (s == best && len == *ol && off < *oo)) {
                best = s; *ol = len; *oo = off;
                *od = UINT16_MAX; *oir = 0;
                if (len >= HS_GOOD_MATCH) return best;
            }
        }
    }
    return best;
}

/* --- block compressor --- */

static uint16_t hs_compress_block(
    const uint8_t *vb, uint16_t hl, uint16_t blen,
    uint8_t *out, uint16_t ocap)
{
    int16_t  head[PC_HASH_CHAIN_DEPTH][HS_HASH_SIZE];
    uint16_t rep[HS_REPEAT_CACHE] = {0, 0, 0};
    uint16_t vlen = (uint16_t)(hl + blen);
    uint16_t vpos, anchor, op = 0;

    memset(head, 0xFF, sizeof(head));

    if (hl >= 3u) {
        uint16_t p;
        for (p = 0; (uint16_t)(p + 2u) < hl; ++p)
            hs_head_ins(head, hs_hash3(vb + p), (int16_t)p);
        {
            uint16_t ts = hl > 64u ? (uint16_t)(hl - 64u) : 0u;
            for (p = ts; (uint16_t)(p + 2u) < hl; ++p) {
                uint16_t h = hs_hash3(vb + p);
                if (head[0][h] != (int16_t)p) {
                    int16_t sv = head[PC_HASH_CHAIN_DEPTH - 1u][h];
                    hs_head_ins(head, h, (int16_t)p);
                    head[PC_HASH_CHAIN_DEPTH - 1u][h] = sv;
                }
            }
        }
    }

    anchor = hl;
    vpos   = hl;
    while (vpos < vlen) {
        uint16_t bl2, bo, bd;
        int bir, bs;
retry:
        bl2 = 0; bo = 0; bd = UINT16_MAX; bir = 0;
        if ((uint16_t)(vlen - vpos) < PC_MATCH_MIN) break;

        bs = hs_find_best(vb, vlen, vpos, head, rep, &bl2, &bo, &bd, &bir);

        if ((uint16_t)(vlen - vpos) >= 3u)
            hs_head_ins(head, hs_hash3(vb + vpos), (int16_t)vpos);

        if (bd != UINT16_MAX && bs == 0 && anchor == vpos)
            bs = 1;
        if (bs <= 1 && bd == UINT16_MAX && anchor < vpos)
            bs = 0;

        /* lazy matching */
        if (bs > 0 && bl2 < HS_GOOD_MATCH) {
            uint16_t step;
            for (step = 1; step <= (uint16_t)PC_LAZY_STEPS; ++step) {
                uint16_t np = (uint16_t)(vpos + step);
                uint16_t nl, no2, nd; int nr, ns;
                if (np >= vlen || (uint16_t)(vlen - np) < PC_MATCH_MIN) break;
                ns = hs_find_best(vb, vlen, np, head, rep, &nl, &no2, &nd, &nr);
                if (ns > bs) {
                    uint16_t s;
                    for (s = 0; s < step; ++s) {
                        uint16_t sp = (uint16_t)(vpos + s);
                        if ((uint16_t)(vlen - sp) >= 3u)
                            hs_head_ins(head, hs_hash3(vb + sp), (int16_t)sp);
                    }
                    vpos = np;
                    goto retry;
                }
            }
        }

        if (bs > 0) {
            uint16_t ll = (uint16_t)(vpos - anchor);
            uint16_t k;
            if (!hs_emit_lit(vb + anchor, ll, out, ocap, &op)) return UINT16_MAX;

            if (bd != UINT16_MAX) {
                if ((uint32_t)op + 1u > ocap) return UINT16_MAX;
                out[op++] = (uint8_t)(0x40u | (bd & 0x3Fu));
            } else if (bir) {
                if ((uint32_t)op + 1u > ocap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xC0u | ((bl2 - PC_MATCH_MIN) & 0x1Fu));
            } else if (bo <= PC_OFFSET_SHORT_MAX && bl2 <= PC_MATCH_MAX) {
                if ((uint32_t)op + 2u > ocap) return UINT16_MAX;
                out[op++] = (uint8_t)(0x80u
                    | (((bl2 - PC_MATCH_MIN) & 0x1Fu) << 1u)
                    | ((bo >> 8u) & 0x01u));
                out[op++] = (uint8_t)(bo & 0xFFu);
            } else {
                uint16_t el = bl2 > PC_LONG_MATCH_MAX ? PC_LONG_MATCH_MAX : bl2;
                if ((uint32_t)op + 3u > ocap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xF0u | ((el - PC_LONG_MATCH_MIN) & 0x0Fu));
                out[op++] = (uint8_t)((bo >> 8u) & 0xFFu);
                out[op++] = (uint8_t)(bo & 0xFFu);
                bl2 = el;
            }

            if (!bir && bo != 0u && bd == UINT16_MAX) {
                rep[2] = rep[1]; rep[1] = rep[0]; rep[0] = bo;
            }
            for (k = 1; k < bl2 && (uint16_t)(vpos+k+2u) < vlen; ++k)
                hs_head_ins(head, hs_hash3(vb+vpos+k), (int16_t)(vpos+k));

            vpos  = (uint16_t)(vpos + bl2);
            anchor = vpos;
        } else {
            ++vpos;
        }
    }

    if (anchor < vlen)
        if (!hs_emit_lit(vb+anchor, (uint16_t)(vlen-anchor), out, ocap, &op))
            return UINT16_MAX;
    return op;
}

/* --- buffer-level compressor (mirrors pc_compress_buffer) --- */

static void hs_update_hist(uint8_t *h, uint16_t *hl, const uint8_t *d, uint16_t len) {
    if (len >= (uint16_t)PC_HISTORY_SIZE) {
        memcpy(h, d + len - (uint16_t)PC_HISTORY_SIZE, PC_HISTORY_SIZE);
        *hl = (uint16_t)PC_HISTORY_SIZE;
    } else if ((uint16_t)(*hl + len) <= (uint16_t)PC_HISTORY_SIZE) {
        memcpy(h + *hl, d, len);
        *hl = (uint16_t)(*hl + len);
    } else {
        uint16_t keep = (uint16_t)(PC_HISTORY_SIZE - len);
        if (keep > *hl) keep = *hl;
        memmove(h, h + *hl - keep, keep);
        memcpy(h + keep, d, len);
        *hl = (uint16_t)(keep + len);
    }
}

static pc_result hs_compress_buf(
    hash_fn_t hfn,
    const uint8_t *in, size_t ilen,
    uint8_t *out, size_t ocap, size_t *olen)
{
    uint8_t  blk[PC_BLOCK_SIZE];
    uint8_t  hist[PC_HISTORY_SIZE];
    uint16_t blen = 0, hlen = 0;
    size_t   pos = 0, wp = 0;

    g_hash      = hfn;
    g_hash_mask = HS_HASH_SIZE - 1u;

    while (pos < ilen) {
        size_t room = (size_t)PC_BLOCK_SIZE - (size_t)blen;
        size_t take = ilen - pos;
        if (take > room) take = room;
        memcpy(blk + blen, in + pos, take);
        blen = (uint16_t)(blen + (uint16_t)take);
        pos += take;

        if (blen == (uint16_t)PC_BLOCK_SIZE || pos == ilen) {
            uint8_t  comb[PC_HISTORY_SIZE + PC_BLOCK_SIZE];
            uint8_t  tmp[PC_BLOCK_MAX_COMPRESSED];
            uint8_t  hdr[4];
            uint16_t clen;

            memcpy(comb, hist, hlen);
            memcpy(comb + hlen, blk, blen);
            clen = hs_compress_block(comb, hlen, blen, tmp, (uint16_t)sizeof(tmp));
            hs_update_hist(hist, &hlen, blk, blen);

            hdr[0] = (uint8_t)(blen & 0xFFu);
            hdr[1] = (uint8_t)(blen >> 8u);

            if (clen == UINT16_MAX || clen >= blen) {
                hdr[2] = 0; hdr[3] = 0;
                if (wp + 4 + blen > ocap) return PC_ERR_OUTPUT_TOO_SMALL;
                memcpy(out + wp, hdr, 4); wp += 4;
                memcpy(out + wp, blk, blen); wp += blen;
            } else {
                hdr[2] = (uint8_t)(clen & 0xFFu);
                hdr[3] = (uint8_t)(clen >> 8u);
                if (wp + 4 + clen > ocap) return PC_ERR_OUTPUT_TOO_SMALL;
                memcpy(out + wp, hdr, 4); wp += 4;
                memcpy(out + wp, tmp, clen); wp += clen;
            }
            blen = 0;
        }
    }
    *olen = wp;
    return PC_OK;
}

/* ====================================================================
 * 5.  File loading
 * ==================================================================== */

typedef struct { uint8_t *data; size_t len; const char *label; } tfile_t;

static int load_file(const char *path, tfile_t *f) {
    FILE *fp = fopen(path, "rb");
    long sz;
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return 0; }
    f->len  = (size_t)sz;
    f->data = (uint8_t *)malloc(f->len);
    if (!f->data) { fclose(fp); return 0; }
    if (fread(f->data, 1, f->len, fp) != f->len) {
        free(f->data); f->data = NULL; fclose(fp); return 0;
    }
    fclose(fp);
    return 1;
}

/* ====================================================================
 * 6.  Main
 * ==================================================================== */

#define NUM_FILES 14

static const char *fpaths[NUM_FILES] = {
    "../tests/json-508.json",
    "../tests/json-4K-pretty.json",
    "../tests/json-4K-minified.json",
    "../tests/lorem-508.txt",
    "../tests/utf8-prose-4K.txt",
    "../tests/uk-addr-4K.txt",
    "../tests/ascii-254.txt",
    "../tests/pattern-508.bin",
    "../tests/rgb-icon-508.bin",
    "../tests/uint32-4K.bin",
    "../tests/sparse-4K.bin",
    "../tests/random-4K.bin",
    "../tests/utf8-int-508.bin",
    "../tests/jpeg-508.bin",
};

static const char *flabels[NUM_FILES] = {
    "json-508    ", "json-4K-prty", "json-4K-min ",
    "lorem-508   ", "utf8-pr-4K  ", "uk-addr-4K  ",
    "ascii-254   ", "pattern-508 ", "rgb-icon-508",
    "uint32-4K   ", "sparse-4K   ", "random-4K   ",
    "utf8int-508 ", "jpeg-508    ",
};

int main(void) {
    tfile_t  files[NUM_FILES];
    unsigned fi, hi, loaded = 0;

    /* per-file compression results (cached for summary) */
    size_t csz[NUM_FILES][NUM_HASHES];
    int    cok[NUM_FILES][NUM_HASHES];

    memset(csz, 0, sizeof(csz));
    memset(cok, 0, sizeof(cok));

    printf("===================================================================\n");
    printf("  picocompress hash spike  —  %d hashes x %d payloads\n",
           NUM_HASHES, NUM_FILES);
    printf("  PC_HASH_BITS=%u  PC_HASH_SIZE=%u  PC_BLOCK_SIZE=%u\n",
           (unsigned)PC_HASH_BITS, (unsigned)PC_HASH_SIZE, (unsigned)PC_BLOCK_SIZE);
    printf("===================================================================\n\n");

    for (fi = 0; fi < NUM_FILES; fi++) {
        files[fi].label = flabels[fi];
        if (load_file(fpaths[fi], &files[fi])) loaded++;
        else { files[fi].data = NULL; files[fi].len = 0; }
    }
    printf("Loaded %u / %u test files\n\n", loaded, NUM_FILES);

    /* ---------------------------------------------------------------
     * Sanity check:  hash A via hs_compress_buf == pc_compress_buffer
     * --------------------------------------------------------------- */
    {
        int sane = 1;
        for (fi = 0; fi < NUM_FILES; fi++) {
            size_t cap, ol1 = 0, ol2 = 0;
            uint8_t *b1, *b2;
            if (!files[fi].data) continue;
            cap = pc_compress_bound(files[fi].len);
            b1  = (uint8_t *)malloc(cap);
            b2  = (uint8_t *)malloc(cap);
            pc_compress_buffer(files[fi].data, files[fi].len, b1, cap, &ol1);
            hs_compress_buf(hash_A_current, files[fi].data, files[fi].len, b2, cap, &ol2);
            if (ol1 != ol2 || memcmp(b1, b2, ol1) != 0) {
                printf("** SANITY FAIL on %s:  pc=%zu  hs=%zu\n",
                       files[fi].label, ol1, ol2);
                sane = 0;
            }
            free(b1); free(b2);
        }
        if (sane) printf("Sanity check PASSED: hash-A replica matches pc_compress_buffer\n\n");
    }

    /* ---------------------------------------------------------------
     * Distribution  @  9-bit  (per file x per hash)
     * --------------------------------------------------------------- */
    {
        unsigned bits = (unsigned)PC_HASH_BITS;
        printf("=== Distribution @ %u bits (%u buckets) ===\n\n", bits, 1u << bits);
        printf("%-13s %-10s %9s %7s %6s %5s %7s\n",
               "File", "Hash", "Chi-sq", "NrmChi", "Fill%", "MaxD", "Coll%");
        printf("------------- ---------- --------- ------- ------ ----- -------\n");

        for (fi = 0; fi < NUM_FILES; fi++) {
            if (!files[fi].data) continue;
            for (hi = 0; hi < NUM_HASHES; hi++) {
                dist_stats_t s = measure_distribution(
                    hash_fns[hi], files[fi].data, files[fi].len, bits);
                printf("%-13s %-10s %9.1f %7.3f %5.1f%% %5u %6.1f%%\n",
                       hi == 0 ? files[fi].label : "",
                       hash_names[hi],
                       s.chi_squared, s.norm_chi,
                       s.fill_pct, s.max_depth, s.collision_pct);
            }
            printf("\n");
        }
    }

    /* ---------------------------------------------------------------
     * Distribution summary:  average NormChi across all files @ 8/9/10
     * --------------------------------------------------------------- */
    {
        unsigned bits_set[] = {8, 9, 10};
        unsigned bi;
        printf("=== Distribution Summary (avg NormChi across files) ===\n\n");
        printf("%-10s", "Hash");
        for (bi = 0; bi < 3; bi++)
            printf(" %5u-bit", bits_set[bi]);
        printf("\n---------- --------- --------- ---------\n");

        for (hi = 0; hi < NUM_HASHES; hi++) {
            printf("%-10s", hash_names[hi]);
            for (bi = 0; bi < 3; bi++) {
                double sum = 0.0;
                unsigned cnt = 0;
                for (fi = 0; fi < NUM_FILES; fi++) {
                    dist_stats_t s;
                    if (!files[fi].data || files[fi].len < 10) continue;
                    s = measure_distribution(hash_fns[hi], files[fi].data,
                                             files[fi].len, bits_set[bi]);
                    sum += s.norm_chi;
                    cnt++;
                }
                printf(" %9.3f", cnt > 0 ? sum / cnt : 0.0);
            }
            printf("\n");
        }
        printf("\n(NormChi ~1.0 = ideal uniform distribution)\n\n");
    }

    /* ---------------------------------------------------------------
     * Speed
     * --------------------------------------------------------------- */
    {
        unsigned sidx = 0, iters;
        size_t mlen = 0;
        unsigned mask = (unsigned)PC_HASH_SIZE - 1u;

        for (fi = 0; fi < NUM_FILES; fi++)
            if (files[fi].data && files[fi].len > mlen)
                { mlen = files[fi].len; sidx = fi; }

        iters = mlen > 0 ? (unsigned)(200000000ULL / mlen) : 1000;
        if (iters < 500) iters = 500;

        printf("=== Speed (%u-bit, %u iters, %s %zu B) ===\n\n",
               (unsigned)PC_HASH_BITS, iters, files[sidx].label, mlen);
        printf("%-10s %12s\n", "Hash", "Mhash/sec");
        printf("---------- ------------\n");

        for (hi = 0; hi < NUM_HASHES; hi++) {
            double mh = benchmark_mhps(hash_fns[hi], files[sidx].data,
                                       files[sidx].len, mask, iters);
            printf("%-10s %10.1f\n", hash_names[hi], mh);
        }
        printf("\n");
    }

    /* ---------------------------------------------------------------
     * Compression ratio  (full roundtrip)
     * --------------------------------------------------------------- */
    printf("=== Compression Ratio (full roundtrip, %u-bit hash) ===\n\n",
           (unsigned)PC_HASH_BITS);
    printf("%-13s %-10s %6s %6s %7s %s\n",
           "File", "Hash", "InSz", "OutSz", "Ratio", "RT");
    printf("------------- ---------- ------ ------ ------- ----\n");

    for (fi = 0; fi < NUM_FILES; fi++) {
        if (!files[fi].data) continue;
        for (hi = 0; hi < NUM_HASHES; hi++) {
            size_t cap  = pc_compress_bound(files[fi].len);
            uint8_t *cb = (uint8_t *)malloc(cap);
            uint8_t *db = (uint8_t *)malloc(files[fi].len + 1024);
            size_t cl = 0, dl = 0;
            pc_result rc;
            int ok = 0;

            rc = hs_compress_buf(hash_fns[hi], files[fi].data, files[fi].len,
                                 cb, cap, &cl);
            if (rc == PC_OK) {
                rc = pc_decompress_buffer(cb, cl, db,
                                          files[fi].len + 1024, &dl);
                if (rc == PC_OK && dl == files[fi].len
                    && memcmp(db, files[fi].data, dl) == 0)
                    ok = 1;
            }

            csz[fi][hi] = cl;
            cok[fi][hi] = ok;

            printf("%-13s %-10s %6zu %6zu %6.1f%% %s\n",
                   hi == 0 ? files[fi].label : "",
                   hash_names[hi],
                   files[fi].len, cl,
                   files[fi].len > 0 ? cl * 100.0 / files[fi].len : 0.0,
                   ok ? " OK" : "FAIL");

            free(cb); free(db);
        }
        printf("\n");
    }

    /* ---------------------------------------------------------------
     * Aggregate compression summary
     * --------------------------------------------------------------- */
    printf("=== Aggregate Compression Summary ===\n\n");
    printf("%-10s %10s %10s %8s %8s\n",
           "Hash", "TotalIn", "TotalOut", "Ratio", "vs Best");
    printf("---------- ---------- ---------- -------- --------\n");

    {
        size_t ti[NUM_HASHES], to[NUM_HASHES];
        size_t best_out = (size_t)-1;
        memset(ti, 0, sizeof(ti));
        memset(to, 0, sizeof(to));

        for (hi = 0; hi < NUM_HASHES; hi++) {
            for (fi = 0; fi < NUM_FILES; fi++) {
                if (!files[fi].data || !cok[fi][hi]) continue;
                ti[hi] += files[fi].len;
                to[hi] += csz[fi][hi];
            }
            if (to[hi] < best_out) best_out = to[hi];
        }

        for (hi = 0; hi < NUM_HASHES; hi++) {
            double ratio = ti[hi] > 0 ? to[hi] * 100.0 / ti[hi] : 0.0;
            double vs    = best_out > 0 ? (double)to[hi] / (double)best_out : 1.0;
            printf("%-10s %10zu %10zu %7.2f%% %7.4fx\n",
                   hash_names[hi], ti[hi], to[hi], ratio, vs);
        }
    }

    printf("\n");

    /* ---------------------------------------------------------------
     * Per-file best/worst hash
     * --------------------------------------------------------------- */
    printf("=== Per-File Winner / Loser ===\n\n");
    printf("%-13s %10s %10s %10s %10s\n",
           "File", "Best", "BestSz", "Worst", "WorstSz");
    printf("------------- ---------- ---------- ---------- ----------\n");

    for (fi = 0; fi < NUM_FILES; fi++) {
        size_t bsz = (size_t)-1, wsz = 0;
        unsigned bi2 = 0, wi = 0;
        if (!files[fi].data) continue;
        for (hi = 0; hi < NUM_HASHES; hi++) {
            if (!cok[fi][hi]) continue;
            if (csz[fi][hi] < bsz) { bsz = csz[fi][hi]; bi2 = hi; }
            if (csz[fi][hi] > wsz) { wsz = csz[fi][hi]; wi  = hi; }
        }
        printf("%-13s %10s %10zu %10s %10zu\n",
               files[fi].label,
               hash_names[bi2], bsz,
               hash_names[wi],  wsz);
    }

    /* cleanup */
    for (fi = 0; fi < NUM_FILES; fi++) free(files[fi].data);

    printf("\nDone.\n");
    return 0;
}
