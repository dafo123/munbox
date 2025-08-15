# StuffIt Method 13 ("*.sit" – LZSS + Huffman) Decompression Specification

This document is a complete, authoritative specification of StuffIt Method 13 as implemented in `lib/layers/sit13.c`.

## References and Acknowledgements

Primary sources:

- Historical reverse‑engineering notes: https://github.com/mietek/theunarchiver/wiki/StuffItAlgorithm13
- The Unarchiver project (original author: Dag Ågren)
- General background on LZSS + Huffman (e.g. DEFLATE family – conceptual only)

## Introduction

Method 13 combines an LZSS style sliding‑window dictionary (64 KiB) with three prefix (Huffman) codes:
1. A literal/length code used after literals ("first tree")
2. A literal/length code used after matches ("second tree")
3. A distance (offset) code giving the number of low bits to read for match distances

Two operating modes exist per block:
* Dynamic mode (header high nibble = 0): all three trees are serialized using a meta‑code that compresses code lengths.
* Predefined mode (header high nibble 1–5): code lengths are chosen from five built‑in tables.

Distinctives vs many similar formats:
* Two alternating literal/length trees (context switching after matches) – not just adaptive frequencies but structurally different Huffman sets.
* Distance alphabet encodes the bit‑length of an LSB-first distance whose highest bit is implicit (except distance=1 special case).
* No end‑of‑block / end sentinel in the compressed stream; decoding halts strictly after producing the externally supplied uncompressed size. A historical document mentions symbol 320 as an end marker – the canonical implementation rejects any symbol > 319.

## Bit Order and Fundamental Conventions

* Bits are consumed least significant bit first within each byte (bit 0, then bit 1, … bit 7) of the little‑endian byte sequence.
* Multi‑bit fields (including meta‑code extension bits, length extra bits, distance low bits) are formed the same LSB‑first way.
* The bitstream is never force‑aligned after the header; decoding proceeds bit‑continuously.

## Header Byte

Single byte (call it `H`). Layout (bit 7 = MSB):

```
 7 6 5 4 3 2 1 0
+-----+---+-----+
| SET |S|  K    |
+-----+---+-----+
```

Field semantics:
- SET (bits 7–4): 0 = dynamic (all three trees serialized). 1..5 = predefined code set number. Other values invalid.
- S (bit 3): 1 = second literal/length tree is identical to the first (tree sharing). 0 = second tree serialized independently (dynamic mode) or taken from predefined set (predefined mode).
- K (bits 2–0): In dynamic mode only, the number of symbols in the distance tree is `10 + K` (K ∈ [0,7]). Ignored in predefined mode (sizes are fixed per set).

## Sliding Window

* Size: 65,536 bytes (indices wrap with `& 0xFFFF`).
* Initialized to all zero bytes before decoding the first symbol of the block.
* Every produced output byte (literal or copied match byte) is written into the window and advances the position.

## Symbol Alphabets

Literal/Length trees always conceptually have 321 symbol slots numbered 0..320. Valid (used) symbols are:
- 0..255: literal byte values.
- 256..317: match lengths `L = symbol - 253` (range 3..64).
- 318: long match length: read 10 extra bits `x` -> `L = x + 65` (range 65..1088).
- 319: very long match length: read 15 extra bits `x` -> `L = x + 65` (range 65..32,832).
- 320: MUST NOT occur in a valid Method 13 stream for this implementation. Encountering it (or any symbol > 319) is a fatal error. (some notes claimed 320 was an end marker)

Distance (offset) tree symbols give the number of encoded bits minus one for the distance, except symbol 0 which gives the fixed distance 1.

## Dynamic (Embedded) Trees

When SET = 0 the bitstream serializes (in order):
1. (Optionally) first literal/length tree
2. (Optionally) second literal/length tree (skipped if S=1, meaning tree sharing)
3. Distance tree (size = 10 + K symbols)

All three lists of code lengths are themselves compressed using a fixed meta‑code (see Meta‑Code section). Each *list* must produce exactly the required number of code length entries before moving on.

### Meta‑Code (Fixed Huffman) Definition

