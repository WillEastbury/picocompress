#include "picocompress.h"

#include <string.h>

/* ================================================================
 * Platform feature detection (private to this translation unit).
 *
 * Capability macros (auto-detected, user-overridable with PC_NO_*):
 *   PC_HAS_HW_CRC32     — Hardware CRC32 (ARM __crc32b / x86 _mm_crc32_u8)
 *   PC_HAS_BITSCAN       — CLZ/CTZ bit-scan (word-at-a-time match)
 *   PC_CAN_UNALIGNED     — safe unaligned word loads
 *   PC_HAS_NEON          — ARM NEON 64/128-bit SIMD
 *   PC_HAS_MVE           — ARM Helium (M-Profile Vector Extension)
 *   PC_HAS_RVV           — RISC-V Vector extension
 *
 * Disable any path with -DPC_NO_HW_CRC32, -DPC_NO_BITSCAN, etc.
 * ================================================================ */

/* --- Hardware CRC32 -------------------------------------------------- *
 *  ARM AArch64 / ARMv8-A with +crc  (__ARM_FEATURE_CRC32 → __crc32b)
 *  x86/x64 with SSE4.2              (_mm_crc32_u8 via <nmmintrin.h>)
 *  Cortex-M33 (RP2350) — no CRC ISA, stays OFF.
 *  Xtensa (ESP32-S3)   — no CRC ISA, stays OFF.
 *  Disable with -DPC_NO_HW_CRC32.
 * --------------------------------------------------------------------- */
#if !defined(PC_NO_HW_CRC32)
#  if defined(__ARM_FEATURE_CRC32)
#    include <arm_acle.h>
#    define PC_HAS_HW_CRC32 1
#    define PC_CRC32_ARM     1
#  elif defined(__SSE4_2__)
     /* GCC / Clang with -msse4.2 (or implied by -march) */
#    include <nmmintrin.h>
#    define PC_HAS_HW_CRC32 1
#    define PC_CRC32_X86     1
#  elif defined(_MSC_VER) && defined(_M_X64)
     /* MSVC x64 — SSE4.2 intrinsics always available; virtually all
      * x64 CPUs since Nehalem (2008) support CRC32.  Build with
      * -DPC_NO_HW_CRC32 if you must target ancient hardware. */
#    include <nmmintrin.h>
#    define PC_HAS_HW_CRC32 1
#    define PC_CRC32_X86     1
#  elif defined(_MSC_VER) && defined(_M_IX86) && defined(__AVX__)
     /* MSVC 32-bit with /arch:AVX — SSE4.2 is guaranteed. */
#    include <nmmintrin.h>
#    define PC_HAS_HW_CRC32 1
#    define PC_CRC32_X86     1
#  else
#    define PC_HAS_HW_CRC32 0
#  endif
#else
#  define PC_HAS_HW_CRC32 0
#endif

/* --- NEON (A-class, some R-class) --- */
#if !defined(PC_NO_NEON) && defined(__ARM_NEON)
#  include <arm_neon.h>
#  define PC_HAS_NEON 1
#else
#  define PC_HAS_NEON 0
#endif

/* --- Helium / MVE (Cortex-M55+) --- */
#if !defined(PC_NO_MVE) && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE >= 1)
#  include <arm_mve.h>
#  define PC_HAS_MVE 1
#else
#  define PC_HAS_MVE 0
#endif

/* --- RISC-V Vector --- */
#if !defined(PC_NO_RVV) && defined(__riscv_vector)
#  include <riscv_vector.h>
#  define PC_HAS_RVV 1
#else
#  define PC_HAS_RVV 0
#endif

/* --- Bit-scan (CLZ/CTZ) for word-at-a-time matching --- */
#ifndef PC_HAS_BITSCAN
#  if !defined(PC_NO_BITSCAN)
#    if defined(__GNUC__) || defined(__clang__)
#      define PC_HAS_BITSCAN 1
#    elif defined(_MSC_VER)
#      include <intrin.h>
#      define PC_HAS_BITSCAN 1
#    else
#      define PC_HAS_BITSCAN 0
#    endif
#  else
#    define PC_HAS_BITSCAN 0
#  endif
#endif

/* --- Unaligned word loads (safe on M3+, A-class, x86; NOT on M0/M23) --- */
#ifndef PC_CAN_UNALIGNED
#  if !defined(PC_NO_UNALIGNED)
#    if defined(__ARM_FEATURE_UNALIGNED)
       /* ACLE standard macro — most reliable when available. */
#      define PC_CAN_UNALIGNED 1
#    elif defined(__ARM_ARCH) && (__ARM_ARCH >= 7)
#      define PC_CAN_UNALIGNED 1
#    elif defined(__ARM_ARCH_8M_MAIN__)
       /* ARMv8-M Mainline (Cortex-M33/M55) — supports unaligned access.
        * Some toolchains define this without setting __ARM_ARCH >= 8. */
#      define PC_CAN_UNALIGNED 1
#    elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
       /* Cortex-M3 / M4 / M7 fallback for toolchains that omit __ARM_ARCH. */
#      define PC_CAN_UNALIGNED 1
#    elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#      define PC_CAN_UNALIGNED 1
#    elif defined(__aarch64__) || defined(_M_ARM64)
#      define PC_CAN_UNALIGNED 1
#    else
#      define PC_CAN_UNALIGNED 0
#    endif
#  else
#    define PC_CAN_UNALIGNED 0
#  endif
#endif

#define PC_HASH_SIZE (1u << PC_HASH_BITS)
#define PC_INVALID_POS (-1)
#define PC_GOOD_MATCH 8u
#define PC_REPEAT_CACHE_SIZE 3u

/* ---- Hash function --------------------------------------------------- */

#if PC_HAS_HW_CRC32 && defined(PC_CRC32_ARM)
/* Hardware CRC32 hash — 1-cycle on AArch64 with +crc, excellent distribution. */
static uint16_t pc_hash3(const uint8_t *p) {
    uint32_t h = __crc32b(__crc32b(__crc32b(0u, p[0]), p[1]), p[2]);
    return (uint16_t)(h & (PC_HASH_SIZE - 1u));
}
#elif PC_HAS_HW_CRC32 && defined(PC_CRC32_X86)
/* Hardware CRC32 hash — single-uop on x86 with SSE4.2, excellent distribution. */
static uint16_t pc_hash3(const uint8_t *p) {
    uint32_t h = (uint32_t)_mm_crc32_u8(
                    (uint32_t)_mm_crc32_u8(
                        (uint32_t)_mm_crc32_u8(0u, p[0]), p[1]), p[2]);
    return (uint16_t)(h & (PC_HASH_SIZE - 1u));
}
#else
/* Portable multiply hash — good distribution, 3 multiplies. */
static uint16_t pc_hash3(const uint8_t *p) {
    uint32_t v = ((uint32_t)p[0] * 251u) + ((uint32_t)p[1] * 11u) + ((uint32_t)p[2] * 3u);
    return (uint16_t)(v & (PC_HASH_SIZE - 1u));
}
#endif

/* ---- Match length comparison ----------------------------------------- */

/* Helper: count first-difference byte position in a 32-bit XOR word.
 * Uses CTZ on little-endian, CLZ on big-endian. Never called with xor==0.
 * Only used by the word-at-a-time match path (when no SIMD is available). */
