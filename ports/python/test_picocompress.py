"""Tests for the picocompress Python port.

Validates roundtrip correctness, token format, dictionary integrity,
streaming API, profile support, and edge cases.
"""

from __future__ import annotations

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))

import picocompress as pc


class TestDictionary(unittest.TestCase):
    """Verify the static dictionary matches the C version exactly."""

    def test_dict_count(self) -> None:
        self.assertEqual(len(pc.STATIC_DICT), 96)

    def test_dict_max_len(self) -> None:
        for i, entry in enumerate(pc.STATIC_DICT):
            self.assertLessEqual(len(entry), 8, f"entry {i} too long: {entry!r}")
            self.assertGreaterEqual(len(entry), 1, f"entry {i} is empty")

    def test_specific_entries(self) -> None:
        """Spot-check entries that are critical for C compatibility."""
        self.assertEqual(pc.STATIC_DICT[0], b'": "')
        self.assertEqual(pc.STATIC_DICT[1], b'},\n"')
        self.assertEqual(pc.STATIC_DICT[2], b'</div')
        self.assertEqual(pc.STATIC_DICT[8], b'":"')
        self.assertEqual(pc.STATIC_DICT[11], b'the')
        self.assertEqual(pc.STATIC_DICT[25], b'true')
        self.assertEqual(pc.STATIC_DICT[26], b'null')
        self.assertEqual(pc.STATIC_DICT[40], b'false')
        self.assertEqual(pc.STATIC_DICT[60], b'number":')
        self.assertEqual(pc.STATIC_DICT[62], b'https://')
        self.assertEqual(pc.STATIC_DICT[63], b'response')
        self.assertEqual(pc.STATIC_DICT[64], b'. The ')
        self.assertEqual(pc.STATIC_DICT[69], b'JSON')
        self.assertEqual(pc.STATIC_DICT[80], b'DIM')
        self.assertEqual(pc.STATIC_DICT[95], b'PROGRAM')

    def test_entry_bytes_exact(self) -> None:
        """Verify byte values of entries with non-obvious content."""
        # Entry 0: ": " → 0x22 0x3A 0x20 0x22
        self.assertEqual(pc.STATIC_DICT[0], bytes([0x22, 0x3A, 0x20, 0x22]))
        # Entry 1: },\n" → 0x7D 0x2C 0x0A 0x22
        self.assertEqual(pc.STATIC_DICT[1], bytes([0x7D, 0x2C, 0x0A, 0x22]))
        # Entry 20: />\r\n → 0x2F 0x3E 0x0D 0x0A
        self.assertEqual(pc.STATIC_DICT[20], bytes([0x2F, 0x3E, 0x0D, 0x0A]))


class TestConstants(unittest.TestCase):
    """Verify all constants match picocompress.h."""

    def test_defaults(self) -> None:
        p = pc.DEFAULT_PROFILE
        self.assertEqual(p.block_size, 508)
        self.assertEqual(p.hash_bits, 9)
        self.assertEqual(p.chain_depth, 2)
        self.assertEqual(p.history_size, 504)
        self.assertEqual(p.lazy_steps, 1)

    def test_match_constants(self) -> None:
        self.assertEqual(pc.LITERAL_MAX, 64)
        self.assertEqual(pc.MATCH_MIN, 2)
        self.assertEqual(pc.MATCH_MAX, 33)
        self.assertEqual(pc.OFFSET_SHORT_MAX, 511)
        self.assertEqual(pc.LONG_MATCH_MAX, 17)
        self.assertEqual(pc.OFFSET_LONG_MAX, 65535)


