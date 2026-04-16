#!/usr/bin/env python3
"""
Offline phoneme-driven dictionary generator for picocompress.

Input:  Mixed corpus of English text, JSON, logs, identifiers (UTF-8).
Output: Packed C dictionary array optimised for picocompress static dict.

Pipeline:
  1. Tokenise corpus into words
  2. Map words → phoneme sequences (lightweight heuristic, not CMU exact)
  3. Extract frequent phoneme n-grams (2–6 phonemes)
  4. Map back to byte sequences
  5. Score: frequency × length² × early-occurrence-weight
  6. Deduplicate overlapping patterns
  7. Emit C array within size budget

No runtime phoneme logic — purely offline training.
"""

import argparse
import collections
import re
from typing import Dict, List, Tuple

# ---------------------------------------------------------------------------
# Lightweight phoneme mapping (ARPAbet-inspired heuristic)
# ---------------------------------------------------------------------------

DIGRAPHS = [
    ("tion", "SHUN"), ("sion", "ZHUN"), ("ight", "AYT"),
    ("ough", "AW"),   ("ment", "MENT"), ("ness", "NES"),
    ("able", "ABUL"), ("ible", "IBUL"), ("ing", "ING"),
    ("ous", "US"),    ("th", "TH"),     ("ch", "CH"),
    ("sh", "SH"),     ("ph", "F"),      ("wh", "W"),
    ("ck", "K"),      ("ng", "NG"),     ("qu", "KW"),
    ("er", "ER"),     ("or", "OR"),     ("ar", "AR"),
    ("ee", "EE"),     ("ea", "EE"),     ("oo", "OO"),
    ("ou", "OW"),     ("ow", "OW"),     ("oi", "OY"),
    ("ai", "AY"),     ("ay", "AY"),     ("ey", "AY"),
    ("ie", "EE"),     ("ei", "AY"),
]

VOWELS = set("aeiou")


def word_to_phonemes(word: str) -> List[str]:
    """Lightweight heuristic phoneme conversion."""
    w = word.lower()
    phonemes: List[str] = []
    i = 0
    while i < len(w):
        matched = False
        for pat, phon in DIGRAPHS:
            if w[i:].startswith(pat):
                phonemes.append(phon)
                i += len(pat)
                matched = True
                break
        if not matched:
            phonemes.append(w[i].upper())
            i += 1
    return phonemes


# ---------------------------------------------------------------------------
# Corpus processing
# ---------------------------------------------------------------------------

def extract_words(text: str) -> List[str]:
    return re.findall(r"[a-zA-Z]{2,}", text)


