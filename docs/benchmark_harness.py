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


# ---------------------------------------------------------------------------
# Scaled payload generators — each returns exactly `size` bytes
# ---------------------------------------------------------------------------

def make_random(size: int) -> bytes:
    rng = random.Random(42)
    return bytes(rng.getrandbits(8) for _ in range(size))


def make_sparse(size: int) -> bytes:
    """Mostly zeros with occasional sensor-like spikes."""
    rng = random.Random(99)
    data = bytearray(size)
    for i in range(size):
        if rng.random() < 0.05:
            data[i] = rng.randint(1, 255)
    return bytes(data)


def make_uint32_array(size: int) -> bytes:
    """Packed little-endian uint32 counters with small deltas."""
    rng = random.Random(77)
    count = size // 4
    vals = []
    v = 1_000_000
    for _ in range(count):
        v += rng.randint(0, 15)
        vals.append(v)
    blob = struct.pack(f"<{count}I", *vals)
    pad = size - len(blob)
    return blob + b"\x00" * pad


_UK_FIRST = [
    "Oliver", "George", "Arthur", "Noah", "Muhammad", "Leo", "Oscar",
    "Harry", "Alfie", "Jack", "Amelia", "Olivia", "Isla", "Ava",
    "Mia", "Ivy", "Lily", "Isabella", "Rosie", "Sophia",
]
_UK_LAST = [
    "Smith", "Jones", "Williams", "Taylor", "Brown", "Davies",
    "Wilson", "Evans", "Thomas", "Roberts", "Johnson", "Walker",
    "Wright", "Robinson", "Thompson", "White", "Hughes", "Edwards",
    "Green", "Hall",
]
_UK_STREETS = [
    "High Street", "Station Road", "Church Lane", "Mill Road",
    "Park Avenue", "Victoria Road", "Green Lane", "Manor Road",
    "Kings Road", "Queens Road", "The Crescent", "Elm Grove",
    "Oakfield Road", "Bridge Street", "Market Square",
]
_UK_TOWNS = [
    "London", "Birmingham", "Manchester", "Leeds", "Bristol",
    "Sheffield", "Liverpool", "Nottingham", "Southampton", "Leicester",
    "Coventry", "Bradford", "Cardiff", "Edinburgh", "Glasgow",
]
_UK_POSTCODES = [
    "SW1A 1AA", "EC2R 8AH", "B1 1BB", "M1 1AE", "BS1 4DJ",
    "S1 2BJ", "L1 8JQ", "NG1 5FW", "SO14 0YN", "LE1 6RN",
    "CV1 1FY", "BD1 1HQ", "CF10 1EP", "EH1 1YZ", "G1 1DN",
]


def make_uk_names_addresses(size: int) -> bytes:
    """Repeating UK name+address records in UTF-8."""
    rng = random.Random(123)
    lines: list[str] = []
    while True:
        first = rng.choice(_UK_FIRST)
        last = rng.choice(_UK_LAST)
        num = rng.randint(1, 200)
        street = rng.choice(_UK_STREETS)
        town = rng.choice(_UK_TOWNS)
        pc = rng.choice(_UK_POSTCODES)
        line = f"{first} {last}, {num} {street}, {town}, {pc}\n"
        lines.append(line)
        total = sum(len(l.encode("utf-8")) for l in lines)
        if total >= size:
            break
    blob = "".join(lines).encode("utf-8")[:size]
    if len(blob) < size:
        blob += b" " * (size - len(blob))
    return blob


def make_utf8_prose(size: int) -> bytes:
    """Repeating English-like prose typical of log/event messages."""
    sentences = [
        "The temperature sensor on ward 7 reported a value of 36.5 degrees Celsius at 14:32 UTC. ",
        "Patient monitor device edge-ward-07 completed periodic sync; all vitals are stable. ",
        "Alert level none: blood pressure reading 120/78 mmHg within normal range for operator night-shift. ",
        "Telemetry profile active for region eu-west-2; next scheduled upload in 300 seconds. ",
        "SpO2 reading 97 percent recorded by pulse oximeter sensor; no intervention required. ",
        "Nurse station acknowledged status update from bed monitor; note=stable logged to record. ",
        "Environmental sensor reports ambient temperature 22.1C, humidity 45 percent, CO2 412 ppm. ",
        "Medication dispenser confirmed dose release: paracetamol 500mg at 08:15 GMT for patient 4072. ",
        "Network gateway eu-west-2-gw03 heartbeat received; latency 12ms, packet loss 0.0 percent. ",
        "Shift handover summary: 12 patients monitored, 0 critical alerts, 3 advisory notes logged. ",
    ]
    rng = random.Random(456)
    parts: list[str] = []
    while sum(len(s.encode("utf-8")) for s in parts) < size:
        parts.append(rng.choice(sentences))
    blob = "".join(parts).encode("utf-8")[:size]
    if len(blob) < size:
        blob += b" " * (size - len(blob))
    return blob


