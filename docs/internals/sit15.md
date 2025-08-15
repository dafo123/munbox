# SIT Method 15 (Arsenic) Decompression

Authoritative, implementation-level specification of the *.sit “method 15” (internal name: Arsenic) decompression format as implemented in `lib/layers/sit15.c`.

## References and Acknowledgements

Primary historical and technical sources:

- Matthew Russotto’s Arsenic notes (archived): http://www.russotto.net/arseniccomp.html
- Fenwick, Block Sorting Text Compression – Final Report, 1996
- Burrows & Wheeler (block sorting transform, 1994 report)
- Wikipedia & academic primers on Burrows–Wheeler, Move‑To‑Front, arithmetic coding
- Original randomization approach inspired by bzip2 design choices
- The Unarchiver project (original author: Dag Ågren)

## Introduction

Arsenic is a multi‑stage, block‑based compression pipeline whose decompression reverses these stages:
1. Arithmetic decoding (context‑adaptive, variable models)
2. Zero run‑length expansion (for clustered MTF zeros)
3. Move‑To‑Front (MTF) inversion
4. Inverse Burrows–Wheeler Transform (iBWT)
5. Optional deterministic “randomization” XOR
6. Final RLE expansion of prior file‑level byte stuffing

Design augmentations over the classic BWT→MTF→entropy trio are: a pre/post RLE (implemented as the final stage during decode), zero‑run suppression between MTF and arithmetic coding, and an optional randomization (scrambling) phase to avoid worst‑case BWT behavior on highly repetitive data.

The encoder split data into equal‑sized blocks (except possibly the last) after applying the initial RLE during compression; decompression therefore may output more total bytes than the configured block size because the RLE was applied before block partitioning. 

## Stream Format

### Overview

A stream consists of a stream header followed by zero or more compressed blocks. If the “initial end” flag in the header is set, no blocks follow.

```
| Stream Header | Block 0 | Block 1 | ... | Block N-1 |
```

### Stream Header (decoded with Primary model bits, LSB-first inside each multi-bit field)

| Field | Bits | Description |
| :---- | :--- | :---------- |
| Signature byte 0 | 8 | Must be 0x41 ('A') |
| Signature byte 1 | 8 | Must be 0x73 ('s') |
| Block Size Bits B | 4 | Integer 0–15; block_size = 1 << (B + 9) (512 .. 16,777,216 bytes) |
| Initial End Flag | 1 | If 1: no blocks; any attempt to read data is an error |

The Primary (binary) arithmetic model (symbols {0,1}, increment 1, limit 256) persists across all blocks and headers.

### Block Structure

Each block = Header + Data + Footer. Blocks are decoded sequentially until an End‑Of‑Stream (EOS) flag in a footer is set.

#### Block Header (Primary model)

| Field | Bits | Description |
| :---- | :--- | :---------- |
| Randomization Flag | 1 | 1 enables Randomization XOR for this block; 0 disables |
| BWT Primary Index | (B+9) | Unsigned integer. Must be < decoded_block_length unless the block is empty (length 0). On non‑empty block violation: fatal error. |

#### Block Data (Selector + MTF symbol models)

Sequence of selector symbols (0..10) producing byte output (after zero‑run & MTF). Selector 10 terminates the block. See “Probability Models and Selection”.

#### Block Footer (Primary model)

| Field | Bits | Description |
| :---- | :--- | :---------- |
| End Of Stream Flag | 1 | If 1: this block is the final one; proceed to CRC field |
| CRC | 32 (conditional) | Present only if EOS flag == 1. Value is decoded then ignored (discarded). Decoded LSB‑first within the 32‑bit assembly, matching `decode_arithmetic_bit_string`. |

Model reset timing: Selector + all 7 MTF symbol models are reset immediately after reading the end‑of‑block marker (selector 10) and before decoding the footer fields (EOS flag + CRC). This ordering differs from some public descriptions that place EOS inside the block header.

### Bit Ordering Summary

