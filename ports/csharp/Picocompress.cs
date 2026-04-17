// picocompress — C# port (pure managed, no unsafe, no P/Invoke)
// Byte-identical output to the C reference implementation.

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Picocompress;

/// <summary>Configurable encoder profile. Decoder is profile-independent.</summary>
public sealed class PicocompressOptions
{
    public int BlockSize { get; init; } = 508;
    public int HashBits { get; init; } = 9;
    public int ChainDepth { get; init; } = 2;
    public int HistorySize { get; init; } = 504;
    public int LazySteps { get; init; } = 1;

    public static PicocompressOptions Default { get; } = new();

    public static PicocompressOptions Micro { get; } = new()
    {
        BlockSize = 192, HashBits = 8, ChainDepth = 1, HistorySize = 64, LazySteps = 1
    };

    public static PicocompressOptions Minimal { get; } = new()
    {
        BlockSize = 508, HashBits = 8, ChainDepth = 1, HistorySize = 128, LazySteps = 1
    };

    public static PicocompressOptions Aggressive { get; } = new()
    {
        BlockSize = 508, HashBits = 8, ChainDepth = 4, HistorySize = 504, LazySteps = 1
    };

    public static PicocompressOptions Q3 { get; } = new()
    {
        BlockSize = 508, HashBits = 10, ChainDepth = 2, HistorySize = 1024, LazySteps = 2
    };

    public static PicocompressOptions Q4 { get; } = new()
    {
        BlockSize = 508, HashBits = 11, ChainDepth = 2, HistorySize = 2048, LazySteps = 2
    };
}

/// <summary>
/// picocompress — block-based LZ compressor designed for embedded payloads.
/// Produces byte-identical output to the C reference implementation.
/// </summary>
public static class PicocompressCodec
{
    // ---- Constants matching the C header ----
    private const int LiteralMax = 64;
    private const int MatchMin = 2;
    private const int MatchCodeBits = 5;
    private const int MatchMax = MatchMin + ((1 << MatchCodeBits) - 1); // 33
    private const int OffsetShortBits = 9;
    private const int OffsetShortMax = (1 << OffsetShortBits) - 1; // 511
    private const int LongMatchMin = 2;
    private const int LongMatchMax = 17;
    private const int OffsetLongMax = 65535;
    private const int DictCount = 96;
    private const int GoodMatch = 8;
    private const int RepeatCacheSize = 3;