def make_order_positional(size: int) -> bytes:
    """Fixed-width positional order records (order header + line items).

    Header (80 bytes):
      ORDER_ID(10) DATE(8) CUST_ID(10) CUST_NAME(30) STATUS(6) TOTAL(10) CURR(3) NL(1) PAD(2)
    Line item (40 bytes):
      LINE(4) SKU(12) DESC(10) QTY(4) UNIT_PRICE(8) NL(1) PAD(1)
    """
    rng = random.Random(789)
    skus = [f"SKU-{i:07d}" for i in range(100)]
    descs = [
        "Widget    ", "Grommet   ", "Sprocket  ", "Bearing   ", "Gasket    ",
        "Bracket   ", "Washer    ", "Bolt M6   ", "Nut M6    ", "Pin 3mm   ",
    ]
    statuses = ["OPEN  ", "HOLD  ", "SHIP  ", "DONE  ", "CANCL ", "BACKOD"]
    names = [
        f"{rng.choice(_UK_FIRST)} {rng.choice(_UK_LAST)}"
        for _ in range(40)
    ]

    buf = bytearray()
    order_id = 1000000
    while len(buf) < size:
        order_id += rng.randint(1, 5)
        date = f"2024{rng.randint(1,12):02d}{rng.randint(1,28):02d}"
        cust_id = f"C{rng.randint(100000,999999):06d}"
        name = rng.choice(names)
        status = rng.choice(statuses)
        n_lines = rng.randint(1, 6)
        total = 0.0
        lines_buf = bytearray()
        for ln in range(1, n_lines + 1):
            sku = rng.choice(skus)
            desc = rng.choice(descs)
            qty = rng.randint(1, 50)
            price = round(rng.uniform(0.50, 99.99), 2)
            total += qty * price
            line_rec = f"{ln:<4d}{sku:<12s}{desc:<10s}{qty:<4d}{price:>8.2f}\n "
            lines_buf.extend(line_rec[:40].encode("ascii"))
        header = (
            f"{order_id:<10d}{date:<8s}{cust_id:<10s}{name:<30s}"
            f"{status:<6s}{total:>10.2f}GBP\n  "
        )
        buf.extend(header[:80].encode("ascii"))
        buf.extend(lines_buf)

    return bytes(buf[:size])


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


# ---------------------------------------------------------------------------
# Realistic file-type payloads
# ---------------------------------------------------------------------------

def make_json_508() -> bytes:
    """Realistic 508-byte JSON API response."""
    doc = json.dumps({
        "id": 42,
        "name": "Oliver Smith",
        "number": 1001,
        "type": "order",
        "status": "active",
        "created": "2024-04-16T09:00:00Z",
        "region": "eu-west-2",
        "data": {
            "value": 36.5,
            "error": False,
            "state": "normal",
            "mode": "auto",
            "time": "2024-04-16T09:00:00Z",
            "message": "all clear",
            "operator": "night",
        },
        "items": [
            {"id": 1, "name": "Widget", "number": 100, "value": 12.50},
            {"id": 2, "name": "Grommet", "number": 200, "value": 8.75},
            {"id": 3, "name": "Sprocket", "number": 150, "value": 15.00},
        ],
    }, indent=2).encode("utf-8")
    return doc[:508].ljust(508)


def make_json_4k_pretty() -> bytes:
    """4K pretty-printed JSON with realistic structure."""
    rng = random.Random(900)
    items = []
    for i in range(30):
        items.append({
            "id": 1000 + i,
            "name": rng.choice(["Widget", "Grommet", "Sprocket", "Bearing",
                                "Gasket", "Bracket", "Washer", "Bolt"]),
            "number": rng.randint(100, 9999),
            "status": rng.choice(["active", "inactive", "pending"]),
            "value": round(rng.uniform(1.0, 99.99), 2),
            "type": rng.choice(["standard", "premium", "bulk"]),
            "region": rng.choice(["eu-west-2", "us-east-1", "ap-south-1"]),
        })
    doc = json.dumps({
        "request": "list_items",
        "status": "active",
        "message": "operation completed",
        "data": {"items": items, "total": len(items), "error": False},
    }, indent=2).encode("utf-8")
    return doc[:4096].ljust(4096)


