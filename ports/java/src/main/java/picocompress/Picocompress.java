package picocompress;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.Arrays;

/**
 * Pure-Java port of the picocompress block-based LZ compression library.
 * Produces byte-identical output to the C reference implementation (portable hash path).
 */
public final class Picocompress {

    private Picocompress() {}

    // ---- Constants (match C reference) ----
    private static final int LITERAL_MAX = 64;
    private static final int MATCH_MIN = 2;
    private static final int MATCH_MAX = MATCH_MIN + ((1 << 5) - 1); // 33
    private static final int OFFSET_SHORT_MAX = (1 << 9) - 1;        // 511
    private static final int LONG_MATCH_MIN = 2;
    private static final int LONG_MATCH_MAX = 17;
    private static final int OFFSET_LONG_MAX = 65535;
    private static final int DICT_COUNT = 96;
    private static final int GOOD_MATCH = 8;
    private static final int REPEAT_CACHE_SIZE = 3;
    private static final int DECODER_HISTORY_MAX = 65535;

    // ---- Options (configurable encoder profiles) ----

    /**
     * Encoder configuration. Each profile trades RAM/CPU for compression ratio.
     * The decoder is profile-independent — any compressed stream can be decompressed
     * regardless of the profile used to produce it.
     */
    public static final class Options {
        /** Block size in bytes (1..511). */
        public final int blockSize;
        /** Hash table width = 2^hashBits. */
        public final int hashBits;
        /** Number of entries per hash bucket. */
        public final int hashChainDepth;
        /** Cross-block history buffer size in bytes. */
        public final int historySize;
        /** Lazy-match lookahead steps (0 = disabled). */
        public final int lazySteps;

        public Options(int blockSize, int hashBits, int hashChainDepth,
                       int historySize, int lazySteps) {
            if (blockSize < 1 || blockSize > OFFSET_SHORT_MAX)
                throw new IllegalArgumentException(
                    "blockSize must be in [1, " + OFFSET_SHORT_MAX + "]");
            if (hashBits < 1 || hashBits > 16)
                throw new IllegalArgumentException("hashBits must be in [1, 16]");
            if (hashChainDepth < 1)
                throw new IllegalArgumentException("hashChainDepth must be >= 1");
            if (historySize < 0)
                throw new IllegalArgumentException("historySize must be >= 0");
            if (lazySteps < 0)
                throw new IllegalArgumentException("lazySteps must be >= 0");
            this.blockSize = blockSize;
            this.hashBits = hashBits;
            this.hashChainDepth = hashChainDepth;
            this.historySize = historySize;
            this.lazySteps = lazySteps;
        }

        /** Balanced — default profile (508/512×2/504/lazy1). ~4.6 KB encode. */
        public static final Options DEFAULT = new Options(508, 9, 2, 504, 1);
        /** Micro — smallest RAM footprint (192/256×1/64/lazy1). ~1.0 KB encode. */
        public static final Options MICRO = new Options(192, 8, 1, 64, 1);
        /** Minimal — (508/256×1/128/lazy1). ~1.8 KB encode. */
        public static final Options MINIMAL = new Options(508, 8, 1, 128, 1);
        /** Balanced — alias for {@link #DEFAULT}. */
        public static final Options BALANCED = DEFAULT;
        /** Aggressive — deeper hash chains (508/256×4/504/lazy1). ~4.6 KB encode. */
        public static final Options AGGRESSIVE = new Options(508, 8, 4, 504, 1);
        /** Q3 — larger history (508/1024×2/1024/lazy2). ~7.7 KB encode. */
        public static final Options Q3 = new Options(508, 10, 2, 1024, 2);
        /** Q4 — maximum reach (508/2048×2/2048/lazy2). ~13.8 KB encode. */
        public static final Options Q4 = new Options(508, 11, 2, 2048, 2);
    }

    // ---- Static dictionary (96 entries — identical to C reference) ----