Size 37. Codes are added to a decoding tree using the (word, length) pairs below. Words are interpreted MSB‑first when building the table; their transmitted bits are still read LSB‑first by the input routine, so do not reverse them manually – replicate the canonical procedure.

```c
#define METACODE_SIZE 37
const uint16_t meta_code_words[METACODE_SIZE] = {
        0x00dd, 0x001a, 0x0002, 0x0003, 0x0000, 0x000f, 0x0035, 0x0005,
        0x0006, 0x0007, 0x001b, 0x0034, 0x0001, 0x0001, 0x000e, 0x000c,
        0x0036, 0x01bd, 0x0006, 0x000b, 0x000e, 0x001f, 0x001e, 0x0009,
        0x0008, 0x000a, 0x01bc, 0x01bf, 0x01be, 0x01b9, 0x01b8, 0x0004,
        0x0002, 0x0001, 0x0007, 0x000c, 0x0002
};
const int meta_code_lengths[METACODE_SIZE] = {
        0xB, 0x8, 0x8, 0x8, 0x8, 0x7, 0x6, 0x5, 0x5, 0x5, 0x5, 0x6, 0x5, 0x6, 0x7, 0x7,
        0x9, 0xC, 0xA, 0xB, 0xB, 0xC, 0xC, 0xB, 0xB, 0xB, 0xC, 0xC, 0xC, 0xC, 0xC, 0x5,
        0x2, 0x2, 0x3, 0x4, 0x5
};
```

### Meta‑Code Output (Code Length RLE Commands)

Maintain a variable `L` (current length), initial value 0. For each output position `i` until the target count is reached:
1. Decode one meta‑symbol `m`.
2. Interpret:
     * 0..30: `L = m + 1`
     * 31: `L = 0` (symbol absent)
     * 32: `L = L + 1`
     * 33: `L = L - 1`
     * 34: Read 1 bit `b`. If `b == 1` emit one extra entry with length `L` immediately (this duplicates the previous length once). Regardless of `b`, continue to the normal emit step.
     * 35: Read 3 bits `n`; repeat length `L` exactly `(n + 2)` additional times (emit those entries immediately, advancing `i`).
     * 36: Read 6 bits `n`; repeat length `L` exactly `(n + 10)` additional times.
3. Emit (store) `L` for the current position (unless it was already emitted inside a repeat operation – the implementation logic emits in the loop header after handling repeats; reproducing that order yields identical results).

Stop only after exactly the required number of code length entries have been produced for that tree.

### Tree Sharing

If S = 1 only the first literal/length tree is serialized; the second tree is a direct alias (identical structure and lengths). If S = 0 both literal/length trees are serialized consecutively.

### Distance Tree Size

Distance tree symbol count = `10 + K`, K from header bits 2..0.

## Predefined Trees (Sets 1–5)

When SET ∈ {1..5} all three trees are constructed from fixed code length tables. Distance tree symbol counts are fixed per set: {11, 13, 14, 11, 11}. Full tables appear in the Tables section.

## Canonical Code Construction

For any list of code lengths (0 = symbol absent):
1. Group symbols by increasing length; within each length order by ascending symbol number.
2. Let `code = 0`. For each length `len` from 1 upward:
     * Left‑shift `code` by 1 after finishing each length bucket.
     * Assign sequential code values to all symbols of that length.
3. Insert codes MSB‑first into a binary decoding tree. Bits will be *read* LSB‑first from the stream; do not reverse the codes – the canonical implementation matches this pairing.

Any code length may legally be zero. Maximum observed code length (in predefined tables) is 18.

## Decoding Procedure

State variables:
* Current literal/length tree pointer (starts at first tree; set to first after a literal; set to second after a completed match)
* Output position (number of bytes produced)
* 64 KiB window buffer (circular)
* (Optional streaming) pending match copy state (the reference code allows yielding mid‑match)

Loop until desired uncompressed byte count reached:
1. Decode one symbol from current literal/length tree.
2. If `< 256`: emit that literal byte, store into window, switch to first tree.
3. Else decode match length per table:
     * 256..317: `L = symbol - 253`
     * 318: `L = read_bits(10) + 65`
     * 319: `L = read_bits(15) + 65`
     * Otherwise: error.