    // ---- Static dictionary (96 entries, identical to C) ----
    private static readonly byte[][] Dict = new byte[DictCount][]
    {
        /* 0  */ [(byte)'"', (byte)':', (byte)' ', (byte)'"'],
        /* 1  */ [(byte)'}', (byte)',', (byte)'\n', (byte)'"'],
        /* 2  */ [(byte)'<', (byte)'/', (byte)'d', (byte)'i', (byte)'v'],
        /* 3  */ "tion"u8.ToArray(),
        /* 4  */ "ment"u8.ToArray(),
        /* 5  */ "ness"u8.ToArray(),
        /* 6  */ "able"u8.ToArray(),
        /* 7  */ "ight"u8.ToArray(),
        /* 8  */ [(byte)'"', (byte)':', (byte)'"'],
        /* 9  */ [(byte)'<', (byte)'/', (byte)'d', (byte)'i'],
        /* 10 */ [(byte)'=', (byte)'"', (byte)'h', (byte)'t'],
        /* 11 */ "the"u8.ToArray(),
        /* 12 */ "ing"u8.ToArray(),
        /* 13 */ [(byte)',', (byte)'"', (byte)','],
        /* 14 */ [(byte)'"', (byte)':', (byte)'{'],
        /* 15 */ [(byte)'"', (byte)':', (byte)'['],
        /* 16 */ "ion"u8.ToArray(),
        /* 17 */ "ent"u8.ToArray(),
        /* 18 */ "ter"u8.ToArray(),
        /* 19 */ "and"u8.ToArray(),
        /* 20 */ [(byte)'/', (byte)'>', (byte)'\r', (byte)'\n'],
        /* 21 */ [(byte)'"', (byte)'}', (byte)','],
        /* 22 */ [(byte)'"', (byte)']', (byte)','],
        /* 23 */ "have"u8.ToArray(),
        /* 24 */ [(byte)'n', (byte)'o', (byte)'"', (byte)':'],
        /* 25 */ "true"u8.ToArray(),
        /* 26 */ "null"u8.ToArray(),
        /* 27 */ "name"u8.ToArray(),
        /* 28 */ "data"u8.ToArray(),
        /* 29 */ "time"u8.ToArray(),
        /* 30 */ "type"u8.ToArray(),
        /* 31 */ "mode"u8.ToArray(),
        /* 32 */ "http"u8.ToArray(),
        /* 33 */ "tion"u8.ToArray(),
        /* 34 */ "code"u8.ToArray(),
        /* 35 */ "size"u8.ToArray(),
        /* 36 */ "ment"u8.ToArray(),
        /* 37 */ "list"u8.ToArray(),
        /* 38 */ "item"u8.ToArray(),
        /* 39 */ "text"u8.ToArray(),
        /* 40 */ "false"u8.ToArray(),
        /* 41 */ "error"u8.ToArray(),
        /* 42 */ "value"u8.ToArray(),
        /* 43 */ "state"u8.ToArray(),
        /* 44 */ "alert"u8.ToArray(),
        /* 45 */ "input"u8.ToArray(),
        /* 46 */ "ation"u8.ToArray(),
        /* 47 */ "order"u8.ToArray(),
        /* 48 */ "status"u8.ToArray(),
        /* 49 */ "number"u8.ToArray(),
        /* 50 */ "active"u8.ToArray(),
        /* 51 */ "device"u8.ToArray(),
        /* 52 */ "region"u8.ToArray(),
        /* 53 */ "string"u8.ToArray(),
        /* 54 */ "result"u8.ToArray(),
        /* 55 */ "length"u8.ToArray(),
        /* 56 */ "message"u8.ToArray(),
        /* 57 */ "content"u8.ToArray(),
        /* 58 */ "request"u8.ToArray(),
        /* 59 */ "default"u8.ToArray(),
        /* 60 */ [(byte)'n', (byte)'u', (byte)'m', (byte)'b', (byte)'e', (byte)'r', (byte)'"', (byte)':'],
        /* 61 */ "operator"u8.ToArray(),
        /* 62 */ [(byte)'h', (byte)'t', (byte)'t', (byte)'p', (byte)'s', (byte)':', (byte)'/', (byte)'/'],
        /* 63 */ "response"u8.ToArray(),
        /* 64 */ [(byte)'.', (byte)' ', (byte)'T', (byte)'h', (byte)'e', (byte)' '],
        /* 65 */ [(byte)'.', (byte)' ', (byte)'I', (byte)'t', (byte)' '],
        /* 66 */ [(byte)'.', (byte)' ', (byte)'T', (byte)'h', (byte)'i', (byte)'s', (byte)' '],
        /* 67 */ [(byte)'.', (byte)' ', (byte)'A', (byte)' '],
        /* 68 */ "HTTP"u8.ToArray(),
        /* 69 */ "JSON"u8.ToArray(),
        /* 70 */ [(byte)'T', (byte)'h', (byte)'e', (byte)' '],
        /* 71 */ "None"u8.ToArray(),
        /* 72 */ "ment"u8.ToArray(),
        /* 73 */ "ness"u8.ToArray(),
        /* 74 */ "able"u8.ToArray(),
        /* 75 */ "ight"u8.ToArray(),
        /* 76 */ "ation"u8.ToArray(),
        /* 77 */ [(byte)'o', (byte)'u', (byte)'l', (byte)'d', (byte)' '],
        /* 78 */ [(byte)'"', (byte)':', (byte)' ', (byte)'"'],
        /* 79 */ [(byte)'"', (byte)',', (byte)' ', (byte)'"'],
        /* 80 */ "DIM"u8.ToArray(),
        /* 81 */ "FOR"u8.ToArray(),
        /* 82 */ "END"u8.ToArray(),
        /* 83 */ "REL"u8.ToArray(),
        /* 84 */ "EACH"u8.ToArray(),
        /* 85 */ "LOAD"u8.ToArray(),
        /* 86 */ "SAVE"u8.ToArray(),
        /* 87 */ "CARD"u8.ToArray(),
        /* 88 */ "JUMP"u8.ToArray(),
        /* 89 */ "PRINT"u8.ToArray(),
        /* 90 */ "INPUT"u8.ToArray(),
        /* 91 */ "GOSUB"u8.ToArray(),
        /* 92 */ "STREAM"u8.ToArray(),
        /* 93 */ "RETURN"u8.ToArray(),
        /* 94 */ "SWITCH"u8.ToArray(),
        /* 95 */ "PROGRAM"u8.ToArray(),
    };

