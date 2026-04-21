# Context-Biased Dictionary Search (Turing Crib Mode)

picocompress doesn't search its dictionary the same way at every byte position. It knows *where it is* in the data and searches smarter.

## The Problem

A static dictionary has 96 entries. Scanning all 96 at every byte position is wasteful — most entries can't possibly match based on context. A JSON stream opening with `{` will never match `PROGRAM` or `ould ` in the first few bytes. An English suffix like `tion` won't appear right after a `}`.

## The Insight

Data has structure. The first bytes of a block are predictable (format signatures, opening braces, protocol headers). Bytes after delimiters like `}`, `,`, `\n` are predictable too (keys, tags, keywords). Everything else is body text where phonemes and common words dominate.

This is the same principle Turing used at Bletchley Park: if you know *where* in the message you are, you can dramatically narrow the search space. A "crib" — known or expected plaintext — lets you skip most possibilities and jump straight to the likely candidates.

## How It Works

The dictionary is physically partitioned into three contiguous regions:

| Region | Entries | When | What's in it |
|---|---|---|---|
| **START** | 0–15 | First 8 bytes of a block | `https://`, `HTTP`, `JSON`, `true`, `null`, `name`, `status` |
| **BOUNDARY** | 16–47 | After `}` `]` `,` `;` `:` `.` `\n` | `": "`, `},\n"`, `</div`, `. The `, `DIM`, `FOR`, `PRINT` |
| **BODY** | 48–95 | Everything else | `tion`, `ment`, `ness`, `ing`, `the`, `value`, `content` |

At each byte position, the encoder checks **where it is** (~3 instructions):

1. **Position in block < 8?** → search START first
2. **Previous byte is a delimiter?** → search BOUNDARY first
3. **Otherwise** → search BODY first

It prescans the top 8 entries of the biased region. If it gets a strong match (≥4 bytes saved), it accepts immediately — no need to check the other 88 entries. If the prescan misses, it falls through to the full dictionary, skipping the entries it already checked.

## Why It Works

Most dictionary hits cluster by context. In a JSON payload, boundary patterns like `": "` and `":"` fire constantly after `:` and `,` — they're in the first 8 entries the encoder checks. English suffixes like `tion` and `ment` fire mid-word in body text — they're in the first 8 entries for BODY context.

The result: the encoder finds matches faster (fewer probes on average), while maintaining identical compression ratios. The dictionary self-disables entirely on binary data via a first-byte probe, so non-text workloads pay zero dict cost.

## What It Costs

- **Zero** extra memory
- **Zero** decoder changes — the token format is unchanged
- **~5 extra instructions** per position (context check + prescan bounds)
- **No** dynamic state, no adaptive learning, no runtime allocation

The dictionary data doesn't change size. The entries are simply reordered. Same 96 entries, same 441 payload bytes, same flash footprint — just searched in a smarter order.