4. Decode distance:
     * Get distance symbol `d` from distance tree.
     * If `d == 0`: distance = 1.
     * Else read `(d - 1)` bits value `x`; distance = `(1 << (d - 1)) + x + 1`.
     * Distance range is 1..65536 (MUST NOT exceed window size).
5. Copy `L` bytes from `(out_pos - distance)` forward (wrapping with `& 0xFFFF`), writing each byte both to output and window. Permit overlap (standard LZ semantics).
6. After finishing the match, switch to the second literal/length tree for the next symbol.

## Termination

There is no in‑band termination symbol. Decompression MUST stop exactly after generating the previously known uncompressed size supplied by container metadata. Emission of symbol 320 (historical end marker) is treated as invalid input.

Errors: premature exhaustion of compressed input, symbol 320 or >319, distance exceeding window size, or malformed code trees.

## Limits and Ranges

* Window size: 65,536 bytes
* Literal symbols: 0..255
* Match length ranges: 3..64 (256..317), 65..1088 (318), 65..32,832 (319)
* Maximum match length: 32,832 bytes
* Distance range: 1..65,536
* Distance tree symbol count: dynamic 10..17, predefined per set (11,13,14,11,11)
* Code length max (observed): 18 (predefined); dynamic meta‑code permits larger but decoder imposes no explicit upper bound besides practical tree construction

## Streaming Considerations (Informative)

An implementation may output incrementally. For large matches exceeding an application buffer size, maintain `pending_match_len` and `pending_match_src` (the source index within the sliding window) so that copying can resume across calls without re‑decoding symbols.

## Reference Pseudocode (Non‑Normative)

```pseudo
init_window_zero()
read header H
if SET == 0:
    build meta-code tree (fixed table)
    first = decode_length_list(321)
    second = first if S==1 else decode_length_list(321)
    dist = decode_length_list(10 + K)
else if 1 <= SET <= 5:
    (first, second, dist) = predefined_tables[SET]
else error
build canonical decoding trees for first, second, dist
cur = first
while produced < target_size:
    sym = decode(cur)
    if sym < 256:
         emit literal sym; cur = first; continue
    if 256 <= sym <= 317: L = sym - 253
    else if sym == 318: L = read_bits(10) + 65
    else if sym == 319: L = read_bits(15) + 65
    else error
    d_sym = decode(dist)
    if d_sym == 0: D = 1 else D = (1 << (d_sym - 1)) + read_bits(d_sym - 1) + 1
    copy L bytes from output_pos - D (wrapping 64K) to output/window
    cur = second
```

## Tables (Normative)

The following code length tables define the five predefined code sets. Each row comprises exactly 321 code lengths for the corresponding literal/length tree (first then second). Distance trees list their full symbol count for the set.

### first_tree_lengths[5][321]