def make_json_4k_minified() -> bytes:
    """Same logical content as make_json_4k_pretty but minified."""
    rng = random.Random(900)
    items = []
    for i in range(30):
        items.append({
            "id": 1000 + i,
            "name": rng.choice(["Widget", "Grommet", "Sprocket", "Bearing",
                                "Gasket", "Bracket", "Washer", "Bolt"]),
            "number": rng.randint(100, 9999),
            "status": rng.choice(["active", "inactive", "pending"]),
            "value": round(rng.uniform(1.0, 99.99), 2),
            "type": rng.choice(["standard", "premium", "bulk"]),
            "region": rng.choice(["eu-west-2", "us-east-1", "ap-south-1"]),
        })
    doc = json.dumps({
        "request": "list_items",
        "status": "active",
        "message": "operation completed",
        "data": {"items": items, "total": len(items), "error": False},
    }, separators=(",", ":")).encode("utf-8")
    return doc[:4096].ljust(4096)


def make_lorem_508() -> bytes:
    """Classic lorem ipsum text, 508 bytes."""
    text = (
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
        "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim "
        "ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut "
        "aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. Sed ut "
        "perspiciatis unde omnis iste natus error sit voluptatem."
    ).encode("utf-8")
    return text[:508].ljust(508)


def _rgb_pixel(r: int, g: int, b: int) -> bytes:
    return bytes([r, g, b])


def make_rgb_icon_508() -> bytes:
    """Tiny 13x13 uncompressed RGB icon (~507 bytes of pixel data + 1 pad).

    Simple gradient pattern with a coloured border — representative of a
    small sprite or status icon on an embedded display.
    """
    w, h = 13, 13
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            if x == 0 or x == w - 1 or y == 0 or y == h - 1:
                pixels += _rgb_pixel(0, 120, 215)       # blue border
            elif x == 1 or x == w - 2 or y == 1 or y == h - 2:
                pixels += _rgb_pixel(255, 255, 255)      # white inner border
            else:
                r = int(255 * x / w)
                g = int(255 * y / h)
                b = 128
                pixels += _rgb_pixel(r, g, b)
    return bytes(pixels[:508].ljust(508))


def make_tiny_jpeg_508() -> bytes:
    """Synthetic but valid-structure JPEG-like blob.

    Real JPEG headers + quantisation tables + entropy-coded noise.
    Not a decodable image, but byte-distribution matches real JPEGs:
    high-entropy with periodic marker bytes (0xFF).
    """
    rng = random.Random(7777)
    # SOI + APP0 header (20 bytes)
    header = bytes([
        0xFF, 0xD8,                         # SOI
        0xFF, 0xE0, 0x00, 0x10,             # APP0 length=16
        0x4A, 0x46, 0x49, 0x46, 0x00,       # JFIF\0
        0x01, 0x01, 0x00,                    # version 1.1, aspect
        0x00, 0x01, 0x00, 0x01, 0x00, 0x00, # 1x1 pixel density
    ])
    # DQT marker + 65-byte quant table
    dqt = bytes([0xFF, 0xDB, 0x00, 0x43, 0x00]) + bytes(
        min(255, 16 + i * 3) for i in range(64)
    )
    # Fill rest with entropy-like data (high entropy, occasional 0xFF)
    remaining = 508 - len(header) - len(dqt) - 2  # -2 for EOI
    entropy = bytearray()
    for _ in range(remaining):
        b = rng.randint(0, 255)
        if b == 0xFF and rng.random() > 0.05:
            b = 0xFE  # avoid accidental markers
        entropy.append(b)
    eoi = bytes([0xFF, 0xD9])
    return (header + dqt + bytes(entropy) + eoi)[:508]


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