#if PC_HAS_BITSCAN && PC_CAN_UNALIGNED && !PC_HAS_NEON && !PC_HAS_MVE && !PC_HAS_RVV
static uint16_t pc_first_diff_bytes(uint32_t xor_val) {
#  if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    if defined(__GNUC__) || defined(__clang__)
    return (uint16_t)((unsigned)__builtin_clz(xor_val) >> 3u);
#    elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse(&idx, xor_val);
    return (uint16_t)((31u - idx) >> 3u);
#    endif
#  else /* little-endian (default for ARM, x86, RISC-V) */
#    if defined(__GNUC__) || defined(__clang__)
    return (uint16_t)((unsigned)__builtin_ctz(xor_val) >> 3u);
#    elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward(&idx, xor_val);
    return (uint16_t)(idx >> 3u);
#    endif
#  endif
}
#endif /* PC_HAS_BITSCAN && PC_CAN_UNALIGNED && !SIMD */

#if PC_HAS_NEON
/* NEON: compare 16 bytes at a time using 128-bit vectors.
 * On mismatch, use CLZ on the inverted comparison mask to find
 * the first differing byte without a scalar loop. */
static uint16_t pc_match_len(const uint8_t *a, const uint8_t *b, uint16_t limit) {
    uint16_t m = 0;
    /* 16-byte NEON loop */
    while ((uint16_t)(limit - m) >= 16u) {
        uint8x16_t va = vld1q_u8(a + m);
        uint8x16_t vb = vld1q_u8(b + m);
        uint8x16_t cmp = vceqq_u8(va, vb);
        /* Narrow to 8-bit bitmask: shift each lane's MSB into a packed u64 pair */
        uint8x16_t msb = vshrq_n_u8(cmp, 7);           /* 0x01 or 0x00 per lane */
        uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(msb), 0);
        uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(msb), 1);
        if (lo != 0x0101010101010101ULL) {
            /* mismatch in first 8 bytes — find which lane */
            uint64_t diff = lo ^ 0x0101010101010101ULL;
            uint16_t k = (uint16_t)(__builtin_ctzll(diff) >> 3u);
            return (uint16_t)(m + k);
        }
        if (hi != 0x0101010101010101ULL) {
            uint64_t diff = hi ^ 0x0101010101010101ULL;
            uint16_t k = (uint16_t)(__builtin_ctzll(diff) >> 3u);
            return (uint16_t)(m + 8u + k);
        }
        m = (uint16_t)(m + 16u);
    }
    /* 8-byte NEON tail */
    if ((uint16_t)(limit - m) >= 8u) {
        uint8x8_t va = vld1_u8(a + m);
        uint8x8_t vb = vld1_u8(b + m);
        uint8x8_t cmp = vceq_u8(va, vb);
        uint8x8_t msb = vshr_n_u8(cmp, 7);
        uint64_t mask;
        vst1_u8((uint8_t *)&mask, msb);
        if (mask != 0x0101010101010101ULL) {
            uint64_t diff = mask ^ 0x0101010101010101ULL;
            uint16_t k = (uint16_t)(__builtin_ctzll(diff) >> 3u);
            return (uint16_t)(m + k);
        }
        m = (uint16_t)(m + 8u);
    }
    /* scalar tail */
    while (m < limit && a[m] == b[m]) ++m;
    return m;
}
#elif PC_HAS_MVE
/* Helium/MVE: compare 16 bytes at a time using predicated vector ops.
 * On mismatch, use __builtin_ctz on the inverted predicate mask. */
static uint16_t pc_match_len(const uint8_t *a, const uint8_t *b, uint16_t limit) {
    uint16_t m = 0;
    while ((uint16_t)(limit - m) >= 16u) {
        uint8x16_t va = vld1q_u8(a + m);
        uint8x16_t vb = vld1q_u8(b + m);
        mve_pred16_t pred = vcmpeqq_u8(va, vb);
        if (pred != 0xFFFFu) {
            /* pred has 1 bit per matching lane; invert to find first mismatch */
            uint16_t mismatch = (uint16_t)(~pred & 0xFFFFu);
            uint16_t k = (uint16_t)__builtin_ctz((unsigned)mismatch);
            return (uint16_t)(m + k);
        }
        m = (uint16_t)(m + 16u);
    }
    while (m < limit && a[m] == b[m]) ++m;
    return m;
}
#elif PC_HAS_RVV
/* RISC-V Vector: compare vl bytes per iteration. */
static uint16_t pc_match_len(const uint8_t *a, const uint8_t *b, uint16_t limit) {
    uint16_t m = 0;
    while (m < limit) {
        size_t vl = __riscv_vsetvl_e8m1((size_t)(limit - m));
        vuint8m1_t va = __riscv_vle8_v_u8m1(a + m, vl);
        vuint8m1_t vb = __riscv_vle8_v_u8m1(b + m, vl);
        vbool8_t neq = __riscv_vmsne_vv_u8m1_b8(va, vb, vl);
        long first = __riscv_vfirst_m_b8(neq, vl);
        if (first >= 0) return (uint16_t)(m + (uint16_t)first);
        m = (uint16_t)(m + (uint16_t)vl);
    }
    return m;
}
#elif PC_HAS_BITSCAN && PC_CAN_UNALIGNED
/* Word-at-a-time with CLZ/CTZ — 4 bytes per compare (M3+, x86, AArch64). */
static uint16_t pc_match_len(const uint8_t *a, const uint8_t *b, uint16_t limit) {
    uint16_t m = 0;
    while ((uint16_t)(limit - m) >= 4u) {
        uint32_t wa, wb, x;
        memcpy(&wa, a + m, 4);
        memcpy(&wb, b + m, 4);
        x = wa ^ wb;
        if (x != 0u) return (uint16_t)(m + pc_first_diff_bytes(x));
        m = (uint16_t)(m + 4u);
    }
    while (m < limit && a[m] == b[m]) ++m;
    return m;
}
#else
/* Portable byte-at-a-time (M0, unknown targets). */
static uint16_t pc_match_len(const uint8_t *a, const uint8_t *b, uint16_t limit) {
    uint16_t m = 0;
    while (m < limit && a[m] == b[m]) {
        ++m;
    }
    return m;
}
#endif

/* ---- Stats accumulation macros (zero-overhead when disabled) ---------- */
#ifdef PC_ENABLE_STATS
#  define PC_STAT_INC(st, field)       do { if (st) (st)->field++; } while(0)
#  define PC_STAT_ADD(st, field, n)    do { if (st) (st)->field += (uint32_t)(n); } while(0)
#else
#  define PC_STAT_INC(st, field)       ((void)0)
#  define PC_STAT_ADD(st, field, n)    ((void)0)
#endif

static int pc_emit_literals(
    const uint8_t *src,
    uint16_t src_len,
    uint8_t *dst,
    uint16_t dst_cap,
    uint16_t *op
) {
    uint16_t pos = 0;
    while (pos < src_len) {
        uint16_t chunk = (uint16_t)(src_len - pos);
        if (chunk > PC_LITERAL_MAX) {
            chunk = PC_LITERAL_MAX;
        }
        if ((uint32_t)(*op) + 1u + chunk > dst_cap) {
            return 0;
        }
        dst[(*op)++] = (uint8_t)(chunk - 1u);               /* 0x00..0x3F */
        memcpy(dst + *op, src + pos, chunk);
        *op = (uint16_t)(*op + chunk);
        pos = (uint16_t)(pos + chunk);
    }
    return 1;
}

static void pc_head_insert(
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE],
    uint16_t hash,
    int16_t pos
) {
    int d;
    for (d = (int)PC_HASH_CHAIN_DEPTH - 1; d > 0; --d) {
        head[d][hash] = head[d - 1][hash];
    }
    head[0][hash] = pos;
}