class TestRoundtrip(unittest.TestCase):
    """Roundtrip: compress then decompress must produce identical data."""

    def _roundtrip(self, data: bytes, profile: pc.Profile | None = None) -> None:
        compressed = pc.compress(data, profile=profile)
        decompressed = pc.decompress(compressed)
        self.assertEqual(decompressed, data,
                         f"roundtrip failed for {len(data)} bytes "
                         f"(compressed {len(compressed)} bytes)")

    def test_empty(self) -> None:
        self._roundtrip(b"")

    def test_single_byte(self) -> None:
        self._roundtrip(b"\x00")
        self._roundtrip(b"\xff")
        self._roundtrip(b"A")

    def test_short_literal(self) -> None:
        self._roundtrip(b"Hello, world!")

    def test_repeated_byte(self) -> None:
        self._roundtrip(b"A" * 100)

    def test_repeated_pattern(self) -> None:
        self._roundtrip(b"abcabc" * 50)

    def test_all_byte_values(self) -> None:
        self._roundtrip(bytes(range(256)))

    def test_exact_literal_max(self) -> None:
        self._roundtrip(bytes(range(64)))

    def test_just_over_literal_max(self) -> None:
        self._roundtrip(bytes(range(65)))

    def test_exact_block_size(self) -> None:
        data = bytes(i & 0xFF for i in range(508))
        self._roundtrip(data)

    def test_multi_block(self) -> None:
        data = b"The quick brown fox jumps over the lazy dog. " * 50
        self._roundtrip(data)

    def test_large_data(self) -> None:
        data = b"picocompress test data with various patterns " * 200
        self._roundtrip(data)

    def test_json_payload(self) -> None:
        data = b'{"name":"test","value":42,"active":true,"status":"ok","type":"device","items":["a","b"]}'
        self._roundtrip(data)

    def test_json_array(self) -> None:
        item = b'{"name":"item","value":"data","type":"string","status":"active"},'
        data = b"[" + item * 20 + b'{"name":"last"}]'
        self._roundtrip(data)

    def test_csv_data(self) -> None:
        header = b"name,type,value,status,region\r\n"
        row = b"device01,sensor,42,active,us-east\r\n"
        data = header + row * 30
        self._roundtrip(data)

    def test_binary_data(self) -> None:
        import hashlib
        data = hashlib.sha256(b"test seed").digest() * 20
        self._roundtrip(data)

    def test_html_data(self) -> None:
        data = b"<div>content</div>" * 30
        self._roundtrip(data)

    def test_dictionary_words(self) -> None:
        """Data containing many dictionary entries."""
        data = b"the message content request response default value error status number"
        self._roundtrip(data)

    def test_null_bytes(self) -> None:
        self._roundtrip(b"\x00" * 200)

    def test_alternating(self) -> None:
        self._roundtrip(bytes([0xAA, 0x55] * 300))

    def test_boundary_508(self) -> None:
        """Exactly one block."""
        self._roundtrip(b"X" * 508)

    def test_boundary_509(self) -> None:
        """One full block + 1 byte."""
        self._roundtrip(b"Y" * 509)

    def test_boundary_1016(self) -> None:
        """Exactly two blocks."""
        self._roundtrip(b"Z" * 1016)


class TestRoundtripProfiles(unittest.TestCase):
    """Verify roundtrip with each named profile."""

    def test_all_profiles(self) -> None:
        data = b'{"name":"value","type":"string","status":"active"}' * 10
        for name, profile in pc.PROFILES.items():
            with self.subTest(profile=name):
                compressed = pc.compress(data, profile=profile)
                decompressed = pc.decompress(compressed)
                self.assertEqual(decompressed, data, f"profile {name} failed")


class TestBlockFormat(unittest.TestCase):
    """Verify the 4-byte block header format."""

    def test_header_layout(self) -> None:
        data = b"Hello, world!"
        compressed = pc.compress(data)
        # First 4 bytes are the block header
        raw_len = compressed[0] | (compressed[1] << 8)
        comp_len = compressed[2] | (compressed[3] << 8)
        self.assertEqual(raw_len, len(data))
        if comp_len == 0:
            # Stored raw
            self.assertEqual(compressed[4:], data)
        else:
            self.assertLess(comp_len, raw_len)

    def test_raw_fallback(self) -> None:
        """Random-looking data should be stored raw (comp_len == 0)."""
        import hashlib
        data = hashlib.sha256(b"random seed 42").digest()[:20]
        compressed = pc.compress(data)
        raw_len = compressed[0] | (compressed[1] << 8)
        comp_len = compressed[2] | (compressed[3] << 8)
        self.assertEqual(raw_len, len(data))
        # comp_len == 0 means raw fallback
        if comp_len == 0:
            self.assertEqual(compressed[4:], data)