    private static final byte[][] DICT = {
        /* 0  */ {'"', ':', ' ', '"'},
        /* 1  */ {'}', ',', '\n', '"'},
        /* 2  */ {'<', '/', 'd', 'i', 'v'},
        /* 3  */ {'t', 'i', 'o', 'n'},
        /* 4  */ {'m', 'e', 'n', 't'},
        /* 5  */ {'n', 'e', 's', 's'},
        /* 6  */ {'a', 'b', 'l', 'e'},
        /* 7  */ {'i', 'g', 'h', 't'},
        /* 8  */ {'"', ':', '"'},
        /* 9  */ {'<', '/', 'd', 'i'},
        /* 10 */ {'=', '"', 'h', 't'},
        /* 11 */ {'t', 'h', 'e'},
        /* 12 */ {'i', 'n', 'g'},
        /* 13 */ {',', '"', ','},
        /* 14 */ {'"', ':', '{'},
        /* 15 */ {'"', ':', '['},
        /* 16 */ {'i', 'o', 'n'},
        /* 17 */ {'e', 'n', 't'},
        /* 18 */ {'t', 'e', 'r'},
        /* 19 */ {'a', 'n', 'd'},
        /* 20 */ {'/', '>', '\r', '\n'},
        /* 21 */ {'"', '}', ','},
        /* 22 */ {'"', ']', ','},
        /* 23 */ {'h', 'a', 'v', 'e'},
        /* 24 */ {'n', 'o', '"', ':'},
        /* 25 */ {'t', 'r', 'u', 'e'},
        /* 26 */ {'n', 'u', 'l', 'l'},
        /* 27 */ {'n', 'a', 'm', 'e'},
        /* 28 */ {'d', 'a', 't', 'a'},
        /* 29 */ {'t', 'i', 'm', 'e'},
        /* 30 */ {'t', 'y', 'p', 'e'},
        /* 31 */ {'m', 'o', 'd', 'e'},
        /* 32 */ {'h', 't', 't', 'p'},
        /* 33 */ {'t', 'i', 'o', 'n'},
        /* 34 */ {'c', 'o', 'd', 'e'},
        /* 35 */ {'s', 'i', 'z', 'e'},
        /* 36 */ {'m', 'e', 'n', 't'},
        /* 37 */ {'l', 'i', 's', 't'},
        /* 38 */ {'i', 't', 'e', 'm'},
        /* 39 */ {'t', 'e', 'x', 't'},
        /* 40 */ {'f', 'a', 'l', 's', 'e'},
        /* 41 */ {'e', 'r', 'r', 'o', 'r'},
        /* 42 */ {'v', 'a', 'l', 'u', 'e'},
        /* 43 */ {'s', 't', 'a', 't', 'e'},
        /* 44 */ {'a', 'l', 'e', 'r', 't'},
        /* 45 */ {'i', 'n', 'p', 'u', 't'},
        /* 46 */ {'a', 't', 'i', 'o', 'n'},
        /* 47 */ {'o', 'r', 'd', 'e', 'r'},
        /* 48 */ {'s', 't', 'a', 't', 'u', 's'},
        /* 49 */ {'n', 'u', 'm', 'b', 'e', 'r'},
        /* 50 */ {'a', 'c', 't', 'i', 'v', 'e'},
        /* 51 */ {'d', 'e', 'v', 'i', 'c', 'e'},
        /* 52 */ {'r', 'e', 'g', 'i', 'o', 'n'},
        /* 53 */ {'s', 't', 'r', 'i', 'n', 'g'},
        /* 54 */ {'r', 'e', 's', 'u', 'l', 't'},
        /* 55 */ {'l', 'e', 'n', 'g', 't', 'h'},
        /* 56 */ {'m', 'e', 's', 's', 'a', 'g', 'e'},
        /* 57 */ {'c', 'o', 'n', 't', 'e', 'n', 't'},
        /* 58 */ {'r', 'e', 'q', 'u', 'e', 's', 't'},
        /* 59 */ {'d', 'e', 'f', 'a', 'u', 'l', 't'},
        /* 60 */ {'n', 'u', 'm', 'b', 'e', 'r', '"', ':'},
        /* 61 */ {'o', 'p', 'e', 'r', 'a', 't', 'o', 'r'},
        /* 62 */ {'h', 't', 't', 'p', 's', ':', '/', '/'},
        /* 63 */ {'r', 'e', 's', 'p', 'o', 'n', 's', 'e'},
        /* 64 */ {'.', ' ', 'T', 'h', 'e', ' '},
        /* 65 */ {'.', ' ', 'I', 't', ' '},
        /* 66 */ {'.', ' ', 'T', 'h', 'i', 's', ' '},
        /* 67 */ {'.', ' ', 'A', ' '},
        /* 68 */ {'H', 'T', 'T', 'P'},
        /* 69 */ {'J', 'S', 'O', 'N'},
        /* 70 */ {'T', 'h', 'e', ' '},
        /* 71 */ {'N', 'o', 'n', 'e'},
        /* 72 */ {'m', 'e', 'n', 't'},
        /* 73 */ {'n', 'e', 's', 's'},
        /* 74 */ {'a', 'b', 'l', 'e'},
        /* 75 */ {'i', 'g', 'h', 't'},
        /* 76 */ {'a', 't', 'i', 'o', 'n'},
        /* 77 */ {'o', 'u', 'l', 'd', ' '},
        /* 78 */ {'"', ':', ' ', '"'},
        /* 79 */ {'"', ',', ' ', '"'},
        /* 80 */ {'D', 'I', 'M'},
        /* 81 */ {'F', 'O', 'R'},
        /* 82 */ {'E', 'N', 'D'},
        /* 83 */ {'R', 'E', 'L'},
        /* 84 */ {'E', 'A', 'C', 'H'},
        /* 85 */ {'L', 'O', 'A', 'D'},
        /* 86 */ {'S', 'A', 'V', 'E'},
        /* 87 */ {'C', 'A', 'R', 'D'},
        /* 88 */ {'J', 'U', 'M', 'P'},
        /* 89 */ {'P', 'R', 'I', 'N', 'T'},
        /* 90 */ {'I', 'N', 'P', 'U', 'T'},
        /* 91 */ {'G', 'O', 'S', 'U', 'B'},
        /* 92 */ {'S', 'T', 'R', 'E', 'A', 'M'},
        /* 93 */ {'R', 'E', 'T', 'U', 'R', 'N'},
        /* 94 */ {'S', 'W', 'I', 'T', 'C', 'H'},
        /* 95 */ {'P', 'R', 'O', 'G', 'R', 'A', 'M'},
    };