Two distinct bit order conventions exist and MUST NOT be conflated:
- Bitstream extraction from bytes: MSB‑first (big‑endian within each byte) into a 32‑bit shifting container.
- Multi‑bit fields decoded via arithmetic (`decode_arithmetic_bit_string`): logical bit positions are assembled LSB‑first (little‑endian) into the integer result (bit 0 first).

## Entropy Coding (Arithmetic Decoder)

### Constants

| Name | Value | Meaning |
| :--- | :---- | :------ |
| ARITHMETIC_BITS | 26 | Working precision |
| RANGE_ONE | 1 << 25 | Initial full range (1.0) |
| RANGE_HALF | 1 << 24 | Renormalization threshold |

### Initialization

1. range = RANGE_ONE
2. code = next 26 bits (MSB-first from bitstream container)
3. Primary model initialized (2 symbols, increment 1, limit 256). Other models initialized per block when needed.

### Decoding One Symbol

Given model total_frequency > 0:
1. renorm_factor = range / total_frequency; if 0 → error
2. freq_threshold = code / renorm_factor
3. Iterate symbols (except possibly last) accumulating frequencies until cumulative + freq > freq_threshold; selected symbol is current
4. low_increment = renorm_factor * cumulative
5. code -= low_increment
6. If symbol is last (cumulative + freq == total_frequency): range -= low_increment else range = freq * renorm_factor
7. While range <= RANGE_HALF: range <<= 1; code = (code << 1) | next_bit()
8. Update symbol frequency by model increment; if model total exceeds limit: halve ( (f+1)>>1 ) all frequencies and recompute total.

Failure to supply enough bits (bitstream exhaustion) during any required read is a fatal error.

## Probability Models and Selection

At block start (data phase) the following models are (re)initialized:

| Model | Symbols (inclusive) | Count | Increment | Limit | Initial per-symbol freq | Initial total |
| :---- | :------------------ | :---- | :-------- | :---- | :----------------------- | :------------ |
| Selector | 0..10 | 11 | 8 | 1024 | 8 | 88 |
| MTF Group 0 | 2..3 | 2 | 8 | 1024 | 8 | 16 |
| MTF Group 1 | 4..7 | 4 | 4 | 1024 | 4 | 16 |
| MTF Group 2 | 8..15 | 8 | 4 | 1024 | 4 | 32 |
| MTF Group 3 | 16..31 | 16 | 4 | 1024 | 4 | 64 |
| MTF Group 4 | 32..63 | 32 | 2 | 1024 | 2 | 64 |
| MTF Group 5 | 64..127 | 64 | 2 | 1024 | 2 | 128 |
| MTF Group 6 | 128..255 | 128 | 1 | 1024 | 1 | 128 |

Primary model (2 symbols) persists across blocks and is NOT reset per block.

### Selector Semantics

| Selector | Action |
| :------- | :----- |
| 0 or 1 | Start / continue zero run-length decoding sub-loop |
| 2 | Literal symbol 1 (MTF input = 1) |
| 3..9 | Decode next symbol from MTF Group (selector - 3) and pass to MTF |
| 10 | End-of-block marker |

After a zero run completes, the first non {0,1} selector (including 10) is processed normally (i.e. it is not discarded).

## Zero Run-Length Decoding

Triggered when a selector ∈ {0,1}. Two integers are used in the C implementation:
- zero_run_state (initial 1)
- zero_run_count (initial 0)

Loop (already have first selector s):
1. If s == 0: zero_run_count += zero_run_state
2. Else (s == 1): zero_run_count += 2 * zero_run_state
3. zero_run_state *= 2
4. Decode next selector s
5. Continue while s < 2

Interpretation: if you denote bit_i = (original selector_i) where selector 0 maps to 0, selector 1 maps to 1, then the accumulated count after n bits is:

  zero_run_count = Σ_{i=0..n-1} ( (1 + bit_i) * 2^i )

Output production:
1. Decode MTF symbol 0 ONCE to obtain a single byte value V (this updates MTF exactly once)
2. Append V repeated zero_run_count times to the MTF output buffer (a `memset` in the reference). The MTF table is NOT further modified by these repeats.

Boundary: If bytes_decoded_in_block + zero_run_count > block_size → error.