```c
static const int8_t first_tree_lengths[5][321] = { /* flattened in implementation; reproduced verbatim */
        /* Set 1 */
        4,5,7,8,8,9,9,9,9,7,9,9,9,8,9,9,9,9,9,9,9,9,9,10,9,9,10,10,9,10,9,9,5,9,9,9,9,10,9,9,9,9,9,9,9,9,7,9,9,8,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,8,9,9,8,8,9,9,9,9,9,9,9,7,8,9,7,9,9,7,7,9,9,9,9,10,9,10,10,10,9,9,9,5,9,8,7,5,9,8,8,7,9,9,8,
        8,5,5,7,10,5,8,5,8,9,9,9,9,9,10,9,9,10,9,9,10,10,10,10,10,10,10,9,10,10,10,10,10,10,10,9,10,10,10,10,10,10,10,10,
        10,10,10,10,10,10,10,9,10,10,10,10,10,10,10,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,10,10,10,10,10,
        9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,10,10,10,10,10,
        10,10,10,10,10,10,9,9,10,10,9,10,10,10,10,10,10,10,9,10,10,10,9,10,9,5,6,5,5,8,9,9,9,9,9,9,10,10,10,9,10,10,10,10,
        10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,10,9,9,9,10,9,10,9,10,9,10,9,10,10,10,9,10,9,10,10,9,9,9,6,9,
        9,10,9,5,
        /* Set 2 */
        4,7,7,8,7,8,8,8,8,7,8,7,8,7,9,8,8,8,9,9,9,9,10,10,9,10,10,10,10,10,9,9,5,9,8,9,9,11,10,9,8,9,9,9,8,9,7,8,8,8,9,9,
        9,9,9,10,9,9,9,10,9,9,10,9,8,8,7,7,7,8,8,9,8,8,9,9,8,8,7,8,7,10,8,7,7,9,9,9,9,10,10,11,11,11,10,9,8,6,8,7,7,5,7,7,
        7,6,9,8,6,7,6,6,7,9,6,6,6,7,8,8,8,8,9,10,9,10,9,9,8,9,10,10,9,10,10,9,9,10,10,10,10,10,10,10,9,10,10,11,10,10,10,
        10,10,10,10,11,10,11,10,10,9,11,10,10,10,10,10,10,9,9,10,11,10,11,10,11,10,12,10,11,10,12,11,12,10,12,10,11,10,11,
        11,11,9,10,11,11,11,12,12,10,10,10,11,11,10,11,10,10,9,11,10,11,10,11,11,11,10,11,11,12,11,11,10,10,10,11,10,10,
        11,11,12,10,10,11,11,12,11,11,10,11,9,12,10,11,11,11,10,11,10,11,10,11,9,10,9,7,3,5,6,6,7,7,8,8,8,9,9,9,11,10,10,
        10,12,13,11,12,12,11,13,12,12,11,12,12,13,12,14,13,14,13,15,13,14,15,15,14,13,15,15,14,15,14,15,15,14,15,13,13,14,
        15,15,14,14,16,16,15,15,15,12,15,10,
        /* Set 3 */
        6,6,6,6,6,9,8,8,4,9,8,9,8,9,9,9,8,9,9,10,8,10,10,10,9,10,10,10,9,10,10,9,9,9,8,10,9,10,9,10,9,10,9,10,9,9,8,9,8,9,
        9,9,10,10,10,10,9,9,9,10,9,10,9,9,7,8,8,9,8,9,9,9,8,9,9,10,9,9,8,9,8,9,8,8,8,9,9,9,9,9,10,10,10,10,10,9,8,8,9,8,9,
        7,8,8,9,8,10,10,8,9,8,8,8,10,8,8,8,8,9,9,9,9,10,10,10,10,10,9,7,9,9,10,10,10,10,10,9,10,10,10,10,10,10,9,9,10,10,
        10,10,10,10,10,10,9,10,10,10,10,10,10,9,10,10,10,10,10,10,10,9,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,
        10,10,10,10,9,8,9,10,10,10,10,10,10,10,10,10,10,10,9,10,10,10,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,9,10,
        10,10,10,10,10,9,10,10,10,10,10,10,9,9,9,10,10,10,10,10,10,9,9,10,9,9,8,9,8,9,4,6,6,6,7,8,8,9,9,10,10,10,9,10,10,
        10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,7,10,10,10,7,10,10,7,7,7,7,7,6,7,10,7,7,10,7,7,7,6,7,6,6,7,7,6,6,9,6,
        9,10,6,10,2,
        /* Set 4 */
        2,6,6,7,7,8,7,8,7,8,8,9,8,9,9,9,8,8,9,9,9,10,10,9,8,10,9,10,9,10,9,9,6,9,8,9,9,10,9,9,9,10,9,9,9,9,8,8,8,8,8,9,9,
        9,9,9,9,9,9,9,9,10,10,9,7,7,8,8,8,8,9,9,7,8,9,10,8,8,7,8,8,10,8,8,8,9,8,9,9,10,9,11,10,11,9,9,8,7,9,8,8,6,8,8,8,7,
        10,9,7,8,7,7,8,10,7,7,7,8,9,9,9,9,10,11,9,11,10,9,7,9,10,10,10,11,11,10,10,11,10,10,10,11,11,10,9,10,10,11,10,11,
        10,11,10,10,10,11,10,11,10,10,9,10,10,11,10,10,10,10,9,10,10,10,10,11,10,11,10,11,10,11,11,11,10,12,10,11,10,11,
        10,11,11,10,8,10,10,11,10,11,11,11,10,11,10,11,10,11,11,11,9,10,11,11,10,11,11,11,10,11,11,11,10,10,10,10,10,11,
        10,10,11,11,10,10,9,11,10,10,11,11,10,10,10,11,10,10,10,10,10,10,9,11,10,10,8,10,8,6,5,6,6,7,7,8,8,8,9,10,11,10,
        10,11,11,12,12,10,11,12,12,12,12,13,13,13,13,13,12,13,13,15,14,12,14,15,16,12,12,13,15,14,16,15,17,18,15,17,16,15,
        15,15,15,13,13,10,14,12,13,17,17,18,10,17,4,
        /* Set 5 */
        7,9,9,9,9,9,9,9,9,8,9,9,9,7,9,9,9,9,9,9,9,9,9,10,9,10,9,10,9,10,9,9,5,9,7,9,9,9,9,9,7,7,7,9,7,7,8,7,8,8,7,7,9,9,9,
        9,7,7,7,9,9,9,9,9,9,7,9,7,7,7,7,9,9,7,9,9,7,7,7,7,7,9,7,8,7,9,9,9,9,9,9,9,9,9,9,9,9,7,8,7,7,7,8,8,6,7,9,7,7,8,7,5,
        6,9,5,7,5,6,7,7,9,8,9,9,9,9,9,9,9,9,10,9,10,10,10,9,9,10,10,10,10,10,10,10,9,10,10,10,10,10,10,10,10,10,10,10,9,10,
        10,10,9,10,10,10,9,9,10,9,9,9,9,10,10,10,10,10,10,10,10,10,10,10,9,10,10,10,10,10,10,10,10,10,9,10,10,10,9,10,10,
        10,9,9,9,10,10,10,10,10,9,10,9,10,10,9,10,10,9,10,10,10,10,10,10,10,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
        10,9,10,10,10,10,10,10,10,9,10,9,10,9,10,10,9,5,6,8,8,7,7,7,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,10,10,5,10,8,9,8,9
};
```