    // ---- Public API ----

    /**
     * Compress input using the default Balanced profile.
     *
     * @param input raw data to compress
     * @return compressed byte array (block-framed)
     */
    public static byte[] compress(byte[] input) {
        return compress(input, Options.DEFAULT);
    }

    /**
     * Compress input using the specified profile.
     *
     * @param input   raw data to compress
     * @param options encoder configuration profile
     * @return compressed byte array (block-framed)
     */
    public static byte[] compress(byte[] input, Options options) {
        if (input == null) throw new NullPointerException("input");
        if (options == null) throw new NullPointerException("options");
        if (input.length == 0) return new byte[0];

        int blockSize = options.blockSize;
        int historySize = options.historySize;
        int blockMaxCompressed = blockSize + (blockSize / LITERAL_MAX) + 16;

        ByteArrayOutputStream out = new ByteArrayOutputStream(
            input.length + ((input.length / blockSize) + 1) * 4);

        byte[] history = new byte[historySize];
        int histLen = 0;
        byte[] tmp = new byte[blockMaxCompressed];

        int pos = 0;
        while (pos < input.length) {
            int rawLen = Math.min(blockSize, input.length - pos);

            // build virtual buffer: [history | block]
            byte[] vbuf = new byte[histLen + rawLen];
            System.arraycopy(history, 0, vbuf, 0, histLen);
            System.arraycopy(input, pos, vbuf, histLen, rawLen);

            int compLen = compressBlock(vbuf, histLen, rawLen, tmp, tmp.length, options);

            // update history for next block
            histLen = updateHistory(history, histLen, historySize, input, pos, rawLen);

            // write 4-byte LE header
            if (compLen < 0 || compLen >= rawLen) {
                // raw fallback
                writeLE16(out, rawLen);
                writeLE16(out, 0);
                out.write(input, pos, rawLen);
            } else {
                writeLE16(out, rawLen);
                writeLE16(out, compLen);
                out.write(tmp, 0, compLen);
            }

            pos += rawLen;
        }

        return out.toByteArray();
    }