If the next selector (that terminated the loop) is 10, the block ends immediately after the run output.

## Move-to-Front Decoding

State: 256-entry table T where T[i] = i at block start. For an input index k:
1. v = T[k]
2. If k > 0: shift T[0..k-1] right by one (memmove)
3. T[0] = v
4. Emit v

Used for every non run-generated symbol (and the single symbol at start of a zero run). Bounds check: k < 256 else error.

## Inverse Burrows-Wheeler Transform (iBWT)

Preparation (once per block after data decode completes):
1. Count frequency freq[c] of each byte c
2. Build cumulative starting positions cumul[c] (standard LF-mapping construction)
3. For each position i (0..len-1): let b = block[i]; write i into transform[ cumul[b] + rank_b ] then rank_b++

Reconstruction:
Initialize current_index = bwt_primary_index. For each output byte:
1. current_index = transform[current_index]
2. byte = block[current_index]
3. Pass byte forward (Randomization → Final RLE)

Constraint: For non-empty block: 0 ≤ primary_index < block_length. For empty block: primary_index is ignored (may be any value) but no bytes are produced.

## Randomization Stage

Optional per block. State initialized per new block:
| Variable | Initial |
| :------- | :------ |
| randomization_table_index | 0 |
| randomization_next_pos | RANDOMIZATION_TABLE[0] |

For each byte position pos (0-based) produced by iBWT:
1. If is_randomized && pos == randomization_next_pos: byte ^= 1; advance:
   - randomization_table_index = (randomization_table_index + 1) & 255
   - randomization_next_pos += RANDOMIZATION_TABLE[randomization_table_index]
2. Emit (possibly XORed) byte

Scheduling therefore uses a cumulative sum of the 16‑bit table entries, wrapping the table index modulo 256.

## Final Run-Length Decoding (Byte Stuffing Expansion)

Purpose: Expand runs encoded during compression as: four literal repeats of a byte followed by a length byte K (0..255) giving total run length = 4 + K. (K == 0 means exactly four.)

Decoder state per block (all zeroed): last_byte, consecutive_count, repeat_count.

Algorithm (processed after previous stages):
1. If repeat_count > 0: output last_byte; repeat_count--; continue
2. Obtain next upstream byte b
3. If b == last_byte: consecutive_count++ else: last_byte = b; consecutive_count = 1
4. If consecutive_count == 4:
   a. consecutive_count = 0
   b. Read extension byte ext
   c. If ext == 0: (run = 4 exactly) continue (the four already emitted earlier)
   d. Output last_byte once (this call’s return) and set repeat_count = ext - 1 (so total additional outputs = ext)
   e. Net run length = 4 + ext (allowing 5..259 when ext ∈ [1,255])
5. If consecutive_count != 4 (normal case) output the current byte

Note: Because repeat_count = ext - 1 after emitting one extension byte immediately, the maximum realized run length is 259 (4 + 255). Runs longer than 259 must be encoded by the compressor as multiple sub-runs.

## Implementation Details and Compatibility

### Memory
Allocate two buffers sized to block_size:
- mtf_output_buffer: block_size bytes
- bwt_transform_array: block_size * sizeof(uint32_t)

### Lifecycle per Block
1. If needed, decode new block (header) when previous block fully consumed
2. Initialize selector + MTF symbol models, MTF table, per-block randomization & final-RLE state
3. Decode selectors & symbols into mtf_output_buffer
4. After selector 10: reset selector & MTF models, decode footer (EOS flag, possibly CRC)
5. Prepare inverse BWT transform
6. Stream out bytes via BWT reconstruction → optional randomization → final RLE expansion

### Error Conditions (fatal)
- Signature mismatch
- Insufficient bits for any required read
- Block size bits outside 0..15
- Memory allocation failure
- Model total_frequency == 0 or renorm_factor == 0 (corrupt input)
- BWT primary index out of range for non-empty block
- Buffer overflow risk (attempting to write beyond block_size during decoding)
- Requesting bytes after EOS fully consumed

### Model Reset Timing
Reset of selector + MTF symbol models occurs immediately after reading selector 10 (before footer). Primary model is never reset after initial creation.