### second_tree_lengths[5][321]

```c
static const int8_t second_tree_lengths[5][321] = { /* flattened; reproduced verbatim */
        /* Set 1 */
        4,5,6,6,7,7,6,7,7,7,6,8,7,8,8,8,8,9,6,9,8,9,8,9,9,9,8,10,5,9,7,9,6,9,8,10,9,10,8,8,9,9,7,9,8,9,8,9,8,8,6,9,9,8,8,
        9,9,10,8,9,9,10,8,10,8,8,8,8,8,9,7,10,6,9,9,11,7,8,8,9,8,10,7,8,6,9,10,9,9,10,8,11,9,11,9,10,9,8,9,8,8,8,8,10,9,9,
        10,10,8,9,8,8,8,11,9,8,8,9,9,10,8,11,10,10,8,10,9,10,8,9,9,11,9,11,9,10,10,11,10,12,9,12,10,11,10,11,9,10,10,11,
        10,11,10,11,10,11,10,10,10,9,9,9,8,7,6,8,11,11,9,12,10,12,9,11,11,11,10,12,11,11,10,12,10,11,10,10,10,11,10,11,11,
        11,9,12,10,12,11,12,10,11,10,12,11,12,11,12,11,12,10,12,11,12,11,11,10,12,10,11,10,12,10,12,10,12,10,11,11,11,10,
        11,11,11,10,12,11,12,10,10,11,11,9,12,11,12,10,11,10,12,10,11,10,12,10,11,10,7,5,4,6,6,7,7,7,8,8,7,7,6,8,6,7,7,9,
        8,9,9,10,11,11,11,12,11,10,11,12,11,12,11,12,12,12,12,11,12,12,11,12,11,12,11,13,11,12,10,13,10,14,14,13,14,15,14,
        16,15,15,18,18,18,9,18,8,
        /* Set 2 */
        5,6,6,6,6,7,7,7,7,7,7,8,7,8,7,7,7,8,8,8,8,9,8,9,8,9,9,9,7,9,8,8,6,9,8,9,8,9,8,9,8,9,8,9,8,9,8,8,8,8,8,9,8,9,8,9,
        9,10,8,10,8,9,9,8,8,8,7,8,8,9,8,9,7,9,8,10,8,9,8,9,8,9,8,8,8,9,9,9,9,10,9,11,9,10,9,10,8,8,8,9,8,8,8,9,9,8,9,10,8,
        9,8,8,8,11,8,7,8,9,9,9,9,10,9,10,9,10,9,8,8,9,9,10,9,10,9,10,8,10,9,10,9,11,10,11,9,11,10,10,10,11,9,11,9,10,9,11,
        9,11,10,10,9,10,9,9,8,10,9,11,9,9,9,11,10,11,9,11,9,11,9,11,10,11,10,11,10,11,9,10,10,11,10,10,8,10,9,10,10,11,9,
        11,9,10,10,11,9,10,10,9,9,10,9,10,9,10,9,10,9,11,9,11,10,10,9,10,9,11,9,11,9,11,9,10,9,11,9,11,9,11,9,10,8,11,9,10,
        9,10,9,10,8,10,8,9,8,9,8,7,4,4,5,6,6,6,7,7,7,7,8,8,8,7,8,8,9,9,10,10,10,10,10,10,11,11,10,10,12,11,11,12,12,11,12,
        12,11,12,12,12,12,12,12,11,12,11,13,12,13,12,13,14,14,14,15,13,14,13,14,18,18,17,7,16,9,
        /* Set 3 */
        5,6,6,6,6,7,7,7,6,8,7,8,7,9,8,8,7,7,8,9,9,9,9,10,8,9,9,10,8,10,9,8,6,10,8,10,8,10,9,9,9,9,9,10,9,9,8,9,8,9,8,9,9,
        10,9,10,9,9,8,10,9,11,10,8,8,8,8,9,7,9,9,10,8,9,8,11,9,10,9,10,8,9,9,9,9,8,9,9,10,10,10,12,10,11,10,10,8,9,9,9,8,
        9,8,8,10,9,10,11,8,10,9,9,8,12,8,9,9,9,9,8,9,10,9,12,10,10,10,8,7,11,10,9,10,11,9,11,7,11,10,12,10,12,10,11,9,11,
        9,12,10,12,10,12,10,9,11,12,10,12,10,11,9,10,9,10,9,11,11,12,9,10,8,12,11,12,9,12,10,12,10,13,10,12,10,12,10,12,
        10,9,10,12,10,9,8,11,10,12,10,12,10,12,10,11,10,12,8,12,10,11,10,10,10,12,9,11,10,12,10,12,11,12,10,9,10,12,9,10,
        10,12,10,11,10,11,10,12,8,12,9,12,8,12,8,11,10,11,10,11,9,10,8,10,9,9,8,9,8,7,4,3,5,5,6,5,6,6,7,7,8,8,8,7,7,7,9,8,
        9,9,11,9,11,9,8,9,9,11,12,11,12,12,13,13,12,13,14,13,14,13,14,13,13,13,12,13,13,12,13,13,14,14,13,13,14,14,14,14,
        15,18,17,18,8,16,10,
        /* Set 4 */
        4,5,6,6,6,6,7,7,6,7,7,9,6,8,8,7,7,8,8,8,6,9,8,8,7,9,8,9,8,9,8,9,6,9,8,9,8,10,9,9,8,10,8,10,8,9,8,9,8,8,7,9,9,9,9,
        9,8,10,9,10,9,10,9,8,7,8,9,9,8,9,9,9,7,10,9,10,9,9,8,9,8,9,8,8,8,9,9,10,9,9,8,11,9,11,10,10,8,8,10,8,8,9,9,9,10,9,
        10,11,9,9,9,9,8,9,8,8,8,10,10,9,9,8,10,11,10,11,11,9,8,9,10,11,9,10,11,11,9,12,10,10,10,12,11,11,9,11,11,12,9,11,
        9,10,10,10,10,12,9,11,10,11,9,11,11,11,10,11,11,12,9,10,10,12,11,11,10,11,9,11,10,11,10,11,9,11,11,9,8,11,10,11,
        11,10,7,12,11,11,11,11,11,12,10,12,11,13,11,10,12,11,10,11,10,11,10,11,11,11,10,12,11,11,10,11,10,10,10,11,10,12,
        11,12,10,11,9,11,10,11,10,11,10,12,9,11,11,11,9,11,10,10,9,11,10,10,9,10,9,7,4,5,5,5,6,6,7,6,8,7,8,9,9,7,8,8,10,9,
        10,10,12,10,11,11,11,11,10,11,12,11,11,11,11,11,13,12,11,12,13,12,12,12,13,11,9,12,13,7,13,11,13,11,10,11,13,15,
        15,12,14,15,15,15,6,15,5,
        /* Set 5 */
        8,10,11,11,11,12,11,11,12,6,11,12,10,5,12,12,12,12,12,12,12,13,13,14,13,13,12,13,12,13,12,15,4,10,7,9,11,11,10,9,
        6,7,8,9,6,7,6,7,8,7,7,8,8,8,8,8,8,9,8,7,10,9,10,10,11,7,8,6,7,8,8,9,8,7,10,10,8,7,8,8,7,10,7,6,7,9,9,8,11,11,11,10,
        11,11,11,8,11,6,7,6,6,6,6,8,7,6,10,9,6,7,6,6,7,10,6,5,6,7,7,7,10,8,11,9,13,7,14,16,12,14,14,15,15,16,16,14,15,15,
        15,15,15,15,15,15,14,15,13,14,14,16,15,17,14,17,15,17,12,14,13,16,12,17,13,17,14,13,13,14,14,12,13,15,15,14,15,17,
        14,17,15,14,15,16,12,16,15,14,15,16,15,16,17,17,15,15,17,17,13,14,15,15,13,12,16,16,17,14,15,16,15,15,13,13,15,13,
        16,17,15,17,17,17,16,17,14,17,14,16,15,17,15,15,14,17,15,17,15,16,15,15,16,16,14,17,17,15,15,16,15,17,15,14,16,16,
        16,16,16,12,4,4,5,5,6,6,6,7,7,7,8,8,8,8,9,9,9,9,9,10,10,10,11,10,11,11,11,11,11,12,12,12,13,13,12,13,12,14,14,12,
        13,13,13,13,14,12,13,13,14,14,14,13,14,14,15,15,13,15,13,17,17,17,9,17,7
};
```