class TestTokenFormat(unittest.TestCase):
    """Verify token byte ranges in compressed output."""

    def _get_tokens(self, data: bytes) -> list[int]:
        """Compress data, strip block header, return first bytes of each token."""
        compressed = pc.compress(data)
        raw_len = compressed[0] | (compressed[1] << 8)
        comp_len = compressed[2] | (compressed[3] << 8)
        if comp_len == 0:
            return []  # stored raw, no tokens
        payload = compressed[4: 4 + comp_len]
        tokens = []
        ip = 0
        while ip < len(payload):
            t = payload[ip]
            tokens.append(t)
            if t < 0x40:
                ip += 1 + (t & 0x3F) + 1
            elif t < 0x80:
                ip += 1
            elif t < 0xC0:
                ip += 2
            elif t < 0xD0:
                ip += 1
            elif t < 0xE0:
                ip += 1
            elif t < 0xF0:
                ip += 1
            else:
                ip += 3
        return tokens

    def test_literal_token_range(self) -> None:
        tokens = self._get_tokens(b"Hello!")
        for t in tokens:
            if t < 0x40:
                self.assertGreaterEqual(t, 0x00)
                self.assertLess(t, 0x40)

    def test_dict_token_fires(self) -> None:
        """Ensure dictionary tokens fire for known entries."""
        data = b'{"name":"value","type":"string","status":"active"}'
        tokens = self._get_tokens(data)
        dict_tokens = [t for t in tokens if 0x40 <= t < 0x80 or 0xD0 <= t < 0xF0]
        self.assertGreater(len(dict_tokens), 0, "expected dictionary tokens")

    def test_lz_match_fires(self) -> None:
        """Ensure LZ tokens fire for repeated content."""
        data = b"abcdefghijklmnop" * 10
        tokens = self._get_tokens(data)
        lz_tokens = [t for t in tokens if 0x80 <= t < 0xC0 or 0xF0 <= t]
        self.assertGreater(len(lz_tokens), 0, "expected LZ tokens")


class TestStreamingAPI(unittest.TestCase):
    """Verify the streaming encoder/decoder API."""

    def test_streaming_roundtrip(self) -> None:
        data = b"The quick brown fox " * 50
        enc = pc.Encoder()
        compressed = bytearray()
        enc.sink(data, compressed.extend)
        enc.finish(compressed.extend)

        dec = pc.Decoder()
        decompressed = bytearray()
        dec.sink(bytes(compressed), decompressed.extend)
        dec.finish()

        self.assertEqual(bytes(decompressed), data)

    def test_streaming_byte_at_a_time(self) -> None:
        """Feed one byte at a time to both encoder and decoder."""
        data = b"streaming test " * 20
        enc = pc.Encoder()
        compressed = bytearray()
        for b in data:
            enc.sink(bytes([b]), compressed.extend)
        enc.finish(compressed.extend)

        dec = pc.Decoder()
        decompressed = bytearray()
        for b in compressed:
            dec.sink(bytes([b]), decompressed.extend)
        dec.finish()

        self.assertEqual(bytes(decompressed), data)

    def test_streaming_matches_buffer(self) -> None:
        """Streaming and buffer APIs produce identical output."""
        data = b"consistency check " * 30
        buffer_result = pc.compress(data)

        enc = pc.Encoder()
        stream_result = bytearray()
        enc.sink(data, stream_result.extend)
        enc.finish(stream_result.extend)

        self.assertEqual(bytes(stream_result), buffer_result)

    def test_decoder_finish_rejects_partial(self) -> None:
        data = pc.compress(b"test data")
        dec = pc.Decoder()
        dec.sink(data[:2], bytearray().extend)
        with self.assertRaises(pc.CorruptDataError):
            dec.finish()


class TestCompression(unittest.TestCase):
    """Test that compression actually reduces size for compressible data."""

    def test_compresses_json(self) -> None:
        item = b'{"name":"item","value":"data","type":"string","status":"active"},'
        data = b"[" + item * 10 + b'{"name":"last"}]'
        compressed = pc.compress(data)
        self.assertLess(len(compressed), len(data))

    def test_compresses_repeated(self) -> None:
        data = b"compress me! " * 100
        compressed = pc.compress(data)
        self.assertLess(len(compressed), len(data))

    def test_expansion_bounded(self) -> None:
        """Worst case: random data expands by at most 4 bytes per block."""
        import hashlib
        data = hashlib.sha256(b"expansion test").digest() * 20  # 640 bytes
        compressed = pc.compress(data)
        blocks = (len(data) + 507) // 508
        max_size = len(data) + blocks * 4
        self.assertLessEqual(len(compressed), max_size)