### Randomization Table

Exactly the following 256-entry 16‑bit unsigned table (values identical to reference implementation):
```c
static const uint16_t RANDOMIZATION_TABLE[256] = {
 0x00ee,0x0056,0x00f8,0x00c3,0x009d,0x009f,0x00ae,0x002c,
 0x00ad,0x00cd,0x0024,0x009d,0x00a6,0x0101,0x0018,0x00b9,
 0x00a1,0x0082,0x0075,0x00e9,0x009f,0x0055,0x0066,0x006a,
 0x0086,0x0071,0x00dc,0x0084,0x0056,0x0096,0x0056,0x00a1,
 0x0084,0x0078,0x00b7,0x0032,0x006a,0x0003,0x00e3,0x0002,
 0x0011,0x0101,0x0008,0x0044,0x0083,0x0100,0x0043,0x00e3,
 0x001c,0x00f0,0x0086,0x006a,0x006b,0x000f,0x0003,0x002d,
 0x0086,0x0017,0x007b,0x0010,0x00f6,0x0080,0x0078,0x007a,
 0x00a1,0x00e1,0x00ef,0x008c,0x00f6,0x0087,0x004b,0x00a7,
 0x00e2,0x0077,0x00fa,0x00b8,0x0081,0x00ee,0x0077,0x00c0,
 0x009d,0x0029,0x0020,0x0027,0x0071,0x0012,0x00e0,0x006b,
 0x00d1,0x007c,0x000a,0x0089,0x007d,0x0087,0x00c4,0x0101,
 0x00c1,0x0031,0x00af,0x0038,0x0003,0x0068,0x001b,0x0076,
 0x0079,0x003f,0x00db,0x00c7,0x001b,0x0036,0x007b,0x00e2,
 0x0063,0x0081,0x00ee,0x000c,0x0063,0x008b,0x0078,0x0038,
 0x0097,0x009b,0x00d7,0x008f,0x00dd,0x00f2,0x00a3,0x0077,
 0x008c,0x00c3,0x0039,0x0020,0x00b3,0x0012,0x0011,0x000e,
 0x0017,0x0042,0x0080,0x002c,0x00c4,0x0092,0x0059,0x00c8,
 0x00db,0x0040,0x0076,0x0064,0x00b4,0x0055,0x001a,0x009e,
 0x00fe,0x005f,0x0006,0x003c,0x0041,0x00ef,0x00d4,0x00aa,
 0x0098,0x0029,0x00cd,0x001f,0x0002,0x00a8,0x0087,0x00d2,
 0x00a0,0x0093,0x0098,0x00ef,0x000c,0x0043,0x00ed,0x009d,
 0x00c2,0x00eb,0x0081,0x00e9,0x0064,0x0023,0x0068,0x001e,
 0x0025,0x0057,0x00de,0x009a,0x00cf,0x007f,0x00e5,0x00ba,
 0x0041,0x00ea,0x00ea,0x0036,0x001a,0x0028,0x0079,0x0020,
 0x005e,0x0018,0x004e,0x007c,0x008e,0x0058,0x007a,0x00ef,
 0x0091,0x0002,0x0093,0x00bb,0x0056,0x00a1,0x0049,0x001b,
 0x0079,0x0092,0x00f3,0x0058,0x004f,0x0052,0x009c,0x0002,
 0x0077,0x00af,0x002a,0x008f,0x0049,0x00d0,0x0099,0x004d,
 0x0098,0x0101,0x0060,0x0093,0x0100,0x0075,0x0031,0x00ce,
 0x0049,0x0020,0x0056,0x0057,0x00e2,0x00f5,0x0026,0x002b,
 0x008a,0x00bf,0x00de,0x00d0,0x0083,0x0034,0x00f4,0x0017
};
```

### Decoder Interface (Reference One‑Shot API)

```c
size_t sit15_decompress(uint8_t *dst, size_t dst_len, const uint8_t *src, size_t src_len);
```
Returns number of bytes written on success (expected to equal dst_len requested by caller) or 0 on error.