    /**
     * Decompress a picocompress-encoded byte array.
     *
     * @param compressed block-framed compressed data
     * @return original uncompressed data
     * @throws IllegalArgumentException if the data is corrupt or truncated
     */
    public static byte[] decompress(byte[] compressed) {
        if (compressed == null) throw new NullPointerException("compressed");
        if (compressed.length == 0) return new byte[0];

        ByteArrayOutputStream out = new ByteArrayOutputStream(compressed.length * 2);
        byte[] history = new byte[DECODER_HISTORY_MAX];
        int histLen = 0;

        int pos = 0;
        while (pos < compressed.length) {
            // read 4-byte header
            if (pos + 4 > compressed.length)
                throw new IllegalArgumentException("Truncated block header at offset " + pos);

            int rawLen = (compressed[pos] & 0xFF) | ((compressed[pos + 1] & 0xFF) << 8);
            int compLen = (compressed[pos + 2] & 0xFF) | ((compressed[pos + 3] & 0xFF) << 8);
            pos += 4;

            if (rawLen == 0 && compLen == 0) continue;
            if (rawLen == 0)
                throw new IllegalArgumentException("Invalid block: raw_len=0 with comp_len=" + compLen);

            byte[] raw;
            if (compLen == 0) {
                // stored raw
                if (pos + rawLen > compressed.length)
                    throw new IllegalArgumentException("Truncated raw block at offset " + pos);
                raw = new byte[rawLen];
                System.arraycopy(compressed, pos, raw, 0, rawLen);
                pos += rawLen;
            } else {
                // compressed
                if (pos + compLen > compressed.length)
                    throw new IllegalArgumentException(
                        "Truncated compressed block at offset " + pos);
                raw = decompressBlock(history, histLen, compressed, pos, compLen, rawLen);
                pos += compLen;
            }

            out.write(raw, 0, raw.length);

            // update decoder history
            histLen = updateHistory(history, histLen, DECODER_HISTORY_MAX,
                                    raw, 0, raw.length);
        }

        return out.toByteArray();
    }

    // ---- Internal: hash function (portable, matches C reference) ----

    private static int hash3(byte[] buf, int pos, int hashMask) {
        return (((buf[pos] & 0xFF) * 251)
              + ((buf[pos + 1] & 0xFF) * 11)
              + ((buf[pos + 2] & 0xFF) * 3)) & hashMask;
    }

    // ---- Internal: match length ----

    private static int matchLen(byte[] buf, int a, int b, int limit) {
        int m = 0;
        while (m < limit && buf[a + m] == buf[b + m]) m++;
        return m;
    }

    // ---- Internal: hash chain insert ----

    private static void headInsert(short[][] head, int depth, int hash, int pos) {
        for (int d = depth - 1; d > 0; d--) {
            head[d][hash] = head[d - 1][hash];
        }
        head[0][hash] = (short) pos;
    }

    // ---- Internal: emit literal runs ----

    /** Emits literal bytes into dst. Returns false on overflow. op[0] is updated. */
    private static boolean emitLiterals(byte[] src, int srcOff, int srcLen,
                                        byte[] dst, int dstCap, int[] op) {
        int p = 0;
        while (p < srcLen) {
            int chunk = Math.min(srcLen - p, LITERAL_MAX);
            if (op[0] + 1 + chunk > dstCap) return false;
            dst[op[0]++] = (byte) (chunk - 1); // 0x00..0x3F
            System.arraycopy(src, srcOff + p, dst, op[0], chunk);
            op[0] += chunk;
            p += chunk;
        }
        return true;
    }

    // ---- Internal: arrays comparison ----

    private static boolean regionEquals(byte[] a, int aOff, byte[] b, int bOff, int len) {
        for (int i = 0; i < len; i++) {
            if (a[aOff + i] != b[bOff + i]) return false;
        }
        return true;
    }

    // ---- Internal: find best match at position ----

    // result indices
    private static final int R_SAVINGS = 0;
    private static final int R_LEN = 1;
    private static final int R_OFF = 2;
    private static final int R_DICT = 3;
    private static final int R_IS_REPEAT = 4;