# Peak RAM usage per codec (measured with allocation-tracking instrumentation).
# These are fixed per codec configuration, independent of payload size.
# encode_ram = peak bytes during compression; decode_ram = peak bytes during decompression.
CODEC_RAM = {
    "picocompress":       {"encode_ram": 3085,   "decode_ram": 1028},
    "heatshrink(w11,l4)": {"encode_ram": 12834,  "decode_ram": 2098},
    "brotli(q1,lgwin10)": {"encode_ram": 17140,  "decode_ram": 32291},
    "brotli(q5,lgwin10)": {"encode_ram": 566915, "decode_ram": 32291},
    "brotli(default)":    {"encode_ram": 739451, "decode_ram": 33667},
}


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
        ram = CODEC_RAM.get(codec_name, {"encode_ram": 0, "decode_ram": 0})
        rows.append(
            {
                "codec": codec_name,
                "compressed_bytes": len(compressed),
                "ratio": len(payload) / len(compressed),
                "saved_bytes": len(payload) - len(compressed),
                "compress_us": timed_us(lambda: cfn(payload)),
                "decompress_us": timed_us(lambda: dfn(compressed)),
                "encode_ram": ram["encode_ram"],
                "decode_ram": ram["decode_ram"],
            }
        )

    return {"payload": payload_name, "payload_bytes": len(payload), "results": rows}


def _fmt_ram(b: int) -> str:
    """Format byte count as a compact human string."""
    if b >= 1048576:
        return f"{b / 1048576:.1f}M"
    if b >= 1024:
        return f"{b / 1024:.1f}K"
    return f"{b}B"


def format_markdown(report: List[Dict[str, object]]) -> str:
    lines: List[str] = []
    for payload_block in report:
        lines.append(f"### {payload_block['payload']} ({payload_block['payload_bytes']} bytes)")
        lines.append(
            "| Codec | Compressed | Ratio | Saved | Compress us | Decompress us "
            "| Enc RAM | Dec RAM |"
        )
        lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
        for row in payload_block["results"]:
            lines.append(
                f"| {row['codec']} "
                f"| {row['compressed_bytes']} "
                f"| {row['ratio']:.3f}x "
                f"| {row['saved_bytes']} "
                f"| {row['compress_us']:.2f} "
                f"| {row['decompress_us']:.2f} "
                f"| {_fmt_ram(row['encode_ram'])} "
                f"| {_fmt_ram(row['decode_ram'])} |"
            )
        lines.append("")
    return "\n".join(lines).strip() + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Benchmark picocompress vs heatshrink and brotli.")
    parser.add_argument("--dll", default="picocompress.dll", help="Path to picocompress DLL.")
    parser.add_argument("--json-out", default="", help="Optional path to write JSON report.")
    args = parser.parse_args()

    pico = PicoCodec(args.dll)

    SIZES = [1024, 2048, 4096, 8192, 16384, 32768, 131072, 524288, 1048576]

    # Original small payloads
    report = [
        run_payload("pattern-508", make_pattern_payload_508(), pico),
        run_payload("utf8-int-508", make_utf8_plus_int_payload_508(), pico),
        run_payload("random-508", make_random_payload_508(), pico),
        run_payload("ascii-254", make_ascii_payload_254(), pico),
    ]

    # Realistic file-type payloads
    report += [
        run_payload("json-508", make_json_508(), pico),
        run_payload("lorem-508", make_lorem_508(), pico),
        run_payload("rgb-icon-508", make_rgb_icon_508(), pico),
        run_payload("jpeg-508", make_tiny_jpeg_508(), pico),
        run_payload("json-4K-pretty", make_json_4k_pretty(), pico),
        run_payload("json-4K-minified", make_json_4k_minified(), pico),
    ]

    # Scaled payloads at each size
    for sz in SIZES:
        tag = f"{sz // 1024}K" if sz >= 1024 else str(sz)
        report.append(run_payload(f"random-{tag}", make_random(sz), pico))
        report.append(run_payload(f"sparse-{tag}", make_sparse(sz), pico))
        report.append(run_payload(f"uint32-{tag}", make_uint32_array(sz), pico))
        report.append(run_payload(f"utf8-prose-{tag}", make_utf8_prose(sz), pico))
        report.append(run_payload(f"uk-addr-{tag}", make_uk_names_addresses(sz), pico))
        report.append(run_payload(f"order-pos-{tag}", make_order_positional(sz), pico))

    print(json.dumps(report, indent=2))
    print()
    print(format_markdown(report))

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)


if __name__ == "__main__":
    main()