#if PC_DICT_COUNT > 0

typedef struct {
    const uint8_t *data;
    uint8_t len;
} pc_dict_entry_t;

/* ---- Context-biased static dictionary (96 entries, ROM/flash) ----------
 * Turing crib mode: entries are partitioned into three contiguous regions.
 * The encoder probes the context-appropriate region first (8 entries),
 * then falls through to the full dict only if needed.
 *
 * Region layout:
 *   START    [0..15]   — stream/block openers, format signatures
 *   BOUNDARY [16..47]  — patterns after structural delimiters
 *   BODY     [48..95]  — English phonemes, common words, keywords
 *
 * Token encoding (unchanged):
 *   entries  0..63  → 0x40 | idx
 *   entries 64..79  → 0xE0 | (idx - 64)
 *   entries 80..95  → 0xD0 | (idx - 80)
 * -------------------------------------------------------------------- */

#define PC_DICT_PRESCAN    8u
#define PC_CTX_START       0u
#define PC_CTX_START_END  16u
#define PC_CTX_BOUND      16u
#define PC_CTX_BOUND_END  48u
#define PC_CTX_BODY       48u
#define PC_CTX_BODY_END   96u

/* --- START region [0..15]: stream openers, format signatures --- */
static const uint8_t pc_s00[] = { 'h','t','t','p','s',':','/','/'}; /* https:// */
static const uint8_t pc_s01[] = "http";
static const uint8_t pc_s02[] = { 'H', 'T', 'T', 'P' };            /* HTTP */
static const uint8_t pc_s03[] = { 'J', 'S', 'O', 'N' };            /* JSON */
static const uint8_t pc_s04[] = { 'T', 'h', 'e', ' ' };            /* The  */
static const uint8_t pc_s05[] = "true";
static const uint8_t pc_s06[] = "null";
static const uint8_t pc_s07[] = "false";
static const uint8_t pc_s08[] = "name";
static const uint8_t pc_s09[] = "data";
static const uint8_t pc_s10[] = "status";
static const uint8_t pc_s11[] = "message";
static const uint8_t pc_s12[] = "response";
static const uint8_t pc_s13[] = { 'N', 'o', 'n', 'e' };            /* None */
static const uint8_t pc_s14[] = "type";
static const uint8_t pc_s15[] = "error";

/* --- BOUNDARY region [16..47]: after delimiters (} ] , ; : . \n) --- */
static const uint8_t pc_b00[] = { '"', ':', ' ', '"' };             /* ": " */
static const uint8_t pc_b01[] = { '"', ':', '"' };                  /* ":" */
static const uint8_t pc_b02[] = { '"', ':', '{' };                  /* ":{ */
static const uint8_t pc_b03[] = { '"', ':', '[' };                  /* ":[ */
static const uint8_t pc_b04[] = { '}', ',', '\n', '"' };            /* },\n" */
static const uint8_t pc_b05[] = { '"', '}', ',' };                  /* "}, */
static const uint8_t pc_b06[] = { '"', ']', ',' };                  /* "], */
static const uint8_t pc_b07[] = { ',', '"', ',' };                  /* "," */
static const uint8_t pc_b08[] = { '"', ',', ' ', '"' };             /* ", " */
static const uint8_t pc_b09[] = { 'n','o','"',':' };                /* no": */
static const uint8_t pc_b10[] = { 'n','u','m','b','e','r','"',':' };/* number": */
static const uint8_t pc_b11[] = { '<', '/', 'd', 'i', 'v' };       /* </div */
static const uint8_t pc_b12[] = { '<', '/', 'd', 'i' };            /* </di */
static const uint8_t pc_b13[] = { '=', '"', 'h', 't' };            /* ="ht */
static const uint8_t pc_b14[] = { '/', '>', '\r', '\n' };           /* />\r\n */
static const uint8_t pc_b15[] = { '.', ' ', 'T', 'h', 'e', ' ' }; /* . The  */
static const uint8_t pc_b16[] = { '.', ' ', 'I', 't', ' ' };       /* . It  */
static const uint8_t pc_b17[] = { '.', ' ', 'T', 'h', 'i', 's', ' ' }; /* . This  */
static const uint8_t pc_b18[] = { '.', ' ', 'A', ' ' };            /* . A  */
static const uint8_t pc_b19[] = "ould ";                             /* would/could/should */
static const uint8_t pc_b20[] = "operator";
/* keywords after delimiters */
static const uint8_t pc_b21[] = "DIM";
static const uint8_t pc_b22[] = "FOR";
static const uint8_t pc_b23[] = "END";
static const uint8_t pc_b24[] = "REL";
static const uint8_t pc_b25[] = "EACH";
static const uint8_t pc_b26[] = "LOAD";
static const uint8_t pc_b27[] = "SAVE";
static const uint8_t pc_b28[] = "CARD";
static const uint8_t pc_b29[] = "JUMP";
static const uint8_t pc_b30[] = "PRINT";
static const uint8_t pc_b31[] = "INPUT";

/* --- BODY region [48..95]: phonemes, common words, suffixes --- */
static const uint8_t pc_y00[] = "tion";
static const uint8_t pc_y01[] = "ment";
static const uint8_t pc_y02[] = "ness";
static const uint8_t pc_y03[] = "able";
static const uint8_t pc_y04[] = "ight";
static const uint8_t pc_y05[] = "ation";
static const uint8_t pc_y06[] = "the";
static const uint8_t pc_y07[] = "ing";
static const uint8_t pc_y08[] = "ion";
static const uint8_t pc_y09[] = "ent";
static const uint8_t pc_y10[] = "ter";
static const uint8_t pc_y11[] = "and";
static const uint8_t pc_y12[] = "have";
static const uint8_t pc_y13[] = "time";
static const uint8_t pc_y14[] = "mode";
static const uint8_t pc_y15[] = "code";
static const uint8_t pc_y16[] = "size";
static const uint8_t pc_y17[] = "list";
static const uint8_t pc_y18[] = "item";
static const uint8_t pc_y19[] = "text";
static const uint8_t pc_y20[] = "value";
static const uint8_t pc_y21[] = "state";
static const uint8_t pc_y22[] = "alert";
static const uint8_t pc_y23[] = "input";
static const uint8_t pc_y24[] = "order";
static const uint8_t pc_y25[] = "number";
static const uint8_t pc_y26[] = "active";
static const uint8_t pc_y27[] = "device";
static const uint8_t pc_y28[] = "region";
static const uint8_t pc_y29[] = "string";
static const uint8_t pc_y30[] = "result";
static const uint8_t pc_y31[] = "length";
static const uint8_t pc_y32[] = "content";
static const uint8_t pc_y33[] = "request";
static const uint8_t pc_y34[] = "default";
static const uint8_t pc_y35[] = "GOSUB";
static const uint8_t pc_y36[] = "STREAM";
static const uint8_t pc_y37[] = "RETURN";
static const uint8_t pc_y38[] = "SWITCH";
static const uint8_t pc_y39[] = "PROGRAM";
/* duplicate phoneme slots kept for coverage */
static const uint8_t pc_y40[] = "tion";
static const uint8_t pc_y41[] = "ment";
static const uint8_t pc_y42[] = "ness";
static const uint8_t pc_y43[] = "able";
static const uint8_t pc_y44[] = "ight";
static const uint8_t pc_y45[] = "ation";
static const uint8_t pc_y46[] = "ould ";
static const uint8_t pc_y47[] = { '"', ':', ' ', '"' };             /* ": " body dup */