    /**
     * Finds the best match (repeat-cache → dictionary → LZ hash chain) at vpos.
     * Fills result[0..4] = {savings, len, off, dictIndex, isRepeat}.
     */
    private static void findBest(
            byte[] vbuf, int vbufLen, int vpos,
            short[][] head, int depth, int hashMask,
            int[] repOffsets,
            boolean skipDict,
            int[] result) {

        int bestSavings = 0;
        int bestLen = 0;
        int bestOff = 0;
        int bestDict = -1;
        int bestIsRepeat = 0;
        int remaining = vbufLen - vpos;

        // 1. Repeat-offset cache
        if (remaining >= MATCH_MIN) {
            int maxRep = Math.min(remaining, MATCH_MAX);
            for (int d = 0; d < REPEAT_CACHE_SIZE; d++) {
                int off = repOffsets[d];
                if (off == 0 || off > vpos) continue;
                // early reject: first byte
                if (vbuf[vpos] != vbuf[vpos - off]) continue;
                // early reject: second byte
                if (remaining >= 2 && vbuf[vpos + 1] != vbuf[vpos - off + 1]) continue;
                int len = matchLen(vbuf, vpos - off, vpos, maxRep);
                if (len < MATCH_MIN) continue;

                boolean isRep = (d == 0 && len <= 17);
                int tokenCost = isRep ? 1 : (off <= OFFSET_SHORT_MAX ? 2 : 3);
                int s = len - tokenCost;

                if (s > bestSavings) {
                    bestSavings = s;
                    bestLen = len;
                    bestOff = off;
                    bestDict = -1;
                    bestIsRepeat = isRep ? 1 : 0;
                    if (len >= GOOD_MATCH) break; // good enough
                }
            }
        }

        // 2. Dictionary match
        if (!skipDict && bestLen < GOOD_MATCH) {
            int firstByte = vbuf[vpos] & 0xFF;
            for (int d = 0; d < DICT_COUNT; d++) {
                byte[] entry = DICT[d];
                int dlen = entry.length;
                if (dlen > remaining) continue;
                if (dlen - 1 <= bestSavings) continue;
                if ((entry[0] & 0xFF) != firstByte) continue;
                if (!regionEquals(vbuf, vpos, entry, 0, dlen)) continue;
                bestSavings = dlen - 1;
                bestDict = d;
                bestLen = dlen;
                bestOff = 0;
                bestIsRepeat = 0;
                if (dlen >= GOOD_MATCH) break; // good enough
            }
        }

        // 3. LZ hash-chain match
        if (remaining >= 3 && bestLen < GOOD_MATCH) {
            int hash = hash3(vbuf, vpos, hashMask);
            int maxLenShort = Math.min(remaining, MATCH_MAX);
            int maxLenLong = Math.min(remaining, LONG_MATCH_MAX);
            int firstByte = vbuf[vpos] & 0xFF;

            for (int d = 0; d < depth; d++) {
                int prev = head[d][hash];
                if (prev < 0) continue;
                if (prev >= vpos) continue;
                int off = vpos - prev;
                if (off == 0 || off > OFFSET_LONG_MAX) continue;

                // early reject: first byte
                if ((vbuf[prev] & 0xFF) != firstByte) continue;

                int maxLen = (off <= OFFSET_SHORT_MAX) ? maxLenShort : maxLenLong;
                int len = matchLen(vbuf, prev, vpos, maxLen);
                if (len < MATCH_MIN) continue;

                int tokenCost = (off <= OFFSET_SHORT_MAX) ? 2 : 3;
                int s = len - tokenCost;

                // offset scoring: prefer nearer, length bonus for long-offset
                if (s > bestSavings
                    || (s == bestSavings && len > bestLen)
                    || (s == bestSavings && len == bestLen && off < bestOff)
                    || (s == bestSavings - 1 && len >= bestLen + 2)) {
                    bestSavings = len - tokenCost;
                    bestLen = len;
                    bestOff = off;
                    bestDict = -1;
                    bestIsRepeat = 0;
                    if (len >= GOOD_MATCH) break; // good enough
                }
            }
        }

        result[R_SAVINGS] = bestSavings;
        result[R_LEN] = bestLen;
        result[R_OFF] = bestOff;
        result[R_DICT] = bestDict;
        result[R_IS_REPEAT] = bestIsRepeat;
    }

    // ---- Internal: compress one block ----