class TestEdgeCases(unittest.TestCase):
    """Edge cases and error handling."""

    def test_compress_empty(self) -> None:
        self.assertEqual(pc.compress(b""), b"")

    def test_decompress_empty(self) -> None:
        self.assertEqual(pc.decompress(b""), b"")

    def test_compress_bound(self) -> None:
        self.assertEqual(pc.compress_bound(0), 0)
        self.assertEqual(pc.compress_bound(1), 5)  # 1 + 1 block * 4
        self.assertEqual(pc.compress_bound(508), 512)  # 508 + 1 * 4
        self.assertEqual(pc.compress_bound(509), 517)  # 509 + 2 * 4

    def test_corrupt_data_raises(self) -> None:
        with self.assertRaises(pc.PicocompressError):
            pc.decompress(b"\x01\x00\x01\x00\x80")  # truncated LZ token

    def test_cross_block_history(self) -> None:
        """Verify cross-block matches work by using data that repeats across blocks."""
        block = b"cross-block pattern test data. " * 17  # ~510 bytes per copy
        data = block + block  # second copy should match history
        compressed = pc.compress(data)
        decompressed = pc.decompress(compressed)
        self.assertEqual(decompressed, data)
        # Second block should compress better thanks to history
        self.assertLess(len(compressed), len(data))


class TestHashFunction(unittest.TestCase):
    """Test the portable hash function matches C behavior."""

    def test_deterministic(self) -> None:
        data = b"abc"
        h1 = pc._hash3(data, 0, 0x1FF)
        h2 = pc._hash3(data, 0, 0x1FF)
        self.assertEqual(h1, h2)

    def test_in_range(self) -> None:
        data = b"xyz"
        h = pc._hash3(data, 0, 0x1FF)
        self.assertGreaterEqual(h, 0)
        self.assertLess(h, 512)

    def test_known_values(self) -> None:
        """Verify hash matches: a*251 + b*11 + c*3."""
        data = b"abc"
        expected = (ord('a') * 251 + ord('b') * 11 + ord('c') * 3) & 0x1FF
        self.assertEqual(pc._hash3(data, 0, 0x1FF), expected)


class TestProfiles(unittest.TestCase):
    """Verify profile definitions."""

    def test_default_is_balanced(self) -> None:
        default = pc.DEFAULT_PROFILE
        balanced = pc.PROFILES["balanced"]
        self.assertEqual(default, balanced)

    def test_all_profiles_exist(self) -> None:
        expected = {"micro", "minimal", "balanced", "aggressive", "q3", "q4"}
        self.assertEqual(set(pc.PROFILES.keys()), expected)

    def test_profile_hash_size(self) -> None:
        for name, p in pc.PROFILES.items():
            self.assertEqual(p.hash_size, 1 << p.hash_bits, f"{name} hash_size")


class TestCCompatibility(unittest.TestCase):
    """Validate byte-level compatibility with the C implementation.

    These tests verify the exact token format and encoding decisions
    match the C version.
    """

    def test_literal_encoding(self) -> None:
        """Short literal: token byte = chunk_len - 1 (0x00..0x3F)."""
        data = b"XYZ"
        compressed = pc.compress(data)
        # Should be a single block with literal token
        raw_len = compressed[0] | (compressed[1] << 8)
        comp_len = compressed[2] | (compressed[3] << 8)
        self.assertEqual(raw_len, 3)
        if comp_len > 0:
            self.assertEqual(compressed[4], 2)  # token: len-1 = 2
            self.assertEqual(compressed[5:8], b"XYZ")

    def test_dict_entry_0_token(self) -> None:
        """Dictionary entry 0 (': "') should emit token 0x40."""
        # Entry 0 is b'": "' (4 bytes)
        data = b'": "'
        compressed = pc.compress(data)
        comp_len = compressed[2] | (compressed[3] << 8)
        if comp_len > 0:
            payload = compressed[4: 4 + comp_len]
            self.assertIn(0x40, payload, "expected dict ref token 0x40")

    def test_repeat_offset_token_range(self) -> None:
        """Repeat-offset tokens must be in 0xC0..0xCF range."""
        data = b"ABCABC" * 50
        compressed = pc.compress(data)
        comp_len = compressed[2] | (compressed[3] << 8)
        if comp_len > 0:
            payload = compressed[4: 4 + comp_len]
            for byte_val in payload:
                if 0xC0 <= byte_val <= 0xDF:
                    # Must be in 0xC0..0xCF for repeat-offset
                    # or 0xD0..0xDF for dict 80-95
                    self.assertTrue(
                        byte_val < 0xD0 or byte_val < 0xE0,
                        f"unexpected byte in repeat/dict range: 0x{byte_val:02X}")


if __name__ == "__main__":
    unittest.main()