static const pc_dict_entry_t pc_static_dict[PC_DICT_COUNT] = {
    /* START [0..15] */
    { pc_s00,8 }, { pc_s01,4 }, { pc_s02,4 }, { pc_s03,4 },
    { pc_s04,4 }, { pc_s05,4 }, { pc_s06,4 }, { pc_s07,5 },
    { pc_s08,4 }, { pc_s09,4 }, { pc_s10,6 }, { pc_s11,7 },
    { pc_s12,8 }, { pc_s13,4 }, { pc_s14,4 }, { pc_s15,5 },
    /* BOUNDARY [16..47] */
    { pc_b00,4 }, { pc_b01,3 }, { pc_b02,3 }, { pc_b03,3 },
    { pc_b04,4 }, { pc_b05,3 }, { pc_b06,3 }, { pc_b07,3 },
    { pc_b08,4 }, { pc_b09,4 }, { pc_b10,8 }, { pc_b11,5 },
    { pc_b12,4 }, { pc_b13,4 }, { pc_b14,4 }, { pc_b15,6 },
    { pc_b16,5 }, { pc_b17,7 }, { pc_b18,4 }, { pc_b19,5 },
    { pc_b20,8 }, { pc_b21,3 }, { pc_b22,3 }, { pc_b23,3 },
    { pc_b24,3 }, { pc_b25,4 }, { pc_b26,4 }, { pc_b27,4 },
    { pc_b28,4 }, { pc_b29,4 }, { pc_b30,5 }, { pc_b31,5 },
    /* BODY [48..95] */
    { pc_y00,4 }, { pc_y01,4 }, { pc_y02,4 }, { pc_y03,4 },
    { pc_y04,4 }, { pc_y05,5 }, { pc_y06,3 }, { pc_y07,3 },
    { pc_y08,3 }, { pc_y09,3 }, { pc_y10,3 }, { pc_y11,3 },
    { pc_y12,4 }, { pc_y13,4 }, { pc_y14,4 }, { pc_y15,4 },
    { pc_y16,4 }, { pc_y17,4 }, { pc_y18,4 }, { pc_y19,4 },
    { pc_y20,5 }, { pc_y21,5 }, { pc_y22,5 }, { pc_y23,5 },
    { pc_y24,5 }, { pc_y25,6 }, { pc_y26,6 }, { pc_y27,6 },
    { pc_y28,6 }, { pc_y29,6 }, { pc_y30,6 }, { pc_y31,6 },
    { pc_y32,7 }, { pc_y33,7 }, { pc_y34,7 }, { pc_y35,5 },
    { pc_y36,6 }, { pc_y37,6 }, { pc_y38,6 }, { pc_y39,7 },
    { pc_y40,4 }, { pc_y41,4 }, { pc_y42,4 }, { pc_y43,4 },
    { pc_y44,4 }, { pc_y45,5 }, { pc_y46,5 }, { pc_y47,4 },
};

#endif /* PC_DICT_COUNT > 0 */

/* Find best savings among repeat-cache, dict, and LZ at a virtual position.
 * Order: repeat-cache → dictionary → hash-chain LZ.
 * good_match: threshold to stop probing early (adaptive per block region).
 * skip_dict: when true, skip dictionary probing (self-disabled after probe window).
 * Returns net savings (bytes saved vs literal). Fills out_* params. */
static int pc_find_best(
    const uint8_t *vbuf, uint16_t vbuf_len, uint16_t vpos,
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE],
    const uint16_t rep_offsets[PC_REPEAT_CACHE_SIZE],
    uint16_t good_match,
    int skip_dict,
    uint16_t hist_len_ctx,
    uint16_t *out_len, uint16_t *out_off, uint16_t *out_dict,
    int *out_is_repeat
) {
    int best_savings = 0;
    uint16_t remaining = (uint16_t)(vbuf_len - vpos);
    int d;

    *out_len = 0;
    *out_off = 0;
    *out_dict = UINT16_MAX;
    *out_is_repeat = 0;

    /* 1. Repeat-offset cache — try recent offsets first (1-byte token each).
     * These fire constantly on structured data. Check first byte before
     * full compare (idea #9: early reject).
     * NOTE: only rep_offsets[0] can emit as a repeat token (decoder tracks
     * only last_offset). Matches on [1]/[2] are scored as normal LZ. */
    if (remaining >= PC_MATCH_MIN) {
        uint16_t max_rep = remaining > PC_MATCH_MAX ? PC_MATCH_MAX : remaining;
        for (d = 0; d < (int)PC_REPEAT_CACHE_SIZE; ++d) {
            uint16_t off = rep_offsets[d];
            uint16_t len;
            int is_rep, token_cost, s;
            if (off == 0u || off > vpos) continue;
            /* early reject: check first byte */
            if (vbuf[vpos] != vbuf[vpos - off]) continue;
            /* fast path for len 2-3 (idea #2) */
            if (remaining >= 2u && vbuf[vpos + 1u] != vbuf[vpos - off + 1u]) continue;
            len = pc_match_len(vbuf + vpos - off, vbuf + vpos, max_rep);
            if (len < PC_MATCH_MIN) continue;

            /* only slot 0 can use the cheap repeat token (max len 17 with 4-bit field) */
            is_rep = (d == 0 && len <= 17u) ? 1 : 0;
            token_cost = is_rep ? 1 : (off <= PC_OFFSET_SHORT_MAX ? 2 : 3);
            s = (int)len - token_cost;

            if (s > best_savings) {
                best_savings = s;
                *out_len = len;
                *out_off = off;
                *out_dict = UINT16_MAX;
                *out_is_repeat = is_rep;
                if (len >= good_match) return best_savings; /* #10: good enough */
            }
        }
    }

    /* 2. Context-biased dictionary search (Turing crib mode).
     * Determine context from position and previous byte, then:
     *   Pass 1: prescan 8 entries from the biased region
     *   Pass 2: scan remainder if prescan didn't find a strong match
     * No extra memory, no dynamic state — just scan order rotation. */
#if PC_DICT_COUNT > 0
    if (!skip_dict) {
        uint8_t first_byte = vbuf[vpos];
        uint16_t ctx_begin, ctx_end, ctx_threshold;
        uint32_t block_pos = (uint32_t)(vpos - (uint32_t)hist_len_ctx);

        /* context detection */
        if (block_pos < 8u) {
            ctx_begin = PC_CTX_START;
            ctx_end   = PC_CTX_START_END;
            ctx_threshold = 3; /* accept len >= 4 (savings 3+) */
        } else if (vpos > 0u && (vbuf[vpos - 1u] == '\n' || vbuf[vpos - 1u] == '}'
                || vbuf[vpos - 1u] == ']' || vbuf[vpos - 1u] == ';'
                || vbuf[vpos - 1u] == ',' || vbuf[vpos - 1u] == ':'
                || vbuf[vpos - 1u] == '.')) {
            ctx_begin = PC_CTX_BOUND;
            ctx_end   = PC_CTX_BOUND_END;
            ctx_threshold = 3;
        } else {
            ctx_begin = PC_CTX_BODY;
            ctx_end   = PC_CTX_BODY_END;
            ctx_threshold = 5; /* body: accept len >= 6 */
        }

        /* Pass 1: prescan biased region (first PC_DICT_PRESCAN entries) */
        {
            uint16_t prescan_end = ctx_begin + PC_DICT_PRESCAN;
            if (prescan_end > ctx_end) prescan_end = ctx_end;
            for (d = (int)ctx_begin; d < (int)prescan_end; ++d) {
                uint8_t dlen = pc_static_dict[d].len;
                int s;
                if (dlen > remaining) continue;
                if ((int)dlen - 1 <= best_savings) continue;
                if (pc_static_dict[d].data[0] != first_byte) continue;
                if (memcmp(vbuf + vpos, pc_static_dict[d].data, dlen) != 0) continue;
                s = (int)dlen - 1;
                best_savings = s;
                *out_dict = (uint16_t)d;
                *out_len = dlen;
                *out_off = 0;
                *out_is_repeat = 0;
                if (s >= (int)ctx_threshold) goto dict_done;
            }
        }

        /* Pass 2: scan full dictionary, skipping prescan range */
        for (d = 0; d < (int)PC_DICT_COUNT; ++d) {
            uint8_t dlen;
            int s;
            /* skip entries already checked in prescan */
            if ((uint16_t)d >= ctx_begin && (uint16_t)d < ctx_begin + PC_DICT_PRESCAN
                && (uint16_t)d < ctx_end) continue;
            dlen = pc_static_dict[d].len;
            if (dlen > remaining) continue;
            if ((int)dlen - 1 <= best_savings) continue;
            if (pc_static_dict[d].data[0] != first_byte) continue;
            if (memcmp(vbuf + vpos, pc_static_dict[d].data, dlen) != 0) continue;
            s = (int)dlen - 1;
            best_savings = s;
            *out_dict = (uint16_t)d;
            *out_len = dlen;
            *out_off = 0;
            *out_is_repeat = 0;
            if (dlen >= good_match) goto dict_done;
        }
        dict_done: ;
    }
#else
    (void)skip_dict;
#endif

    /* 3. LZ hash-chain match — with early reject (#9), offset scoring (#5),
     * good-enough bail (#10). */
    if (remaining >= 3u) {
        uint16_t hash = pc_hash3(vbuf + vpos);
        uint16_t max_len_short = remaining > PC_MATCH_MAX ? PC_MATCH_MAX : remaining;
        uint16_t max_len_long = remaining > PC_LONG_MATCH_MAX ? PC_LONG_MATCH_MAX : remaining;
        uint8_t first_byte = vbuf[vpos];

        for (d = 0; d < (int)PC_HASH_CHAIN_DEPTH; ++d) {
            int16_t prev = head[d][hash];
            uint16_t prev_pos, off, len, max_len;
            int s, token_cost;

            if (prev < 0) continue;
            prev_pos = (uint16_t)prev;
            if (prev_pos >= vpos) continue;
            off = (uint16_t)(vpos - prev_pos);
            if (off == 0u || off > PC_OFFSET_LONG_MAX) continue;

            /* #9: early reject — check first byte before full compare */
            if (vbuf[prev_pos] != first_byte) continue;

            max_len = (off <= PC_OFFSET_SHORT_MAX) ? max_len_short : max_len_long;
            len = pc_match_len(vbuf + prev_pos, vbuf + vpos, max_len);
            if (len < PC_MATCH_MIN) continue;

            token_cost = (off <= PC_OFFSET_SHORT_MAX) ? 2 : 3;
            s = (int)len - token_cost;

            /* #5: offset scoring — prefer nearer matches at equal savings.
             * #A: long-offset length bonus — when a far match is 2+ bytes
             * longer, prefer it even though the token costs 1 more byte. */
            if (s > best_savings
                || (s == best_savings && len > *out_len)
                || (s == best_savings && len == *out_len && off < *out_off)
                || (s == best_savings - 1 && len >= *out_len + 2u)) {
                best_savings = (int)len - token_cost;
                *out_len = len;
                *out_off = off;
                *out_dict = UINT16_MAX;
                *out_is_repeat = 0;
                if (len >= good_match) return best_savings; /* #10 */
            }
        }
    }

    return best_savings;
}