    /**
     * Compresses blockLen bytes from vbuf starting at offset histLen.
     * vbuf = [history(histLen) | block(blockLen)].
     * Returns compressed size, or -1 on overflow.
     */
    private static int compressBlock(byte[] vbuf, int histLen, int blockLen,
                                     byte[] out, int outCap, Options opts) {
        int hashSize = 1 << opts.hashBits;
        int hashMask = hashSize - 1;
        int depth = opts.hashChainDepth;
        int lazySteps = opts.lazySteps;

        short[][] head = new short[depth][hashSize];
        for (int d = 0; d < depth; d++) Arrays.fill(head[d], (short) -1);

        int[] repOffsets = new int[REPEAT_CACHE_SIZE];
        int vbufLen = histLen + blockLen;
        int[] op = {0};

        // seed hash table from history
        if (histLen >= 3) {
            for (int p = 0; p + 2 < histLen; p++) {
                headInsert(head, depth, hash3(vbuf, p, hashMask), p);
            }
            // boundary-boost: re-inject last 64 history positions into slot 0
            int tailStart = histLen > 64 ? histLen - 64 : 0;
            for (int p = tailStart; p + 2 < histLen; p++) {
                int h = hash3(vbuf, p, hashMask);
                if (head[0][h] != (short) p) {
                    short save = head[depth - 1][h];
                    headInsert(head, depth, h, p);
                    head[depth - 1][h] = save;
                }
            }
        }

        int anchor = histLen;
        int vpos = histLen;

        // dictionary self-disabling: skip dict for binary data
        boolean dictSkip = false;
        if (blockLen >= 1) {
            int b0 = vbuf[histLen] & 0xFF;
            if (b0 == '{' || b0 == '[' || b0 == '<' || b0 == 0xEF) {
                dictSkip = false;
            } else {
                int checkLen = Math.min(blockLen, 4);
                for (int ci = 0; ci < checkLen; ci++) {
                    int c = vbuf[histLen + ci] & 0xFF;
                    if (c < 0x20 || c > 0x7E) {
                        dictSkip = true;
                        break;
                    }
                }
            }
        }

        int[] result = new int[5];

        while (vpos < vbufLen) {
            // retry loop (implements C "goto retry_pos")
            boolean retrying = true;
            while (retrying) {
                retrying = false;

                if (vbufLen - vpos < MATCH_MIN) {
                    vpos = vbufLen; // force outer break
                    break;
                }

                findBest(vbuf, vbufLen, vpos, head, depth, hashMask,
                         repOffsets, dictSkip, result);
                int bestSavings = result[R_SAVINGS];
                int bestLen = result[R_LEN];

                // insert current position into hash table
                if (vbufLen - vpos >= 3) {
                    headInsert(head, depth, hash3(vbuf, vpos, hashMask), vpos);
                }

                // literal run extension: skip weak matches mid-run
                if (bestSavings <= 1 && result[R_DICT] < 0 && anchor < vpos) {
                    result[R_SAVINGS] = 0;
                }

                // lazy matching: only if current match is short
                if (result[R_SAVINGS] > 0 && bestLen < GOOD_MATCH) {
                    boolean jumped = false;
                    for (int step = 1; step <= lazySteps; step++) {
                        int npos = vpos + step;
                        if (npos >= vbufLen || vbufLen - npos < MATCH_MIN) break;
                        int[] nResult = new int[5];
                        findBest(vbuf, vbufLen, npos, head, depth, hashMask,
                                 repOffsets, dictSkip, nResult);
                        if (nResult[R_SAVINGS] > result[R_SAVINGS]) {
                            // insert skipped positions
                            for (int s = 0; s < step; s++) {
                                int sp = vpos + s;
                                if (vbufLen - sp >= 3)
                                    headInsert(head, depth,
                                               hash3(vbuf, sp, hashMask), sp);
                            }
                            vpos = npos;
                            jumped = true;
                            break;
                        }
                    }
                    if (jumped) {
                        retrying = true;
                        continue;
                    }
                }
            }

            if (vpos >= vbufLen) break;

            int bestSavings = result[R_SAVINGS];
            int bestLen = result[R_LEN];
            int bestOff = result[R_OFF];
            int bestDict = result[R_DICT];
            int bestIsRepeat = result[R_IS_REPEAT];

            // emit
            if (bestSavings > 0) {
                int litLen = vpos - anchor;

                // emit pending literals
                if (!emitLiterals(vbuf, anchor, litLen, out, outCap, op))
                    return -1;

                if (bestDict >= 0) {
                    // dictionary reference
                    if (op[0] + 1 > outCap) return -1;
                    if (bestDict < 64) {
                        out[op[0]++] = (byte) (0x40 | (bestDict & 0x3F));
                    } else if (bestDict < 80) {
                        out[op[0]++] = (byte) (0xE0 | ((bestDict - 64) & 0x0F));
                    } else {
                        out[op[0]++] = (byte) (0xD0 | ((bestDict - 80) & 0x0F));
                    }
                } else if (bestIsRepeat != 0) {
                    // repeat-offset match
                    if (op[0] + 1 > outCap) return -1;
                    out[op[0]++] = (byte) (0xC0 | ((bestLen - MATCH_MIN) & 0x0F));
                } else if (bestOff <= OFFSET_SHORT_MAX && bestLen <= MATCH_MAX) {
                    // short-offset LZ match (2 bytes)
                    if (op[0] + 2 > outCap) return -1;
                    out[op[0]++] = (byte) (0x80
                        | (((bestLen - MATCH_MIN) & 0x1F) << 1)
                        | ((bestOff >> 8) & 0x01));
                    out[op[0]++] = (byte) (bestOff & 0xFF);
                } else {
                    // long-offset LZ match (3 bytes)
                    int elen = Math.min(bestLen, LONG_MATCH_MAX);
                    if (op[0] + 3 > outCap) return -1;
                    out[op[0]++] = (byte) (0xF0 | ((elen - LONG_MATCH_MIN) & 0x0F));
                    out[op[0]++] = (byte) ((bestOff >> 8) & 0xFF);
                    out[op[0]++] = (byte) (bestOff & 0xFF);
                    bestLen = elen;
                }

                // update repeat-offset cache
                if (bestIsRepeat == 0 && bestOff != 0 && bestDict < 0) {
                    repOffsets[2] = repOffsets[1];
                    repOffsets[1] = repOffsets[0];
                    repOffsets[0] = bestOff;
                }

                // insert match positions into hash table
                for (int k = 1; k < bestLen && vpos + k + 2 < vbufLen; k++) {
                    headInsert(head, depth,
                               hash3(vbuf, vpos + k, hashMask), vpos + k);
                }

                vpos += bestLen;
                anchor = vpos;
            } else {
                vpos++;
            }
        }

        // flush remaining literals
        if (anchor < vbufLen) {
            if (!emitLiterals(vbuf, anchor, vbufLen - anchor, out, outCap, op))
                return -1;
        }

        return op[0];
    }