    // ================================================================
    // Public API
    // ================================================================

    /// <summary>Compress input bytes using the picocompress format.</summary>
    public static byte[] Compress(ReadOnlySpan<byte> input, PicocompressOptions? options = null)
    {
        var opts = options ?? PicocompressOptions.Default;
        int blockSize = opts.BlockSize;
        if (blockSize < 1 || blockSize > OffsetShortMax)
            throw new ArgumentOutOfRangeException(nameof(options), "BlockSize must be in [1, 511]");

        if (input.IsEmpty)
            return [];

        var output = new List<byte>(input.Length + ((input.Length / blockSize) + 1) * 4);
        var history = new byte[opts.HistorySize];
        int histLen = 0;

        int pos = 0;
        while (pos < input.Length)
        {
            int rawLen = Math.Min(blockSize, input.Length - pos);
            ReadOnlySpan<byte> block = input.Slice(pos, rawLen);

            // Build virtual buffer [history | block]
            int vbufLen = histLen + rawLen;
            var vbuf = new byte[vbufLen];
            Array.Copy(history, 0, vbuf, 0, histLen);
            block.CopyTo(vbuf.AsSpan(histLen));

            int maxCompressed = rawLen + (rawLen / LiteralMax) + 16;
            var tmp = new byte[maxCompressed];
            int compLen = CompressBlock(vbuf, (ushort)histLen, (ushort)rawLen, tmp, (ushort)maxCompressed, opts);

            // 4-byte header
            if (compLen < 0 || compLen >= rawLen)
            {
                // raw fallback
                output.Add((byte)(rawLen & 0xFF));
                output.Add((byte)(rawLen >> 8));
                output.Add(0);
                output.Add(0);
                for (int i = 0; i < rawLen; i++)
                    output.Add(block[i]);
            }
            else
            {
                output.Add((byte)(rawLen & 0xFF));
                output.Add((byte)(rawLen >> 8));
                output.Add((byte)(compLen & 0xFF));
                output.Add((byte)(compLen >> 8));
                for (int i = 0; i < compLen; i++)
                    output.Add(tmp[i]);
            }

            // Update history
            UpdateHistory(history, ref histLen, block, opts.HistorySize);
            pos += rawLen;
        }

        return output.ToArray();
    }

    /// <summary>Decompress picocompress data back to original bytes.</summary>
    public static byte[] Decompress(ReadOnlySpan<byte> compressed)
    {
        var output = new List<byte>();
        // Decoder history — must support any encoder history size.
        // We use a large buffer; the decoder only needs what was actually produced.
        var history = new byte[65536];
        int histLen = 0;
        int ip = 0;

        while (ip < compressed.Length)
        {
            if (ip + 4 > compressed.Length)
                throw new InvalidOperationException("Truncated block header");

            int rawLen = compressed[ip] | (compressed[ip + 1] << 8);
            int compLen = compressed[ip + 2] | (compressed[ip + 3] << 8);
            ip += 4;

            if (rawLen == 0 && compLen == 0) continue;
            if (rawLen == 0 || rawLen > OffsetShortMax)
                throw new InvalidOperationException("Corrupt: invalid raw_len");

            if (compLen == 0)
            {
                // Stored raw
                if (ip + rawLen > compressed.Length)
                    throw new InvalidOperationException("Truncated raw block");
                var raw = compressed.Slice(ip, rawLen);
                for (int i = 0; i < rawLen; i++)
                    output.Add(raw[i]);
                UpdateHistory(history, ref histLen, raw, history.Length);
                ip += rawLen;
            }
            else
            {
                if (ip + compLen > compressed.Length)
                    throw new InvalidOperationException("Truncated compressed block");
                var payload = compressed.Slice(ip, compLen);
                var raw = new byte[rawLen];
                DecompressBlock(history, histLen, payload, raw, rawLen);
                for (int i = 0; i < rawLen; i++)
                    output.Add(raw[i]);
                UpdateHistory(history, ref histLen, raw, history.Length);
                ip += compLen;
            }
        }

        return output.ToArray();
    }

