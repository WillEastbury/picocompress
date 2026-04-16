#!/usr/bin/env python3
import argparse
import ctypes
import json
import random
import struct
import time
from typing import Callable, Dict, List

import brotli
import heatshrink2


def make_pattern_payload_508() -> bytes:
    pat = b"AACCBBDDAACCBBDD1122334455667788"
    data = bytearray(508)
    for i in range(len(data)):
        if (i % 64) < 52:
            data[i] = pat[i % len(pat)]
        else:
            data[i] = (i * 17) & 0xFF
    return bytes(data)


def make_utf8_plus_int_payload_508() -> bytes:
    text_target = 230
    base_text = (
        "device=edge-ward-07|region=eu-west-2|profile=telemetry|mode=active|"
        "alert=none|message=patient monitor periodic sync; vitals stable; "
        "operator=night-shift|units=temp_c,pulse_bpm,spo2_pct,bp_mmhg|"
    ).encode("utf-8")
    text_bytes = (base_text + (b"note=stable;" * 64))[:text_target]

    records = []
    for i in range(23):
        ts = 1713000000 + (i * 5)
        temp_tenths = 365 + ((i % 7) - 3)
        spo2 = 97 - (i % 2)
        pulse = 68 + (i % 11)
        systolic = 118 + (i % 5)
        diastolic = 76 + (i % 4)
        seq = i
        records.append(struct.pack("<IhBBBBH", ts, temp_tenths, spo2, pulse, systolic, diastolic, seq))

    int_blob = b"".join(records) + struct.pack("<H", len(records))
    payload = text_bytes + int_blob
    if len(payload) != 508:
        raise RuntimeError(f"unexpected payload size: {len(payload)}")
    return payload


def make_random_payload_508() -> bytes:
    rng = random.Random(1337)
    return bytes(rng.getrandbits(8) for _ in range(508))


def make_ascii_payload_254() -> bytes:
    base = (
        "ts=1713000000,id=ward07,mode=active,alert=none,temp=36.5,pulse=72,spo2=97,"
        "bp=120/78,operator=night,region=eu-west-2,msg=stable;"
    )
    return (base * 8)[:254].encode("ascii")


def timed_us(fn: Callable[[], None], min_seconds: float = 0.25) -> float:
    n = 1
    while True:
        t0 = time.perf_counter()
        for _ in range(n):
            fn()
        dt = time.perf_counter() - t0
        if dt >= min_seconds or n >= 1_000_000:
            return (dt / n) * 1e6
        n *= 2


class PicoCodec:
    def __init__(self, dll_path: str):
        self.lib = ctypes.CDLL(dll_path)
        c_uint8_p = ctypes.POINTER(ctypes.c_uint8)
        self.lib.pc_compress_bound.argtypes = [ctypes.c_size_t]
        self.lib.pc_compress_bound.restype = ctypes.c_size_t
        self.lib.pc_compress_buffer.argtypes = [
            c_uint8_p,
            ctypes.c_size_t,
            c_uint8_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.pc_compress_buffer.restype = ctypes.c_int
        self.lib.pc_decompress_buffer.argtypes = [
            c_uint8_p,
            ctypes.c_size_t,
            c_uint8_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.pc_decompress_buffer.restype = ctypes.c_int

    def compress(self, payload: bytes) -> bytes:
        in_arr = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
        out_cap = int(self.lib.pc_compress_bound(len(payload))) + 64
        out_arr = (ctypes.c_uint8 * out_cap)()
        out_len = ctypes.c_size_t(0)
        rc = self.lib.pc_compress_buffer(in_arr, len(payload), out_arr, out_cap, ctypes.byref(out_len))
        if rc != 0:
            raise RuntimeError(f"pc_compress_buffer failed: {rc}")
        return bytes(out_arr[: out_len.value])

    def decompress(self, blob: bytes, expected_len: int) -> bytes:
        in_arr = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob)
        out_arr = (ctypes.c_uint8 * expected_len)()
        out_len = ctypes.c_size_t(0)
        rc = self.lib.pc_decompress_buffer(in_arr, len(blob), out_arr, expected_len, ctypes.byref(out_len))
        if rc != 0:
            raise RuntimeError(f"pc_decompress_buffer failed: {rc}")
        return bytes(out_arr[: out_len.value])


def run_payload(payload_name: str, payload: bytes, pico: PicoCodec) -> Dict[str, object]:
    codecs = [
        ("picocompress", lambda p: pico.compress(p), lambda c: pico.decompress(c, len(payload))),
        (
            "heatshrink(w11,l4)",
            lambda p: heatshrink2.compress(p, window_sz2=11, lookahead_sz2=4),
            lambda c: heatshrink2.decompress(c, window_sz2=11, lookahead_sz2=4),
        ),
        ("brotli(q1,lgwin10)", lambda p: brotli.compress(p, quality=1, lgwin=10), brotli.decompress),
        ("brotli(q5,lgwin10)", lambda p: brotli.compress(p, quality=5, lgwin=10), brotli.decompress),
        ("brotli(default)", lambda p: brotli.compress(p), brotli.decompress),
    ]

    rows: List[Dict[str, object]] = []
    for codec_name, cfn, dfn in codecs:
        compressed = cfn(payload)
        restored = dfn(compressed)
        if restored != payload:
            raise RuntimeError(f"{payload_name}: {codec_name} roundtrip mismatch")
        rows.append(
            {
                "codec": codec_name,
                "compressed_bytes": len(compressed),
                "ratio": len(payload) / len(compressed),
                "saved_bytes": len(payload) - len(compressed),
                "compress_us": timed_us(lambda: cfn(payload)),
                "decompress_us": timed_us(lambda: dfn(compressed)),
            }
        )

    return {"payload": payload_name, "payload_bytes": len(payload), "results": rows}


def format_markdown(report: List[Dict[str, object]]) -> str:
    lines: List[str] = []
    for payload_block in report:
        lines.append(f"### {payload_block['payload']} ({payload_block['payload_bytes']} bytes)")
        lines.append("| Codec | Compressed bytes | Ratio | Saved bytes | Compress us | Decompress us |")
        lines.append("|---|---:|---:|---:|---:|---:|")
        for row in payload_block["results"]:
            lines.append(
                f"| {row['codec']} | {row['compressed_bytes']} | {row['ratio']:.3f}x | "
                f"{row['saved_bytes']} | {row['compress_us']:.2f} | {row['decompress_us']:.2f} |"
            )
        lines.append("")
    return "\n".join(lines).strip() + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Benchmark picocompress vs heatshrink and brotli.")
    parser.add_argument("--dll", default="picocompress.dll", help="Path to picocompress DLL.")
    parser.add_argument("--json-out", default="", help="Optional path to write JSON report.")
    args = parser.parse_args()

    pico = PicoCodec(args.dll)
    report = [
        run_payload("pattern-508", make_pattern_payload_508(), pico),
        run_payload("utf8-int-508", make_utf8_plus_int_payload_508(), pico),
        run_payload("random-508", make_random_payload_508(), pico),
        run_payload("ascii-254", make_ascii_payload_254(), pico),
    ]

    print(json.dumps(report, indent=2))
    print()
    print(format_markdown(report))

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)


if __name__ == "__main__":
    main()