    // ---- Internal: decompress one block ----

    private static byte[] decompressBlock(byte[] hist, int histLen,
                                          byte[] in, int inOff, int inLen,
                                          int rawLen) {
        byte[] out = new byte[rawLen];
        int ip = 0;
        int op = 0;
        int lastOffset = 0;

        while (ip < inLen) {
            int token = in[inOff + ip++] & 0xFF;

            // 0x00..0x3F: short literal run (1..64)
            if (token < 0x40) {
                int litLen = (token & 0x3F) + 1;
                if (ip + litLen > inLen || op + litLen > rawLen)
                    throw new IllegalArgumentException("Corrupt: literal overflow");
                System.arraycopy(in, inOff + ip, out, op, litLen);
                ip += litLen;
                op += litLen;
                continue;
            }

            // 0x40..0x7F: dictionary reference (index 0..63)
            if (token < 0x80) {
                int idx = token & 0x3F;
                if (idx >= DICT_COUNT)
                    throw new IllegalArgumentException("Corrupt: dict index " + idx);
                byte[] entry = DICT[idx];
                if (op + entry.length > rawLen)
                    throw new IllegalArgumentException("Corrupt: dict output overflow");
                System.arraycopy(entry, 0, out, op, entry.length);
                op += entry.length;
                continue;
            }

            // 0x80..0xBF: short LZ match (2 bytes)
            if (token < 0xC0) {
                if (ip >= inLen)
                    throw new IllegalArgumentException("Corrupt: LZ truncated");
                int matchLength = ((token >> 1) & 0x1F) + MATCH_MIN;
                int off = ((token & 0x01) << 8) | (in[inOff + ip++] & 0xFF);
                if (off == 0)
                    throw new IllegalArgumentException("Corrupt: zero offset");
                if (off > op + histLen)
                    throw new IllegalArgumentException("Corrupt: offset exceeds history");
                if (op + matchLength > rawLen)
                    throw new IllegalArgumentException("Corrupt: match output overflow");
                copyMatch(out, op, hist, histLen, off, matchLength);
                op += matchLength;
                lastOffset = off;
                continue;
            }

            // 0xC0..0xCF: repeat-offset match (1 byte)
            if (token < 0xD0) {
                int matchLength = (token & 0x0F) + MATCH_MIN;
                if (lastOffset == 0)
                    throw new IllegalArgumentException("Corrupt: repeat with no prior offset");
                if (lastOffset > op + histLen)
                    throw new IllegalArgumentException("Corrupt: repeat offset exceeds history");
                if (op + matchLength > rawLen)
                    throw new IllegalArgumentException("Corrupt: repeat output overflow");
                copyMatch(out, op, hist, histLen, lastOffset, matchLength);
                op += matchLength;
                continue;
            }

            // 0xD0..0xDF: dictionary reference (index 80..95)
            if (token < 0xE0) {
                int idx = 80 + (token & 0x0F);
                if (idx >= DICT_COUNT)
                    throw new IllegalArgumentException("Corrupt: dict index " + idx);
                byte[] entry = DICT[idx];
                if (op + entry.length > rawLen)
                    throw new IllegalArgumentException("Corrupt: dict output overflow");
                System.arraycopy(entry, 0, out, op, entry.length);
                op += entry.length;
                continue;
            }

            // 0xE0..0xEF: dictionary reference (index 64..79)
            if (token < 0xF0) {
                int idx = 64 + (token & 0x0F);
                if (idx >= DICT_COUNT)
                    throw new IllegalArgumentException("Corrupt: dict index " + idx);
                byte[] entry = DICT[idx];
                if (op + entry.length > rawLen)
                    throw new IllegalArgumentException("Corrupt: dict output overflow");
                System.arraycopy(entry, 0, out, op, entry.length);
                op += entry.length;
                continue;
            }

            // 0xF0..0xFF: long-offset LZ match (3 bytes)
            {
                int matchLength = (token & 0x0F) + LONG_MATCH_MIN;
                if (ip + 2 > inLen)
                    throw new IllegalArgumentException("Corrupt: long LZ truncated");
                int off = ((in[inOff + ip] & 0xFF) << 8) | (in[inOff + ip + 1] & 0xFF);
                ip += 2;
                if (off == 0)
                    throw new IllegalArgumentException("Corrupt: zero offset");
                if (off > op + histLen)
                    throw new IllegalArgumentException("Corrupt: offset exceeds history");
                if (op + matchLength > rawLen)
                    throw new IllegalArgumentException("Corrupt: match output overflow");
                copyMatch(out, op, hist, histLen, off, matchLength);
                op += matchLength;
                lastOffset = off;
            }
        }

        if (op != rawLen)
            throw new IllegalArgumentException(
                "Corrupt: decoded " + op + " bytes, expected " + rawLen);

        return out;
    }