    // ================================================================
    // Encoder internals
    // ================================================================

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ushort Hash3(byte[] buf, int p, int hashSize)
    {
        uint v = (uint)buf[p] * 251u + (uint)buf[p + 1] * 11u + (uint)buf[p + 2] * 3u;
        return (ushort)(v & (uint)(hashSize - 1));
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static int MatchLen(byte[] a, int aOff, byte[] b, int bOff, int limit)
    {
        int m = 0;
        while (m < limit && a[aOff + m] == b[bOff + m])
            m++;
        return m;
    }

    private static void HeadInsert(short[,] head, int chainDepth, ushort hash, short pos)
    {
        for (int d = chainDepth - 1; d > 0; d--)
            head[d, hash] = head[d - 1, hash];
        head[0, hash] = pos;
    }

    private static bool EmitLiterals(byte[] src, int srcOff, int srcLen, byte[] dst, int dstCap, ref int op)
    {
        int pos = 0;
        while (pos < srcLen)
        {
            int chunk = Math.Min(srcLen - pos, LiteralMax);
            if (op + 1 + chunk > dstCap) return false;
            dst[op++] = (byte)(chunk - 1); // 0x00..0x3F
            Array.Copy(src, srcOff + pos, dst, op, chunk);
            op += chunk;
            pos += chunk;
        }
        return true;
    }

    private struct FindResult
    {
        public int Savings;
        public ushort Len;
        public ushort Off;
        public ushort DictIdx;
        public bool IsRepeat;
    }

    private static FindResult FindBest(
        byte[] vbuf, int vbufLen, int vpos,
        short[,] head, int chainDepth, int hashSize,
        ushort[] repOffsets,
        int goodMatch,
        bool skipDict)
    {
        var r = new FindResult { DictIdx = ushort.MaxValue };
        int remaining = vbufLen - vpos;

        // 1. Repeat-offset cache
        if (remaining >= MatchMin)
        {
            int maxRep = Math.Min(remaining, MatchMax);
            for (int d = 0; d < RepeatCacheSize; d++)
            {
                ushort off = repOffsets[d];
                if (off == 0 || off > vpos) continue;
                if (vbuf[vpos] != vbuf[vpos - off]) continue;
                if (remaining >= 2 && vbuf[vpos + 1] != vbuf[vpos - off + 1]) continue;
                int len = MatchLen(vbuf, vpos - off, vbuf, vpos, maxRep);
                if (len < MatchMin) continue;

                bool isRep = d == 0 && len <= 17;
                int tokenCost = isRep ? 1 : (off <= OffsetShortMax ? 2 : 3);
                int s = len - tokenCost;

                if (s > r.Savings)
                {
                    r.Savings = s;
                    r.Len = (ushort)len;
                    r.Off = off;
                    r.DictIdx = ushort.MaxValue;
                    r.IsRepeat = isRep;
                    if (len >= goodMatch) return r;
                }
            }
        }

        // 2. Dictionary
        if (!skipDict)
        {
            byte firstByte = vbuf[vpos];
            for (int d = 0; d < DictCount; d++)
            {
                byte[] entry = Dict[d];
                int dlen = entry.Length;
                if (dlen > remaining) continue;
                if (dlen - 1 <= r.Savings) continue;
                if (entry[0] != firstByte) continue;
                bool match = true;
                for (int k = 1; k < dlen; k++)
                {
                    if (vbuf[vpos + k] != entry[k]) { match = false; break; }
                }
                if (!match) continue;
                r.Savings = dlen - 1;
                r.DictIdx = (ushort)d;
                r.Len = (ushort)dlen;
                r.Off = 0;
                r.IsRepeat = false;
                if (dlen >= goodMatch) return r;
            }
        }

        // 3. LZ hash-chain
        if (remaining >= 3)
        {
            ushort hash = Hash3(vbuf, vpos, hashSize);
            int maxLenShort = Math.Min(remaining, MatchMax);
            int maxLenLong = Math.Min(remaining, LongMatchMax);
            byte firstByte = vbuf[vpos];

            for (int d = 0; d < chainDepth; d++)
            {
                short prev = head[d, hash];
                if (prev < 0) continue;
                int prevPos = prev;
                if (prevPos >= vpos) continue;
                int off = vpos - prevPos;
                if (off == 0 || off > OffsetLongMax) continue;
                if (vbuf[prevPos] != firstByte) continue;

                int maxLen = off <= OffsetShortMax ? maxLenShort : maxLenLong;
                int len = MatchLen(vbuf, prevPos, vbuf, vpos, maxLen);
                if (len < MatchMin) continue;

                int tokenCost = off <= OffsetShortMax ? 2 : 3;
                int s = len - tokenCost;

                if (s > r.Savings
                    || (s == r.Savings && len > r.Len)
                    || (s == r.Savings && len == r.Len && off < r.Off)
                    || (s == r.Savings - 1 && len >= r.Len + 2))
                {
                    r.Savings = len - tokenCost;
                    r.Len = (ushort)len;
                    r.Off = (ushort)off;
                    r.DictIdx = ushort.MaxValue;
                    r.IsRepeat = false;
                    if (len >= goodMatch) return r;
                }
            }
        }

        return r;
    }

    private static int CompressBlock(
        byte[] vbuf, ushort histLen, ushort blockLen,
        byte[] outBuf, ushort outCap,
        PicocompressOptions opts)
    {
        int hashSize = 1 << opts.HashBits;
        int chainDepth = opts.ChainDepth;
        int lazySteps = opts.LazySteps;

        var head = new short[chainDepth, hashSize];
        for (int d = 0; d < chainDepth; d++)
            for (int h = 0; h < hashSize; h++)
                head[d, h] = -1;

        ushort[] repOffsets = [0, 0, 0];
        int vbufLen = histLen + blockLen;
        int op = 0;

        // Seed hash table from history
        if (histLen >= 3)
        {
            for (int p = 0; p + 2 < histLen; p++)
                HeadInsert(head, chainDepth, Hash3(vbuf, p, hashSize), (short)p);

            // Re-inject near boundary
            int tailStart = histLen > 64 ? histLen - 64 : 0;
            for (int p = tailStart; p + 2 < histLen; p++)
            {
                ushort h = Hash3(vbuf, p, hashSize);
                if (head[0, h] != (short)p)
                {
                    short save = head[chainDepth - 1, h];
                    HeadInsert(head, chainDepth, h, (short)p);
                    head[chainDepth - 1, h] = save;
                }
            }
        }

        // Self-disabling dictionary check
        bool dictSkip = false;
        if (blockLen >= 1)
        {
            byte b0 = vbuf[histLen];
            if (b0 == (byte)'{' || b0 == (byte)'[' || b0 == (byte)'<' || b0 == 0xEF)
            {
                dictSkip = false;
            }
            else
            {
                int checkLen = Math.Min((int)blockLen, 4);
                for (int ci = 0; ci < checkLen; ci++)
                {
                    byte c = vbuf[histLen + ci];
                    if (c < 0x20 || c > 0x7E)
                    {
                        dictSkip = true;
                        break;
                    }
                }
            }
        }

        int anchor = histLen;
        int vpos = histLen;

        while (vpos < vbufLen)
        {
            if (vbufLen - vpos < MatchMin)
                break;

            retry_pos:
            var best = FindBest(vbuf, vbufLen, vpos, head, chainDepth, hashSize, repOffsets, GoodMatch, dictSkip);

            // Insert current position into hash table
            if (vbufLen - vpos >= 3)
                HeadInsert(head, chainDepth, Hash3(vbuf, vpos, hashSize), (short)vpos);

            // Literal run extension — skip weak matches mid-run
            if (best.Savings <= 1 && best.DictIdx == ushort.MaxValue && anchor < vpos)
                best.Savings = 0;

            // Lazy matching
            if (best.Savings > 0 && best.Len < GoodMatch)
            {
                for (int step = 1; step <= lazySteps; step++)
                {
                    int npos = vpos + step;
                    if (npos >= vbufLen || vbufLen - npos < MatchMin) break;

                    var next = FindBest(vbuf, vbufLen, npos, head, chainDepth, hashSize, repOffsets, GoodMatch, dictSkip);
                    if (next.Savings > best.Savings)
                    {
                        // Insert skipped positions
                        for (int s = 0; s < step; s++)
                        {
                            int sp = vpos + s;
                            if (vbufLen - sp >= 3)
                                HeadInsert(head, chainDepth, Hash3(vbuf, sp, hashSize), (short)sp);
                        }
                        vpos = npos;
                        goto retry_pos;
                    }
                }
            }

            // Emit
            if (best.Savings > 0)
            {
                int litLen = vpos - anchor;
                if (!EmitLiterals(vbuf, anchor, litLen, outBuf, outCap, ref op))
                    return -1;

                if (best.DictIdx != ushort.MaxValue)
                {
                    if (op + 1 > outCap) return -1;
                    if (best.DictIdx < 64)
                        outBuf[op++] = (byte)(0x40 | (best.DictIdx & 0x3F));
                    else if (best.DictIdx < 80)
                        outBuf[op++] = (byte)(0xE0 | ((best.DictIdx - 64) & 0x0F));
                    else
                        outBuf[op++] = (byte)(0xD0 | ((best.DictIdx - 80) & 0x0F));
                }
                else if (best.IsRepeat)
                {
                    if (op + 1 > outCap) return -1;
                    outBuf[op++] = (byte)(0xC0 | ((best.Len - MatchMin) & 0x0F));
                }
                else if (best.Off <= OffsetShortMax && best.Len <= MatchMax)
                {
                    // Short-offset LZ: 2-byte token
                    if (op + 2 > outCap) return -1;
                    outBuf[op++] = (byte)(0x80
                        | (((best.Len - MatchMin) & 0x1F) << 1)
                        | ((best.Off >> 8) & 0x01));
                    outBuf[op++] = (byte)(best.Off & 0xFF);
                }
                else
                {
                    // Long-offset LZ: 3-byte token
                    int elen = Math.Min((int)best.Len, LongMatchMax);
                    if (op + 3 > outCap) return -1;
                    outBuf[op++] = (byte)(0xF0 | ((elen - LongMatchMin) & 0x0F));
                    outBuf[op++] = (byte)((best.Off >> 8) & 0xFF);
                    outBuf[op++] = (byte)(best.Off & 0xFF);
                    best.Len = (ushort)elen;
                }

                // Update repeat-offset cache
                if (!best.IsRepeat && best.Off != 0 && best.DictIdx == ushort.MaxValue)
                {
                    repOffsets[2] = repOffsets[1];
                    repOffsets[1] = repOffsets[0];
                    repOffsets[0] = best.Off;
                }

                // Insert match positions into hash table
                for (int k = 1; k < best.Len && vpos + k + 2 < vbufLen; k++)
                    HeadInsert(head, chainDepth, Hash3(vbuf, vpos + k, hashSize), (short)(vpos + k));

                vpos += best.Len;
                anchor = vpos;
            }
            else
            {
                vpos++;
            }
        }

        // Trailing literals
        if (anchor < vbufLen)
        {
            if (!EmitLiterals(vbuf, anchor, vbufLen - anchor, outBuf, outCap, ref op))
                return -1;
        }

        return op;
    }

    // ================================================================
    // Decoder internals
    // ================================================================

    private static void DecompressBlock(byte[] hist, int histLen,
        ReadOnlySpan<byte> inBuf, byte[] outBuf, int outLen)
    {
        int ip = 0;
        int op = 0;
        int lastOffset = 0;
        int inLen = inBuf.Length;

        while (ip < inLen)
        {
            byte token = inBuf[ip++];

            // 0x00..0x3F: short literal
            if (token < 0x40)
            {
                int litLen = (token & 0x3F) + 1;
                if (ip + litLen > inLen || op + litLen > outLen)
                    throw new InvalidOperationException("Corrupt: literal overflow");
                inBuf.Slice(ip, litLen).CopyTo(outBuf.AsSpan(op));
                ip += litLen;
                op += litLen;
                continue;
            }

            // 0x40..0x7F: dictionary ref (0..63)
            if (token < 0x80)
            {
                int idx = token & 0x3F;
                if (idx >= DictCount) throw new InvalidOperationException("Corrupt: dict index");
                byte[] entry = Dict[idx];
                if (op + entry.Length > outLen) throw new InvalidOperationException("Corrupt: dict overflow");
                Array.Copy(entry, 0, outBuf, op, entry.Length);
                op += entry.Length;
                continue;
            }

            // 0x80..0xBF: LZ match
            if (token < 0xC0)
            {
                if (ip >= inLen) throw new InvalidOperationException("Corrupt: LZ truncated");
                int matchLen = ((token >> 1) & 0x1F) + MatchMin;
                int off = ((token & 0x01) << 8) | inBuf[ip++];
                if (off == 0) throw new InvalidOperationException("Corrupt: zero offset");
                if (off > op + histLen) throw new InvalidOperationException("Corrupt: offset too large");
                if (op + matchLen > outLen) throw new InvalidOperationException("Corrupt: match overflow");
                CopyMatch(outBuf, ref op, hist, histLen, off, matchLen);
                lastOffset = off;
                continue;
            }

            // 0xC0..0xCF: repeat-offset match
            if (token < 0xD0)
            {
                int matchLen = (token & 0x0F) + MatchMin;
                if (lastOffset == 0) throw new InvalidOperationException("Corrupt: no last offset");
                if (lastOffset > op + histLen) throw new InvalidOperationException("Corrupt: repeat offset too large");
                if (op + matchLen > outLen) throw new InvalidOperationException("Corrupt: repeat overflow");
                CopyMatch(outBuf, ref op, hist, histLen, lastOffset, matchLen);
                continue;
            }

            // 0xD0..0xDF: dictionary ref (80..95)
            if (token < 0xE0)
            {
                int idx = 80 + (token & 0x0F);
                if (idx >= DictCount) throw new InvalidOperationException("Corrupt: dict index");
                byte[] entry = Dict[idx];
                if (op + entry.Length > outLen) throw new InvalidOperationException("Corrupt: dict overflow");
                Array.Copy(entry, 0, outBuf, op, entry.Length);
                op += entry.Length;
                continue;
            }

            // 0xE0..0xEF: dictionary ref (64..79)
            if (token < 0xF0)
            {
                int idx = 64 + (token & 0x0F);
                if (idx >= DictCount) throw new InvalidOperationException("Corrupt: dict index");
                byte[] entry = Dict[idx];
                if (op + entry.Length > outLen) throw new InvalidOperationException("Corrupt: dict overflow");
                Array.Copy(entry, 0, outBuf, op, entry.Length);
                op += entry.Length;
                continue;
            }

            // 0xF0..0xFF: long-offset LZ
            {
                int matchLen = (token & 0x0F) + LongMatchMin;
                if (ip + 2 > inLen) throw new InvalidOperationException("Corrupt: long LZ truncated");
                int off = (inBuf[ip] << 8) | inBuf[ip + 1];
                ip += 2;
                if (off == 0) throw new InvalidOperationException("Corrupt: zero long offset");
                if (off > op + histLen) throw new InvalidOperationException("Corrupt: long offset too large");
                if (op + matchLen > outLen) throw new InvalidOperationException("Corrupt: long match overflow");
                CopyMatch(outBuf, ref op, hist, histLen, off, matchLen);
                lastOffset = off;
            }
        }

        if (op != outLen)
            throw new InvalidOperationException("Corrupt: output length mismatch");
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static void CopyMatch(byte[] outBuf, ref int op, byte[] hist, int histLen, int off, int matchLen)
    {
        if (off <= op)
        {
            int src = op - off;
            for (int j = 0; j < matchLen; j++)
                outBuf[op++] = outBuf[src + j];
        }
        else
        {
            int histBack = off - op;
            int histStart = histLen - histBack;
            for (int j = 0; j < matchLen; j++)
            {
                int src = histStart + j;
                if (src < histLen)
                    outBuf[op++] = hist[src];
                else
                    outBuf[op++] = outBuf[src - histLen];
            }
        }
    }

    private static void UpdateHistory(byte[] hist, ref int histLen, ReadOnlySpan<byte> data, int historySize)
    {
        int len = data.Length;
        if (len >= historySize)
        {
            data.Slice(len - historySize, historySize).CopyTo(hist);
            histLen = historySize;
        }
        else if (histLen + len <= historySize)
        {
            data.CopyTo(hist.AsSpan(histLen));
            histLen += len;
        }
        else
        {
            int keep = historySize - len;
            if (keep > histLen) keep = histLen;
            Array.Copy(hist, histLen - keep, hist, 0, keep);
            data.CopyTo(hist.AsSpan(keep));
            histLen = keep + len;
        }
    }
}