### offset_tree_lengths[5]

```c
static const int8_t offset_tree_lengths[5][14] = {
        {5,6,3,3,3,3,3,3,3,4,6},          /* Set 1 (11 symbols) */
        {5,6,4,4,3,3,3,3,3,4,4,4,6},      /* Set 2 (13 symbols) */
        {6,7,4,4,3,3,3,3,3,4,4,4,5,7},    /* Set 3 (14 symbols) */
        {3,6,5,4,2,3,3,3,4,4,6},          /* Set 4 (11 symbols) */
        {6,7,7,6,4,3,2,2,3,3,6}           /* Set 5 (11 symbols) */
};
```

## Implementation Notes

* A practical decoder may preload four bytes at a time (like the reference) and extract arbitrary bit fields by masking after shifting out already‑consumed bits.
* Because bits are consumed LSB‑first, *do not* reverse canonical codes – instead build the binary tree from the assigned MSB‑first representation and drive it with single‑bit reads.
* Symbol 320: treat as error even though predefined tables include a (never used) code length for that position.

## Conformance Checklist

An implementation is conformant if it:
- Parses header fields exactly as specified.
- Reconstructs dynamic trees using meta‑code commands verbatim and the fixed meta‑code table.
- Builds canonical Huffman codes (ascending length / ascending symbol) without post‑reversal.
- Interprets symbols 0..319 exactly as defined and rejects 320.
- Computes distances with implicit top bit and +1 offset rule.
- Maintains a 64 KiB circular window and supports overlapping copies.
- Stops strictly after producing the externally supplied uncompressed size.