    // ---- Internal: copy match bytes (resolves cross-block history refs) ----

    private static void copyMatch(byte[] out, int op,
                                  byte[] hist, int histLen,
                                  int off, int matchLen) {
        if (off <= op) {
            // entirely within current block output
            int src = op - off;
            for (int j = 0; j < matchLen; j++) {
                out[op + j] = out[src + j];
            }
        } else {
            // starts in history, may cross into current output
            int histBack = off - op;
            int histStart = histLen - histBack;
            for (int j = 0; j < matchLen; j++) {
                int src = histStart + j;
                if (src < histLen) {
                    out[op + j] = hist[src];
                } else {
                    out[op + j] = out[src - histLen];
                }
            }
        }
    }

    // ---- Internal: history buffer update ----

    /**
     * Updates the history buffer with new block data. Returns the new histLen.
     */
    private static int updateHistory(byte[] hist, int histLen, int historySize,
                                     byte[] data, int dataOff, int dataLen) {
        if (historySize == 0) return 0;
        if (dataLen >= historySize) {
            System.arraycopy(data, dataOff + dataLen - historySize, hist, 0, historySize);
            return historySize;
        } else if (histLen + dataLen <= historySize) {
            System.arraycopy(data, dataOff, hist, histLen, dataLen);
            return histLen + dataLen;
        } else {
            int keep = historySize - dataLen;
            if (keep > histLen) keep = histLen;
            System.arraycopy(hist, histLen - keep, hist, 0, keep);
            System.arraycopy(data, dataOff, hist, keep, dataLen);
            return keep + dataLen;
        }
    }

    // ---- Internal: LE16 write helper ----

    private static void writeLE16(ByteArrayOutputStream out, int value) {
        out.write(value & 0xFF);
        out.write((value >> 8) & 0xFF);
    }
}