def extract_byte_patterns(
    corpus: bytes,
    min_len: int = 2,
    max_len: int = 8,
    early_window: int = 512,
    sample_stride: int = 1,
    max_corpus: int = 256_000,
) -> List[Tuple[bytes, float]]:
    """Extract and score frequent byte patterns from corpus.

    For large corpora (>max_corpus), samples every Nth position to keep
    runtime bounded while preserving frequency ranking.
    """
    corpus_len = len(corpus)
    if corpus_len > max_corpus:
        sample_stride = max(1, corpus_len // max_corpus)

    pattern_freq: Dict[bytes, int] = collections.Counter()
    pattern_early: Dict[bytes, int] = collections.Counter()

    for pos in range(0, corpus_len - max_len, sample_stride):
        window = corpus[pos:pos + max_len]
        block_offset = pos % 508
        is_early = block_offset < early_window
        for length in range(min_len, max_len + 1):
            pat = window[:length]
            # Skip patterns containing newlines/CR or all-same-byte runs
            if 0x0A in pat or 0x0D in pat:
                continue
            if pat[0] == pat[-1] and len(set(pat)) <= 1:
                continue
            pattern_freq[pat] += 1
            if is_early:
                pattern_early[pat] += 1

    # Scale frequencies back up if we sampled
    scale = sample_stride

    scored: List[Tuple[bytes, float]] = []
    for pat, freq in pattern_freq.items():
        real_freq = freq * scale
        if real_freq < 3:
            continue
        length = len(pat)
        early_count = pattern_early.get(pat, 0) * scale
        early_weight = 1.0 + (early_count / max(real_freq, 1)) * 0.5
        score = real_freq * (length ** 2) * early_weight
        scored.append((pat, score))

    scored.sort(key=lambda x: -x[1])
    return scored


def extract_phoneme_patterns(
    corpus_text: str,
    min_freq: int = 5,
) -> List[Tuple[bytes, float]]:
    """Extract patterns using phoneme analysis of English content."""
    words = extract_words(corpus_text)
    word_freq = collections.Counter(words)
    phoneme_substrings: Dict[str, int] = collections.Counter()

    for word, freq in word_freq.most_common(2000):
        if len(word) < 2:
            continue
        for n in range(2, min(7, len(word) + 1)):
            for start in range(len(word) - n + 1):
                sub = word.lower()[start:start + n]
                phoneme_substrings[sub] += freq

    scored: List[Tuple[bytes, float]] = []
    for sub, freq in phoneme_substrings.items():
        if freq < min_freq:
            continue
        pat = sub.encode("utf-8")
        if len(pat) < 2 or len(pat) > 8:
            continue
        score = freq * (len(pat) ** 2)
        scored.append((pat, score))

    scored.sort(key=lambda x: -x[1])
    return scored


def deduplicate_patterns(
    patterns: List[Tuple[bytes, float]],
    max_entries: int = 64,
    size_budget: int = 512,
) -> List[bytes]:
    selected: List[bytes] = []
    total_bytes = 0

    for pat, _score in patterns:
        if len(selected) >= max_entries:
            break
        if total_bytes + len(pat) > size_budget:
            continue
        # Skip if this is a substring of an already-selected pattern
        if any(pat in sel for sel in selected):
            continue
        # Skip if an already-selected pattern is a substring of this one
        # (the shorter one scored higher, so it's more general)
        if any(sel in pat for sel in selected):
            continue
        # Skip near-duplicates: if shifting by 1 byte yields a selected pattern
        if any(pat[1:] == sel[:-1] or pat[:-1] == sel[1:] for sel in selected if len(sel) == len(pat)):
            continue
        selected.append(pat)
        total_bytes += len(pat)

    return selected


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def emit_c_array(patterns: List[bytes], name: str = "pc_phoneme_dict") -> str:
    lines = [
        f"/* Auto-generated phoneme dictionary — {len(patterns)} entries,",
        f" * {sum(len(p) for p in patterns)} payload bytes */",
        "",
    ]

    for i, pat in enumerate(patterns):
        c_bytes = ", ".join(f"0x{b:02X}" for b in pat)
        try:
            printable = pat.decode("ascii")
            if all(0x20 <= b < 0x7F for b in pat):
                comment = f"  /* {printable} */"
            else:
                comment = ""
        except (UnicodeDecodeError, ValueError):
            comment = ""
        lines.append(f"static const uint8_t {name}_{i:02d}[] = {{ {c_bytes} }};{comment}")

    lines.append("")
    lines.append(f"static const pc_dict_entry_t {name}[{len(patterns)}] = {{")
    for i, pat in enumerate(patterns):
        comma = "," if i < len(patterns) - 1 else ""
        lines.append(f"    {{ {name}_{i:02d}, {len(pat)} }}{comma}")
    lines.append("};")

    return "\n".join(lines)


def emit_metadata(
    patterns: List[bytes],
    scores: Dict[bytes, float],
    freqs: Dict[bytes, int],
) -> str:
    lines = [
        "# Phoneme dictionary metadata",
        f"# Entries: {len(patterns)}",
        f"# Total payload: {sum(len(p) for p in patterns)} bytes",
        "",
        f"{'#':>3s}  {'Pattern':<20s}  {'Len':>3s}  {'Freq':>6s}  {'Score':>10s}",
        f"{'---':>3s}  {'--------------------':<20s}  {'---':>3s}  {'------':>6s}  {'----------':>10s}",
    ]
    for i, pat in enumerate(patterns):
        try:
            display = pat.decode("ascii")
            if not all(0x20 <= b < 0x7F for b in pat):
                display = pat.hex()
        except (UnicodeDecodeError, ValueError):
            display = pat.hex()
        lines.append(f"{i:3d}  {display:<20s}  {len(pat):3d}  {freqs.get(pat,0):6d}  {scores.get(pat,0):10.0f}")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate phoneme-driven static dictionary for picocompress"
    )
    parser.add_argument("corpus", nargs="+", help="Corpus files (text, JSON, logs)")
    parser.add_argument("--budget", type=int, default=512, help="Payload size budget (bytes)")
    parser.add_argument("--entries", type=int, default=64, help="Max dictionary entries")
    parser.add_argument("--c-out", default="", help="Write C array to file")
    parser.add_argument("--meta-out", default="", help="Write metadata to file")
    args = parser.parse_args()

    print(f"Loading {len(args.corpus)} corpus files...")
    raw = bytearray()
    for path in args.corpus:
        with open(path, "rb") as f:
            raw.extend(f.read())
    raw_corpus = bytes(raw)
    text_corpus = raw_corpus.decode("utf-8", errors="replace")
    print(f"  Total: {len(raw_corpus)} bytes")

    print("Extracting byte patterns...")
    byte_patterns = extract_byte_patterns(raw_corpus)
    print(f"  {len(byte_patterns)} candidate byte patterns")

    print("Extracting phoneme patterns...")
    phoneme_patterns = extract_phoneme_patterns(text_corpus)
    print(f"  {len(phoneme_patterns)} candidate phoneme patterns")

    # Merge scores
    all_scores: Dict[bytes, float] = {}
    for pat, score in byte_patterns:
        all_scores[pat] = all_scores.get(pat, 0) + score
    for pat, score in phoneme_patterns:
        all_scores[pat] = all_scores.get(pat, 0) + score * 1.5

    # Count raw frequencies — only for top candidates (not all patterns)
    ranked_pre = sorted(all_scores.items(), key=lambda x: -x[1])[:args.entries * 4]
    all_freqs: Dict[bytes, int] = {}
    for pat, _score in ranked_pre:
        count = 0
        start = 0
        while True:
            idx = raw_corpus.find(pat, start)
            if idx < 0:
                break
            count += 1
            start = idx + 1
        all_freqs[pat] = count

    ranked = sorted(all_scores.items(), key=lambda x: -x[1])
    selected = deduplicate_patterns(ranked, args.entries, args.budget)
    selected.sort(key=lambda p: (len(p), p))

    print(f"\nSelected {len(selected)} entries, "
          f"{sum(len(p) for p in selected)} payload bytes "
          f"(budget: {args.budget})")
    print()

    c_code = emit_c_array(selected)
    print(c_code)
    print()

    if args.c_out:
        with open(args.c_out, "w") as f:
            f.write(c_code + "\n")
        print(f"C array written to {args.c_out}")

    meta = emit_metadata(selected, all_scores, all_freqs)
    print(meta)

    if args.meta_out:
        with open(args.meta_out, "w") as f:
            f.write(meta + "\n")
        print(f"Metadata written to {args.meta_out}")


if __name__ == "__main__":
    main()