/* Compress block_len bytes from vbuf starting at offset hist_len.
 * vbuf = [history(hist_len) | block(block_len)].
 * Returns compressed size, or UINT16_MAX on overflow. */
static uint16_t pc_compress_block(
    const uint8_t *vbuf,
    uint16_t hist_len,
    uint16_t block_len,
    uint8_t *out,
    uint16_t out_cap
#ifdef PC_ENABLE_STATS
    , pc_encoder_stats *stats
#endif
) {
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE];
    uint16_t rep_offsets[PC_REPEAT_CACHE_SIZE] = {0, 0, 0};
    uint16_t vbuf_len = (uint16_t)(hist_len + block_len);
    uint16_t vpos;
    uint16_t anchor;
    uint16_t op = 0;
    memset(head, 0xFF, sizeof(head));

    /* seed hash table from history — use normal insert so chain works */
    if (hist_len >= 3u) {
        uint16_t p;
        for (p = 0; (uint16_t)(p + 2u) < hist_len; ++p) {
            pc_head_insert(head, pc_hash3(vbuf + p), (int16_t)p);
        }
        /* Re-inject positions near the block boundary into slot 0 */
        {
            uint16_t tail_start = hist_len > 64u ? (uint16_t)(hist_len - 64u) : 0u;
            for (p = tail_start; (uint16_t)(p + 2u) < hist_len; ++p) {
                uint16_t h = pc_hash3(vbuf + p);
                if (head[0][h] != (int16_t)p) {
                    int16_t save = head[PC_HASH_CHAIN_DEPTH - 1u][h];
                    pc_head_insert(head, h, (int16_t)p);
                    head[PC_HASH_CHAIN_DEPTH - 1u][h] = save;
                }
            }
        }
    }

    anchor = hist_len;
    vpos = hist_len;

    /* Self-disabling dictionary: check first bytes of the block.
     * If byte[0] is a structured-data opener ({, [, <, BOM lead 0xEF)
     * → keep dict active.  Otherwise, if any of the first 4 bytes is
     * outside printable ASCII (0x20..0x7E) → binary data, skip dict.
     * This is a single cheap check per block, not per position. */
    {
        int dict_skip = 0;
#if PC_DICT_COUNT > 0
        if (block_len >= 1u) {
            uint8_t b0 = vbuf[hist_len];
            if (b0 == '{' || b0 == '[' || b0 == '<' || b0 == 0xEFu) {
                dict_skip = 0; /* structured data — keep dict */
            } else {
                /* check first 4 bytes for non-printable ASCII */
                uint16_t check_len = block_len < 4u ? block_len : 4u;
                uint16_t ci;
                dict_skip = 0;
                for (ci = 0; ci < check_len; ++ci) {
                    uint8_t c = vbuf[hist_len + ci];
                    if (c < 0x20u || c > 0x7Eu) {
                        dict_skip = 1;
                        break;
                    }
                }
            }
        }
#endif

    while (vpos < vbuf_len) {
        uint16_t best_len, best_off, best_dict;
        int best_is_repeat, best_savings;
retry_pos:
        best_len = 0; best_off = 0; best_dict = UINT16_MAX;
        best_is_repeat = 0;

        if ((uint16_t)(vbuf_len - vpos) < PC_MATCH_MIN) {
            break;
        }

        best_savings = pc_find_best(
            vbuf, vbuf_len, vpos, head, rep_offsets, PC_GOOD_MATCH,
            dict_skip, hist_len,
            &best_len, &best_off, &best_dict, &best_is_repeat);

        /* insert current position into hash table (needs 3 bytes) */
        if ((uint16_t)(vbuf_len - vpos) >= 3u) {
            pc_head_insert(head, pc_hash3(vbuf + vpos), (int16_t)vpos);
        }

        /* #7: literal run extension — skip weak matches (savings <= 1) when
         * mid-literal-run. The token overhead isn't worth breaking the run. */
        if (best_savings <= 1 && best_dict == UINT16_MAX && anchor < vpos) {
            best_savings = 0;
        }

        /* #4: lazy matching — only if current match is short.
         * Long matches (>= GOOD_MATCH) are rarely beaten; accept immediately. */
        if (best_savings > 0 && best_len < PC_GOOD_MATCH) {
            uint16_t step;
            for (step = 1; step <= (uint16_t)PC_LAZY_STEPS; ++step) {
                uint16_t npos = (uint16_t)(vpos + step);
                if (npos >= vbuf_len || (uint16_t)(vbuf_len - npos) < PC_MATCH_MIN)
                    break;
                {
                    uint16_t n_len, n_off, n_dict;
                    int n_rep;
                    int n_sav = pc_find_best(
                        vbuf, vbuf_len, npos, head, rep_offsets, PC_GOOD_MATCH,
                        dict_skip, hist_len,
                        &n_len, &n_off, &n_dict, &n_rep);
                    if (n_sav > best_savings) {
                        uint16_t s;
                        for (s = 0; s < step; ++s) {
                            uint16_t sp = (uint16_t)(vpos + s);
                            if ((uint16_t)(vbuf_len - sp) >= 3u)
                                pc_head_insert(head, pc_hash3(vbuf + sp), (int16_t)sp);
                        }
                        PC_STAT_INC(stats, lazy_improvements);
                        vpos = npos;
                        goto retry_pos;
                    }
                }
            }
        }

        /* emit */
        if (best_savings > 0) {
            uint16_t lit_len = (uint16_t)(vpos - anchor);
            uint16_t k;

            PC_STAT_ADD(stats, literal_bytes, lit_len);
            if (best_len >= PC_GOOD_MATCH) {
                PC_STAT_INC(stats, good_enough_hits);
            }

            if (!pc_emit_literals(vbuf + anchor, lit_len, out, out_cap, &op)) {
                return UINT16_MAX;
            }

            if (best_dict != UINT16_MAX) {
                if ((uint32_t)op + 1u > out_cap) return UINT16_MAX;
                if (best_dict < 64u) {
                    out[op++] = (uint8_t)(0x40u | (best_dict & 0x3Fu));
                } else if (best_dict < 80u) {
                    out[op++] = (uint8_t)(0xE0u | ((best_dict - 64u) & 0x0Fu));
                } else {
                    out[op++] = (uint8_t)(0xD0u | ((best_dict - 80u) & 0x0Fu));
                }
                PC_STAT_INC(stats, dict_hits);
                PC_STAT_INC(stats, match_count);
            } else if (best_is_repeat) {
                if ((uint32_t)op + 1u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xC0u | ((best_len - PC_MATCH_MIN) & 0x0Fu));
                PC_STAT_INC(stats, repeat_hits);
                PC_STAT_INC(stats, match_count);
            } else if (best_off <= PC_OFFSET_SHORT_MAX && best_len <= PC_MATCH_MAX) {
                /* short-offset LZ: 2-byte token */
                if ((uint32_t)op + 2u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(
                    0x80u
                    | (((best_len - PC_MATCH_MIN) & 0x1Fu) << 1u)
                    | ((best_off >> 8u) & 0x01u));
                out[op++] = (uint8_t)(best_off & 0xFFu);
                PC_STAT_INC(stats, lz_short_hits);
                PC_STAT_INC(stats, match_count);
            } else {
                /* long-offset LZ: 3-byte token (0xF0..0xFF) */
                uint16_t elen = best_len > PC_LONG_MATCH_MAX ? PC_LONG_MATCH_MAX : best_len;
                if ((uint32_t)op + 3u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xF0u | ((elen - PC_LONG_MATCH_MIN) & 0x0Fu));
                out[op++] = (uint8_t)((best_off >> 8u) & 0xFFu);
                out[op++] = (uint8_t)(best_off & 0xFFu);
                best_len = elen;
                PC_STAT_INC(stats, lz_long_hits);
                PC_STAT_INC(stats, match_count);
            }

            /* update repeat-offset cache (#1) */
            if (!best_is_repeat && best_off != 0u && best_dict == UINT16_MAX) {
                rep_offsets[2] = rep_offsets[1];
                rep_offsets[1] = rep_offsets[0];
                rep_offsets[0] = best_off;
            }

            for (k = 1; k < best_len && (uint16_t)(vpos + k + 2u) < vbuf_len; ++k) {
                pc_head_insert(head, pc_hash3(vbuf + vpos + k), (int16_t)(vpos + k));
            }

            vpos = (uint16_t)(vpos + best_len);
            anchor = vpos;
        } else {
            ++vpos;
        }
    }

    if (anchor < vbuf_len) {
        PC_STAT_ADD(stats, literal_bytes, (uint16_t)(vbuf_len - anchor));
        if (!pc_emit_literals(vbuf + anchor, (uint16_t)(vbuf_len - anchor), out, out_cap, &op)) {
            return UINT16_MAX;
        }
    }

    } /* end dict_skip scope */

    return op;
}

/* Copy match bytes, resolving cross-block references into history. */
static void pc_copy_match(
    uint8_t *out, uint16_t *op_p,
    const uint8_t *hist, uint16_t hist_len,
    uint16_t off, uint16_t match_len
) {
    uint16_t op = *op_p;
    uint16_t j;
    if (off <= op) {
        /* entirely within current block output */
        uint16_t src = (uint16_t)(op - off);
        for (j = 0; j < match_len; ++j) {
            out[op++] = out[src + j];
        }
    } else {
        /* starts in history, may cross into current output */
        uint16_t hist_back = (uint16_t)(off - op);
        uint16_t hist_start = (uint16_t)(hist_len - hist_back);
        for (j = 0; j < match_len; ++j) {
            uint16_t src = (uint16_t)(hist_start + j);
            if (src < hist_len) {
                out[op++] = hist[src];
            } else {
                out[op++] = out[src - hist_len];
            }
        }
    }
    *op_p = op;
}

static pc_result pc_decompress_block(
    const uint8_t *hist, uint16_t hist_len,
    const uint8_t *in, uint16_t in_len,
    uint8_t *out, uint16_t out_len
) {
    uint16_t ip = 0;
    uint16_t op = 0;
    uint16_t last_offset = 0;

    while (ip < in_len) {
        uint8_t token = in[ip++];

        /* 0x00..0x3F: short literal run (1..64) */
        if (token < 0x40u) {
            uint16_t lit_len = (uint16_t)((token & 0x3Fu) + 1u);
            if ((uint32_t)ip + lit_len > in_len || (uint32_t)op + lit_len > out_len) {
                return PC_ERR_CORRUPT;
            }
            memcpy(out + op, in + ip, lit_len);
            ip = (uint16_t)(ip + lit_len);
            op = (uint16_t)(op + lit_len);
            continue;
        }

        /* 0x40..0x7F: dictionary reference (0..63) */
        if (token < 0x80u) {
#if PC_DICT_COUNT > 0
            uint16_t idx = (uint16_t)(token & 0x3Fu);
            uint8_t dlen;
            if (idx >= PC_DICT_COUNT) return PC_ERR_CORRUPT;
            dlen = pc_static_dict[idx].len;
            if ((uint32_t)op + dlen > out_len) return PC_ERR_CORRUPT;
            memcpy(out + op, pc_static_dict[idx].data, dlen);
            op = (uint16_t)(op + dlen);
            continue;
#else
            return PC_ERR_CORRUPT; /* no dictionary compiled in */
#endif
        }

        /* 0x80..0xBF: LZ match with explicit offset */
        if (token < 0xC0u) {
            uint16_t match_len, off;
            if (ip >= in_len) return PC_ERR_CORRUPT;
            match_len = (uint16_t)(((token >> 1u) & 0x1Fu) + PC_MATCH_MIN);
            off = (uint16_t)(((uint16_t)(token & 0x01u) << 8u) | (uint16_t)in[ip++]);

            if (off == 0u) return PC_ERR_CORRUPT;
            if (off > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;

            pc_copy_match(out, &op, hist, hist_len, off, match_len);
            last_offset = off;
            continue;
        }

        /* 0xC0..0xCF: repeat-offset match (4-bit length) */
        if (token < 0xD0u) {
            uint16_t match_len = (uint16_t)((token & 0x0Fu) + PC_MATCH_MIN);
            if (last_offset == 0u) return PC_ERR_CORRUPT;
            if (last_offset > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;
            pc_copy_match(out, &op, hist, hist_len, last_offset, match_len);
            continue;
        }

        /* 0xD0..0xDF: dictionary keywords (entries 80..95) */
        if (token < 0xE0u) {
#if PC_DICT_COUNT > 80
            uint16_t idx = (uint16_t)(80u + (token & 0x0Fu));
            uint8_t dlen;
            if (idx >= PC_DICT_COUNT) return PC_ERR_CORRUPT;
            dlen = pc_static_dict[idx].len;
            if ((uint32_t)op + dlen > out_len) return PC_ERR_CORRUPT;
            memcpy(out + op, pc_static_dict[idx].data, dlen);
            op = (uint16_t)(op + dlen);
            continue;
#else
            return PC_ERR_CORRUPT;
#endif
        }

        /* 0xE0..0xEF: dictionary overflow (entries 64..79) */
        if (token < 0xF0u) {
#if PC_DICT_COUNT > 64
            uint16_t idx = (uint16_t)(64u + (token & 0x0Fu));
            uint8_t dlen;
            if (idx >= PC_DICT_COUNT) return PC_ERR_CORRUPT;
            dlen = pc_static_dict[idx].len;
            if ((uint32_t)op + dlen > out_len) return PC_ERR_CORRUPT;
            memcpy(out + op, pc_static_dict[idx].data, dlen);
            op = (uint16_t)(op + dlen);
            continue;
#else
            return PC_ERR_CORRUPT;
#endif
        }

        /* 0xF0..0xFF: long-offset LZ match (3-byte token) */
        {
            uint16_t match_len = (uint16_t)((token & 0x0Fu) + PC_LONG_MATCH_MIN);
            uint16_t off;
            if ((uint32_t)ip + 2u > in_len) return PC_ERR_CORRUPT;
            off = (uint16_t)(((uint16_t)in[ip] << 8u) | (uint16_t)in[ip + 1u]);
            ip = (uint16_t)(ip + 2u);

            if (off == 0u) return PC_ERR_CORRUPT;
            if (off > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;

            pc_copy_match(out, &op, hist, hist_len, off, match_len);
            last_offset = off;
        }
    }

    if (op != out_len) {
        return PC_ERR_CORRUPT;
    }
    return PC_OK;
}

static pc_result pc_write_all(pc_write_fn write_fn, void *user, const uint8_t *data, size_t len) {
    if (len == 0) {
        return PC_OK;
    }
    if (write_fn == NULL) {
        return PC_ERR_INPUT;
    }
    return write_fn(user, data, len) == 0 ? PC_OK : PC_ERR_WRITE;
}

static void pc_update_history(uint8_t *hist, uint16_t *hist_len, const uint8_t *data, uint16_t len) {
    if (len >= (uint16_t)PC_HISTORY_SIZE) {
        memcpy(hist, data + len - (uint16_t)PC_HISTORY_SIZE, PC_HISTORY_SIZE);
        *hist_len = (uint16_t)PC_HISTORY_SIZE;
    } else if ((uint16_t)(*hist_len + len) <= (uint16_t)PC_HISTORY_SIZE) {
        memcpy(hist + *hist_len, data, len);
        *hist_len = (uint16_t)(*hist_len + len);
    } else {
        uint16_t keep = (uint16_t)(PC_HISTORY_SIZE - len);
        if (keep > *hist_len) keep = *hist_len;
        memmove(hist, hist + *hist_len - keep, keep);
        memcpy(hist + keep, data, len);
        *hist_len = (uint16_t)(keep + len);
    }
}

static pc_result pc_encoder_flush(pc_encoder *enc, pc_write_fn write_fn, void *user) {
    uint8_t combined[PC_HISTORY_SIZE + PC_BLOCK_SIZE];
    uint8_t tmp[PC_BLOCK_MAX_COMPRESSED];
    uint8_t header[4];
    uint16_t raw_len;
    uint16_t hist_len;
    uint16_t comp_len;
    pc_result rc;

    if (enc->block_len == 0u) {
        return PC_OK;
    }

    raw_len = enc->block_len;
    hist_len = enc->history_len;

    memcpy(combined, enc->history, hist_len);
    memcpy(combined + hist_len, enc->block, raw_len);

    comp_len = pc_compress_block(combined, hist_len, raw_len, tmp, (uint16_t)sizeof(tmp)
#ifdef PC_ENABLE_STATS
        , &enc->stats
#endif
    );

    /* update history for next block */
    pc_update_history(enc->history, &enc->history_len, enc->block, raw_len);

    PC_STAT_ADD(&enc->stats, bytes_in, raw_len);
    PC_STAT_INC(&enc->stats, blocks);

    header[0] = (uint8_t)(raw_len & 0xFFu);
    header[1] = (uint8_t)(raw_len >> 8u);

    if (comp_len == UINT16_MAX || comp_len >= raw_len) {
        header[2] = 0u;
        header[3] = 0u;
        rc = pc_write_all(write_fn, user, header, sizeof(header));
        if (rc != PC_OK) {
            return rc;
        }
        rc = pc_write_all(write_fn, user, enc->block, raw_len);
        if (rc != PC_OK) {
            return rc;
        }
        PC_STAT_ADD(&enc->stats, bytes_out, 4u + raw_len);
    } else {
        header[2] = (uint8_t)(comp_len & 0xFFu);
        header[3] = (uint8_t)(comp_len >> 8u);
        rc = pc_write_all(write_fn, user, header, sizeof(header));
        if (rc != PC_OK) {
            return rc;
        }
        rc = pc_write_all(write_fn, user, tmp, comp_len);
        if (rc != PC_OK) {
            return rc;
        }
        PC_STAT_ADD(&enc->stats, bytes_out, 4u + comp_len);
    }

    enc->block_len = 0u;
    return PC_OK;
}

void pc_encoder_init(pc_encoder *enc) {
    if (enc != NULL) {
        enc->block_len = 0u;
        enc->history_len = 0u;
#ifdef PC_ENABLE_STATS
        memset(&enc->stats, 0, sizeof(enc->stats));
#endif
    }
}

pc_result pc_encoder_sink(
    pc_encoder *enc,
    const uint8_t *data,
    size_t len,
    pc_write_fn write_fn,
    void *user
) {
    size_t pos = 0;

    if (enc == NULL || (len > 0u && data == NULL) || write_fn == NULL) {
        return PC_ERR_INPUT;
    }

    while (pos < len) {
        size_t room = (size_t)PC_BLOCK_SIZE - (size_t)enc->block_len;
        size_t take = len - pos;
        if (take > room) {
            take = room;
        }
        memcpy(enc->block + enc->block_len, data + pos, take);
        enc->block_len = (uint16_t)(enc->block_len + (uint16_t)take);
        pos += take;

        if (enc->block_len == (uint16_t)PC_BLOCK_SIZE) {
            pc_result rc = pc_encoder_flush(enc, write_fn, user);
            if (rc != PC_OK) {
                return rc;
            }
        }
    }

    return PC_OK;
}

pc_result pc_encoder_finish(pc_encoder *enc, pc_write_fn write_fn, void *user) {
    if (enc == NULL || write_fn == NULL) {
        return PC_ERR_INPUT;
    }
    return pc_encoder_flush(enc, write_fn, user);
}

#ifdef PC_ENABLE_STATS
void pc_encoder_get_stats(const pc_encoder *enc, pc_encoder_stats *out) {
    if (enc != NULL && out != NULL) {
        *out = enc->stats;
    }
}
#endif

void pc_decoder_init(pc_decoder *dec) {
    if (dec != NULL) {
        memset(dec, 0, sizeof(*dec));
    }
}

static pc_result pc_decoder_emit_block(pc_decoder *dec, pc_write_fn write_fn, void *user) {
    if (dec->comp_len == 0u) {
        pc_result rc = pc_write_all(write_fn, user, dec->payload, dec->raw_len);
        if (rc == PC_OK) {
            pc_update_history(dec->history, &dec->history_len, dec->payload, dec->raw_len);
        }
        return rc;
    }
    {
        pc_result rc = pc_decompress_block(
            dec->history, dec->history_len,
            dec->payload, dec->comp_len,
            dec->raw, dec->raw_len);
        if (rc != PC_OK) {
            return rc;
        }
        rc = pc_write_all(write_fn, user, dec->raw, dec->raw_len);
        if (rc == PC_OK) {
            pc_update_history(dec->history, &dec->history_len, dec->raw, dec->raw_len);
        }
        return rc;
    }
}

pc_result pc_decoder_sink(
    pc_decoder *dec,
    const uint8_t *data,
    size_t len,
    pc_write_fn write_fn,
    void *user
) {
    size_t pos = 0;

    if (dec == NULL || (len > 0u && data == NULL) || write_fn == NULL) {
        return PC_ERR_INPUT;
    }

    while (pos < len) {
        if (dec->header_len < 4u) {
            size_t need = 4u - (size_t)dec->header_len;
            size_t take = len - pos;
            if (take > need) {
                take = need;
            }
            memcpy(dec->header + dec->header_len, data + pos, take);
            dec->header_len = (uint8_t)(dec->header_len + (uint8_t)take);
            pos += take;

            if (dec->header_len < 4u) {
                continue;
            }

            dec->raw_len = (uint16_t)(dec->header[0] | ((uint16_t)dec->header[1] << 8u));
            dec->comp_len = (uint16_t)(dec->header[2] | ((uint16_t)dec->header[3] << 8u));
            dec->payload_len = 0u;

            if (dec->raw_len == 0u && dec->comp_len == 0u) {
                dec->header_len = 0u;
                continue;
            }
            if (dec->raw_len == 0u || dec->raw_len > (uint16_t)PC_BLOCK_SIZE) {
                return PC_ERR_CORRUPT;
            }
            if (dec->comp_len > 0u && dec->comp_len > (uint16_t)PC_BLOCK_MAX_COMPRESSED) {
                return PC_ERR_CORRUPT;
            }
        }

        {
            uint16_t target = dec->comp_len == 0u ? dec->raw_len : dec->comp_len;
            size_t need = (size_t)(target - dec->payload_len);
            size_t take = len - pos;
            if (take > need) {
                take = need;
            }
            memcpy(dec->payload + dec->payload_len, data + pos, take);
            dec->payload_len = (uint16_t)(dec->payload_len + (uint16_t)take);
            pos += take;

            if (dec->payload_len == target) {
                pc_result rc = pc_decoder_emit_block(dec, write_fn, user);
                if (rc != PC_OK) {
                    return rc;
                }
                dec->header_len = 0u;
                dec->raw_len = 0u;
                dec->comp_len = 0u;
                dec->payload_len = 0u;
            }
        }
    }

    return PC_OK;
}

pc_result pc_decoder_finish(pc_decoder *dec) {
    if (dec == NULL) {
        return PC_ERR_INPUT;
    }
    if (dec->header_len != 0u || dec->raw_len != 0u || dec->comp_len != 0u || dec->payload_len != 0u) {
        return PC_ERR_CORRUPT;
    }
    return PC_OK;
}

size_t pc_compress_bound(size_t input_len) {
    size_t blocks;
    if (input_len == 0u) {
        return 0u;
    }
    blocks = (input_len + (size_t)PC_BLOCK_SIZE - 1u) / (size_t)PC_BLOCK_SIZE;
    return input_len + (blocks * 4u);
}

typedef struct pc_mem_writer {
    uint8_t *out;
    size_t cap;
    size_t len;
} pc_mem_writer;

static int pc_mem_write(void *user, const uint8_t *data, size_t len) {
    pc_mem_writer *w = (pc_mem_writer *)user;
    if (w->len + len > w->cap) {
        return 1;
    }
    memcpy(w->out + w->len, data, len);
    w->len += len;
    return 0;
}

pc_result pc_compress_buffer(
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_cap,
    size_t *output_len
) {
    pc_encoder enc;
    pc_mem_writer writer;
    pc_result rc;

    if ((input_len > 0u && input == NULL) || output == NULL || output_len == NULL) {
        return PC_ERR_INPUT;
    }

    writer.out = output;
    writer.cap = output_cap;
    writer.len = 0u;

    pc_encoder_init(&enc);
    rc = pc_encoder_sink(&enc, input, input_len, pc_mem_write, &writer);
    if (rc != PC_OK) {
        return rc == PC_ERR_WRITE ? PC_ERR_OUTPUT_TOO_SMALL : rc;
    }
    rc = pc_encoder_finish(&enc, pc_mem_write, &writer);
    if (rc != PC_OK) {
        return rc == PC_ERR_WRITE ? PC_ERR_OUTPUT_TOO_SMALL : rc;
    }

    *output_len = writer.len;
    return PC_OK;
}

pc_result pc_decompress_buffer(
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_cap,
    size_t *output_len
) {
    pc_decoder dec;
    pc_mem_writer writer;
    pc_result rc;

    if ((input_len > 0u && input == NULL) || output == NULL || output_len == NULL) {
        return PC_ERR_INPUT;
    }

    writer.out = output;
    writer.cap = output_cap;
    writer.len = 0u;

    pc_decoder_init(&dec);
    rc = pc_decoder_sink(&dec, input, input_len, pc_mem_write, &writer);
    if (rc != PC_OK) {
        return rc == PC_ERR_WRITE ? PC_ERR_OUTPUT_TOO_SMALL : rc;
    }
    rc = pc_decoder_finish(&dec);
    if (rc != PC_OK) {
        return rc;
    }

    *output_len = writer.len;
    return PC_OK;
}
