# Compact Pro (.cpt) File Format

The Compact Pro format originates from classic Mac OS and was designed to preserve Macintosh files (with their dual-fork structure) while achieving good compression. Two lossless algorithms are used, arranged per-fork based on flags: either RLE alone (RLE), or LZSS+Huffman followed by RLE (referred to here as “LZH then RLE”).

## References and Acknowledgements

Primary historical and technical sources:

- https://en.wikipedia.org/wiki/Compact_Pro
- https://github.com/mietek/theunarchiver/wiki/CompactProSpecs
- https://preservation.tylerthorsted.com/2024/02/16/compact-pro/
- The Unarchiver project (original author: Dag Ågren)

## Introduction

The Compact Pro archive format, identified by the .cpt extension, is a file compression and archiving utility developed by Bill Goodman for the Apple Macintosh platform in the early 1990s. Originally released as "Compactor," it emerged as a significant competitor to the then-dominant StuffIt archiver, often delivering superior compression speeds and, in many cases, smaller archive sizes. Its popularity was bolstered by its efficient design, clean user interface, and distribution as shareware, which made it a common sight on Bulletin Board Systems (BBS) of the era.

- Compact Pro (.cpt) is a classic Mac archive format that stores a directory tree and Macintosh files (with both data and resource forks) plus Finder metadata.
- The archive begins with a fixed 8‑byte header that points to a directory section (a second header plus a depth‑first list of file and directory entries).
- File entries include absolute offsets to their fork data. Each fork is compressed independently using RLE, or LZSS+Huffman followed by RLE.
- All multi‑byte integers are big‑endian.

A concise decoding overview (fork order and compression layering) is given in Section 4; full details follow in Sections 5–6.

At a glance:
- Endianness is big-endian throughout.
- The archive starts with a tiny fixed header that points to a “directory” section.
- The directory contains a second header and a list of entries (files and directories); file entries include absolute offsets to their data.
- Each file’s data is stored as two separately-compressed forks in a fixed order: resource fork first, then data fork.
- Two CRC-32 checksums are used: one for the directory and one per file over the uncompressed data (resource then data).

## Concepts and rationale

Classic Mac files have two forks:
- Data fork: raw file data.
- Resource fork: structured resources (icons, menus, code, etc.).

Compact Pro preserves both, plus Mac metadata (type, creator, Finder flags, timestamps). Each fork is compressed independently, chosen by per-fork flags:
- RLE only; or
- LZSS+Huffman (LZH) followed by RLE.

Why two layers? RLE is very fast and effective on runs. LZSS+Huffman captures wider redundancy. In Compact Pro, when a fork is marked “LZH,” you first decode LZSS+Huffman to produce an RLE-encoded stream, then apply RLE to recover the original fork bytes.

All multi-byte numbers in the format are big-endian (most significant byte first), reflecting the Mac 68k heritage.

## Archive structure

### Identification and byte order
- Magic byte at offset 0: 0x01.
- Volume number at offset 1: 0x01 for single-volume archives (meaning not entirely clear).
- All multi-byte numbers are big-endian.

### Initial archive header (8 bytes)

| Offset | Size | Type       | Meaning |
|:------:|:----:|:----------:|:--------|
| 0      | 1    | uint8      | File identifier, always 0x01 |
| 1      | 1    | uint8      | Volume number (0x01 for single-volume archives; exact semantics unclear) |
| 2      | 2    | uint16_be  | Cross-volume magic number (meaning unclear) |
| 4      | 4    | uint32_be  | Offset to the directory (second header + entry records) from file start |

The 4-byte offset points to the “directory” area, which begins with the second archive header.

### Second archive header (at the directory offset)

| Offset | Size | Type       | Meaning |
|:------:|:----:|:----------:|:--------|
| 0      | 4    | uint32_be  | CRC-32 of the directory metadata (see Section 3.1 for algorithm and coverage) |
| 4      | 2    | uint16_be  | Total number of files and directories |
| 6      | 1    | uint8      | Archive comment length (N) |
| 7      | N    | bytes      | Archive comment |

Immediately following this header are N entries (files and/or directories) as indicated by the total count. Directory subtrees are serialized inline; see 2.4 for recursion rules.

### Entry records

There are two record types: directory and file.

Directory entry (recursive)

| Offset | Size | Type       | Meaning |
|:------:|:----:|:----------:|:--------|
| 0      | 1    | uint8      | Name length with type flag: bit 7 set indicates directory; lower 7 bits are name length (N) |
| 1      | N    | bytes      | Directory name |
| N+1    | 2    | uint16_be  | Total number of entries in this directory’s subtree (including nested files and directories) |

The directory hierarchy is reconstructed by walking entries recursively using the subtree count:
- Read a directory entry and its 2-byte subtree count C.
- Immediately following are exactly C entries belonging to this directory’s subtree, serialized depth-first. Recurse into child directories as they are encountered.
- After processing that subtree, consider this directory consumed and continue with subsequent siblings/parents until the top-level total has been consumed.

File entry

| Offset   | Size | Type       | Meaning |
|:--------:|:----:|:----------:|:--------|
| 0        | 1    | uint8      | Name length with type flag: bit 7 clear indicates file; lower 7 bits are name length (N) |
| 1        | N    | bytes      | File name |
| N+1      | 1    | uint8      | Volume number (meaning unclear) |
| N+2      | 4    | uint32_be  | Offset to file data (absolute, from file start) |
| N+6      | 4    | uint32_be  | Mac OS file type (four-char OSType) |
| N+10     | 4    | uint32_be  | Mac OS file creator (four-char OSType) |
| N+14     | 4    | uint32_be  | Creation date (seconds since 1904-01-01) |
| N+18     | 4    | uint32_be  | Modification date (seconds since 1904-01-01) |
| N+22     | 2    | uint16_be  | Finder flags |
| N+24     | 4    | uint32_be  | Uncompressed file data CRC-32 (see Section 3.2) |
| N+28     | 2    | uint16_be  | File flags (encryption and per-fork compression encoding indicators; see below) |
| N+30     | 4    | uint32_be  | Resource fork uncompressed length |
| N+34     | 4    | uint32_be  | Data fork uncompressed length |
| N+38     | 4    | uint32_be  | Resource fork compressed length |
| N+42     | 4    | uint32_be  | Data fork compressed length |

File flags (16 bits)
- Bit 0: file is encrypted (algorithm unknown)
- Bit 1: resource fork uses LZH (if not set, uses RLE)
- Bit 2: data fork uses LZH (if not set, uses RLE)

Note: The flags describe how each fork is encoded and should be followed by a conforming decoder.

Fork layout (critical): For each file, the resource fork’s compressed data comes first, followed by the data fork’s compressed data. The “offset to file data” points to the start of the resource fork’s compressed data. The offset to the data fork is: file data offset + resource fork compressed length.


## Checksums (CRC-32)

Compact Pro uses IEEE CRC-32 (reflected) in two places, with different finalization:

Common parameters
- Polynomial: 0xEDB88320 (reflected form of 0x04C11DB7)
- Initial value: 0xFFFFFFFF
- Input reflected: Yes (table-driven reflected arithmetic)
- Stored values are big-endian 32-bit

### Directory CRC-32 (second header)

Finalization: no final XOR (compare accumulator directly to stored value).

Coverage (in this exact order):
1) 2 bytes: total number of entries (uint16_be)
2) 1 byte: comment length
3) comment bytes (if any)
4) For each entry (repeat for the total count):
   - 1 byte: name length/type flag (bit 7 set = directory)
   - N bytes: name, N = (name length & 0x7F)
   - Metadata bytes:
    - Directory: 2 bytes (subtree entry count for that directory)
     - File: 45 bytes (volume, file data offset, type/creator, dates, Finder flags, uncompressed CRC, flags, fork sizes and compressed sizes)

The computed accumulator (starting at 0xFFFFFFFF) must equal the stored big-endian CRC-32 at the start of the second header.

### Per-file data CRC-32

Finalization: XOR with 0xFFFFFFFF before storing. Equivalently, if you maintain the accumulator and do not apply a final XOR, the stored value equals bitwise NOT of the accumulator.

Coverage: the uncompressed resource fork bytes followed immediately by the uncompressed data fork bytes (omit any empty fork).


## Compression pipelines (how to decide what to run)

Per file, decide per fork using the flags:
- If the fork’s LZH bit is set: decode LZSS+Huffman first (producing an RLE stream), then decode with RLE.
- If the fork’s LZH bit is clear: decode with RLE only.

Resource fork is decoded first (it appears first in the file’s compressed data), then data fork.

## Integration in munbox: fork-aware detection via open()

When CPT appears inside a container that preserves classic Mac forks (for example, MacBinary), the munbox pipeline exposes a non-destructive fork iterator through the layer API:

- open(MUNBOX_OPEN_FIRST, info) positions the stream at the first fork and returns its metadata (info.fork_type is DATA or RESOURCE).
- open(MUNBOX_OPEN_NEXT, info) advances to the next fork, if any.

For CPT identification, the recommended strategy is:

- If input->open is available, iterate forks with open(FIRST/NEXT) and perform a small non-destructive peek at the start of each fork to check the 8-byte CPT header (0x01 0x01 … dirOffset). This avoids consuming the stream and allows retrying the other fork on mismatch.
- Once identified, read the entire fork into memory (CPT needs random access to parse the directory and absolute offsets) and iterate files/forks via `open(FIRST/NEXT)` + `read()`.
- If input->open is not available, fall back to a single peek at the current stream position; on match, read the entire stream into memory and proceed.

Note
- CPT archives are expected to start at offset 0 of the carrying fork (unlike some self-extracting formats where content may be embedded at a non-zero offset). So a header check at the fork start is sufficient; no in-fork scanning is required.
- In the munbox factory order, CPT runs after fork-preserving transformers (e.g., BIN), so detection sees the current fork’s content directly via the peek wrapper.

# RLE — Run-Length Encoding used by Compact Pro

## Overview

Compact Pro uses a stateful RLE with a single escape byte (0x81). It supports literal escapes, double escapes, and run-length sequences (0x81 0x82 N). The decoder keeps only minimal state and is suitable for streaming.

## Fundamental Algorithm Principles

### State-Based Processing

The RLE decoder maintains persistent state across multiple input bytes:

- **Current Processing Mode**: The decoder can be in one of several states:
  - Reading normal input
  - Processing pending repeated bytes
  - Handling escaped sequences
  
- **Memory of Previous Context**: The decoder remembers the last processed byte for potential repetition

- **Deferred Output**: Some operations queue bytes for future output rather than immediate emission

### Single Escape Marker Philosophy

The algorithm uses 0x81 as the sole escape marker, followed by different bytes to indicate various operations:

- **0x81 followed by 0x82**: Run-length encoding sequence
- **0x81 followed by 0x81**: Double escape for literal 0x81
- **0x81 followed by any other byte**: Simple escape sequence

## Decoder State Variables

The decoder maintains exactly three state variables:

### saved_byte
- **Purpose**: Stores the last byte that was processed and output
- **Usage**: Available for repetition in RLE sequences
- **Initialization**: Set to 0 at start
- **Updates**: Updated whenever a new byte is output to the stream

### repeat_count
- **Purpose**: Number of additional copies of saved_byte to output
- **Usage**: Decremented each time a repeated byte is output
- **Initialization**: Set to 0 at start
- **Range**: Non-negative integer

### half_escaped
- **Purpose**: Boolean flag indicating that 0x81 should be output on the next processing cycle
- **Usage**: Set by double escape sequences (0x81 0x81)
- **Initialization**: Set to false at start
- **Behavior**: Cleared after outputting the pending 0x81

## Processing Algorithm

### Main Processing Loop

The decoder follows this exact sequence for each output position:

```
while output_position < desired_output_length:
    1. Check for pending repeated bytes
    2. Check for half-escaped output
    3. Read and process new input byte
    4. Handle escape sequences if applicable
    5. Output appropriate byte and update state
```

### Step-by-Step Processing Rules

**Step 1: Handle Pending Repeats**
```
if repeat_count > 0:
    output(saved_byte)
    repeat_count = repeat_count - 1
    continue to next output position
```

**Step 2: Handle Half-Escaped State**
```
if half_escaped flag is set:
    output(0x81)
    half_escaped = false
    continue to next output position
```

**Step 3: Read New Input**
```
if input_position >= input_length:
    break (end of input)
byte = input[input_position]
input_position = input_position + 1
```

**Step 4: Process Input Byte**
```
if byte != 0x81:
    // Normal literal byte
    output(byte)
    saved_byte = byte
else:
    // Escape sequence - read next byte
    process_escape_sequence()
```

### Escape Sequence Processing

When 0x81 is encountered, read the next byte and process according to these rules:

**Case 1: 0x81 0x82 (RLE Sequence)**
```
read next_byte
if next_byte == 0x82:
    read count_byte
    if count_byte == 0:
        // Special case: literal 0x81 0x82
        output(0x81)
        saved_byte = 0x82
        repeat_count = 1
    else:
        // Standard RLE: output saved_byte now, then (count-2) more
        output(saved_byte)
        repeat_count = max(0, count_byte - 2)
```

**Case 2: 0x81 0x81 (Double Escape)**
```
if next_byte == 0x81:
    output(0x81)
    saved_byte = 0x81
    half_escaped = true
```

**Case 3: 0x81 X (Simple Escape)**
```
else:
    // Any other byte after 0x81
    output(0x81)
    saved_byte = next_byte
    repeat_count = 1
```

## Critical Count Interpretation

### The N-2 Rule

For RLE sequences (0x81 0x82 N where N > 0), the count interpretation is:

**CORRECT**: Output saved_byte immediately, then output (N-2) additional copies
**WRONG**: Output saved_byte N times total
**WRONG**: Output N additional copies

### Mathematical Explanation

If the RLE sequence encodes K repetitions of a byte:
- Total output bytes: K
- Immediate output: 1 (the saved_byte)
- Additional repeats: K-1
- Count byte value: K+1
- repeat_count setting: (K+1)-2 = K-1

This means: `repeat_count = count_byte - 2`

### Concrete Examples

**Example 1**: Encode 2 copies of byte A
- Sequence: A, 0x81, 0x82, 0x03
- Processing: Output A (immediate), set repeat_count = 3-2 = 1
- Result: A, A (2 total)

**Example 2**: Encode 4 copies of byte A  
- Sequence: A, 0x81, 0x82, 0x05
- Processing: Output A (immediate), set repeat_count = 5-2 = 3
- Result: A, A, A, A (4 total)

## Special Cases and Edge Conditions

### Zero Count RLE
When encountering 0x81 0x82 0x00:
- This represents literal bytes 0x81 0x82, not a run
- Output 0x81 immediately
- Set saved_byte = 0x82 and repeat_count = 1
- Next cycle will output 0x82

### Invalid Count Values
The sequence 0x81 0x82 0x01 should never appear in valid data:
- It would imply -1 additional repeats (1-2 = -1)
- Robust decoders may treat this as a no-op
- Encoders must never generate this sequence

### End of Input During Processing
If input ends during an escape sequence:
- After 0x81 but before the next byte: error condition
- After 0x81 0x82 but before count: error condition
- While repeat_count > 0: continue outputting saved_byte until done

## Complete Algorithm in Structured Pseudocode

```
function rle_decompress(input_bytes, expected_output_length):
    // State variables
    input_position = 0
    output_position = 0
    saved_byte = 0
    repeat_count = 0
    half_escaped = false
    
    output_buffer = allocate(expected_output_length)
    
    while output_position < expected_output_length:
        // Step 1: Handle pending repeats
        if repeat_count > 0:
            output_buffer[output_position] = saved_byte
            output_position = output_position + 1
            repeat_count = repeat_count - 1
            continue
        
        // Step 2: Handle half-escaped state
        if half_escaped:
            output_buffer[output_position] = 0x81
            output_position = output_position + 1
            half_escaped = false
            continue
        
        // Step 3: Check for end of input
        if input_position >= length(input_bytes):
            break
        
        // Step 4: Read next input byte
        current_byte = input_bytes[input_position]
        input_position = input_position + 1
        
        // Step 5: Process the byte
        if current_byte != 0x81:
            // Normal literal byte
            output_buffer[output_position] = current_byte
            output_position = output_position + 1
            saved_byte = current_byte
        else:
            // Escape sequence
            if input_position >= length(input_bytes):
                return error("Incomplete escape sequence")
            
            next_byte = input_bytes[input_position]
            input_position = input_position + 1
            
            if next_byte == 0x82:
                // RLE sequence
                if input_position >= length(input_bytes):
                    return error("Incomplete RLE count")
                
                count = input_bytes[input_position]
                input_position = input_position + 1
                
                if count == 0:
                    // Literal 0x81 0x82
                    output_buffer[output_position] = 0x81
                    output_position = output_position + 1
                    saved_byte = 0x82
                    repeat_count = 1
                else:
                    // Standard RLE
                    output_buffer[output_position] = saved_byte
                    output_position = output_position + 1
                    repeat_count = max(0, count - 2)
            
            else if next_byte == 0x81:
                // Double escape
                output_buffer[output_position] = 0x81
                output_position = output_position + 1
                saved_byte = 0x81
                half_escaped = true
            
            else:
                // Simple escape
                output_buffer[output_position] = 0x81
                output_position = output_position + 1
                saved_byte = next_byte
                repeat_count = 1
    
    return output_buffer[0:output_position]
```

## Validation Test Cases

Any implementation must pass these exact test cases:

### Basic RLE Tests
```
Test 1: Simple 2-byte run
Input:  [0x41, 0x81, 0x82, 0x03]
Output: [0x41, 0x41]
Length: 2 bytes

Test 2: Longer run
Input:  [0x41, 0x81, 0x82, 0x05] 
Output: [0x41, 0x41, 0x41, 0x41]
Length: 4 bytes

Test 3: Run of different byte
Input:  [0x41, 0x42, 0x81, 0x82, 0x03]
Output: [0x41, 0x42, 0x42]
Length: 3 bytes
```

### Escape Sequence Tests
```
Test 4: Simple escape
Input:  [0x81, 0x41]
Output: [0x81, 0x41]
Length: 2 bytes

Test 5: Zero count (literal)
Input:  [0x81, 0x82, 0x00]
Output: [0x81, 0x82]
Length: 2 bytes

Test 6: Double escape
Input:  [0x81, 0x81, 0x42]
Output: [0x81, 0x81, 0x42]
Length: 3 bytes
```

### Complex Sequence Tests
```
Test 7: Mixed operations
Input:  [0x41, 0x81, 0x82, 0x03, 0x43, 0x81, 0x44]
Output: [0x41, 0x41, 0x43, 0x81, 0x44]
Length: 5 bytes

Test 8: Run after escape
Input:  [0x81, 0x42, 0x81, 0x82, 0x04]
Output: [0x81, 0x42, 0x42, 0x42]
Length: 4 bytes
```

## Implementation Guidelines

### State Management
- Initialize all state variables explicitly
- Never assume default values for uninitialized variables
- Maintain state consistency across all processing paths

### Error Handling
- Check for incomplete escape sequences
- Validate input bounds before reading
- Handle unexpected end-of-input gracefully

### Performance Considerations
- Process pending outputs before reading new input
- Minimize state variable updates
- Use efficient buffer management for output

### Debugging Techniques
- Log all state variable changes
- Trace input position and escape sequence processing
- Compare output byte-by-byte with reference implementation

## Common Implementation Errors

### Count Interpretation Mistakes
**Error**: Using N or N-1 instead of N-2 for repeat count
**Symptom**: Wrong output length, assertion failures
**Fix**: Always use `repeat_count = count_byte - 2`

### State Variable Confusion
**Error**: Updating saved_byte at wrong times
**Symptom**: Wrong bytes in output stream
**Fix**: Update saved_byte only when outputting new literal bytes

### Processing Order Errors
**Error**: Reading new input before handling pending output
**Symptom**: Skipped or duplicated bytes
**Fix**: Always check repeat_count and half_escaped first

### Escape Sequence Mishandling
**Error**: Incorrect logic for 0x81 0x81 sequences
**Symptom**: Missing or extra 0x81 bytes in output
**Fix**: Implement half_escaped flag correctly

## LZH (CompactProLzhAlgorithm) — LZSS + Huffman, block-based

When bit 1 and/or bit 2 of the file flags are set, the corresponding fork is compressed with a combination of LZSS and Huffman coding. The decompression pipeline is: first apply LZSS+Huffman decoding to obtain an RLE-encoded stream, then apply RLE decoding to recover the original fork.

The LZH stage emits an RLE-encoded byte stream. Decoding is MSB-first at the bit level, with block-local canonical Huffman codes for literals (256 symbols), match lengths (64), and the upper 7 bits of match offsets (128). Huffman tables are rebuilt at the start of each block and persist until the next block.


### Rationale and intuition
LZSS replaces repeated substrings with back-references; Huffman coding compresses the resulting symbol stream. Compact Pro uses block-local code tables so that symbol probabilities adapt to local data, improving compression. Decoding operates on a bitstream read most-significant-bit first.

### Bitstream and window
- Bit order: MSB-first within each byte.
- LZSS window size: 8192 bytes (8 KiB), initialized to all zeros at the start of the stream.
- Match offset range: 13 bits combined from table+raw bits, interpreted as 1-based in the range [1, 8191].

### Block header — three code tables
For each block, three prefix-code tables are serialized in this order: literal (256 entries), length (64), offset (128).

For each table:
1) Read a 1-byte count numbytes.
2) Read numbytes bytes. Each byte contains two 4-bit code lengths: high nibble for symbol i, low nibble for symbol i+1, in ascending symbol order.
3) If numbytes*2 < table size, the remaining code lengths are implicitly 0.
4) If numbytes*2 > table size, the stream is invalid.

Construct canonical prefix codes from the code-length arrays (no bit reversal when decoding MSB-first):
- Process symbols in ascending code length, then ascending symbol value within that length.
- Maintain a running integer code for each length; when moving from length L to L+1, left-shift the running code by 1.
- For each symbol of length L, insert the current code path of L bits into the decode tree (MSB to LSB), then increment the running code for length L.
The maximum code length is 15 bits.

### Block data — decoding literals and matches
After building the three codes:
1) Read one bit:
   - If 1: decode a literal byte using the literal code; output and append to the LZSS window.
   - If 0: decode a match:
    * Decode match length using the length code; the decoded symbol value is the exact match length (no extra bits). Length must be at least 1.
     * Decode the upper 7 bits of the match offset using the offset code.
     * Read the lower 6 bits of the match offset as raw bits.
    * Combine to a 13-bit offset: offset = (upper7 << 6) | lower6. This offset is 1-based.
    * Copy starting at output_position - offset, for length bytes, updating the sliding window as you emit.

### Block size and termination
Blocks are measured in “symbol cost,” not output bytes:
- Each decoded literal increases the block counter by 2.
- Each decoded match increases it by 3.
Stop when the counter reaches or exceeds 0x1FFF0 (the Compact Pro block size). The final symbol may overflow the counter.

### End-of-block input flush
At the end of a block:
1) Align to the next byte boundary.
2) Let B be the number of bytes consumed by the block’s data portion (from immediately after the three tables to the current position). If B is odd, skip 3 bytes; if B is even, skip 2 bytes. Then begin the next block by reading its three tables.
This flush is required to re-synchronize with the next block header.

### Minimal LZH block decoder pseudocode

while not end_of_lzh_stream:
  // Parse code tables (literal, length, offset)
  lit_len = read_code_lengths(count_byte(), 256)
  len_len = read_code_lengths(count_byte(),  64)
  off_len = read_code_lengths(count_byte(), 128)
  literal = build_canonical(lit_len, 15)
  length  = build_canonical(len_len, 15)
  offset  = build_canonical(off_len, 15)

  block_counter = 0
  while block_counter < 0x1FFF0:
    if read_bit() == 1:
      sym = read_symbol(literal)
      output_byte(sym); lz_put(sym)
      block_counter += 2
    else:
      mlen = read_symbol(length)
      upper7 = read_symbol(offset)
      lower6 = read_bits(6)
    off = (upper7 << 6) | lower6  // 1-based
    lz_copy_from(pos - off, mlen)
      block_counter += 3

  align_to_byte()
  skipped = (bytes_consumed_in_block_data() % 2 == 1) ? 3 : 2
  skip_bytes(skipped)

### Worked example — block header nibble encoding

At the start of a block, the three code tables appear in order: literal, length, offset. For each table, read numbytes, then numbytes bytes of nibbles. If numbytes = 0x03 and the three bytes are [0x21, 0x30, 0x04], then for that table symbols 0..5 get lengths [2,1,3,0,0,4]; symbols 6..end get 0. Build the canonical code from these lengths (ascending by length, then symbol); max length is 15.


## Reconstructing Macintosh files

Fork storage and ordering
- The resource fork’s compressed data comes first at the file’s “offset to file data”.
- The data fork’s compressed data starts at: file data offset + resource fork compressed length.
- Decode each fork as per the flags (RLE only, or LZH then RLE), and verify the per-file CRC-32 over (resource || data) uncompressed bytes.

Path reconstruction: Entry names are per-path-segment. Reconstruct relative paths by concatenating parent directory names and file names with a '/' separator (or the host-appropriate separator).


## Implementation checklist and pitfalls

Checklist
- Big-endian for all multi-byte numbers.
- Initial 8-byte header; 4-byte offset points to the directory / second header.
- Second header: CRC-32 (dir), total entries, comment length, comment.
- Entry parsing: bit 7 of name-length distinguishes directory (1) vs file (0).
- Directory record: name and a 2-byte total entries count.
- File record: volume, absolute file data offset, type/creator, dates, Finder flags, uncompressed CRC-32, flags, fork lengths, compressed lengths.
- Flags: bit 0 encryption (unknown algorithm); bit 1 resource LZH; bit 2 data LZH; otherwise RLE per fork.
- Fork layout: resource fork data precedes data fork data; compute the data fork offset using the resource fork compressed length.
- Directory CRC-32: IEEE reflected, init 0xFFFFFFFF, no final XOR, coverage exactly as in Section 3.1.
- File data CRC-32: IEEE reflected, init 0xFFFFFFFF, final XOR 0xFFFFFFFF, over uncompressed resource||data; stored big-endian.
- Compression: for LZH forks, run LZH then RLE; otherwise RLE only.
- LZH decoding: MSB-first bits; per-block code tables (literal/length/offset); match length is the decoded symbol; offset is 13 bits from (upper7 via code, lower6 raw), interpreted as 1-based; copy from pos - offset; block counter (2 for literal, 3 for match) up to 0x1FFF0; end-of-block flush (byte-align + skip 2 or 3 bytes by even/odd rule of the data portion).
- RLE decoding: escape 0x81 0x82; N=0 literalizes 0x81 0x82; N>=2 outputs the last byte once immediately and schedules (N-2) more; cannot form a run before any literal has been output; 0x81 0x81 encodes two literal 0x81 bytes (one now, one next via a half-escape); 0x81 X encodes a literal 0x81 now and X on the next step.

Common pitfalls
- Mixing CRC-32 finalization: directory CRC has no final XOR; per-file CRC does. Compare/stash values accordingly (both big-endian in the file).
- Forgetting that LZH’s output is not the final plaintext: you must RLE-decode after LZH when the fork is marked LZH.
- Miscounting LZH block termination: it’s by symbol cost (2 for literal, 3 for match), not output bytes.
- Skipping the end-of-block flush: you must byte-align and skip 2 or 3 bytes depending on the parity of data bytes consumed in the block.
- Using the wrong copy base for matches: the start is output_position - offset (offset is 1-based).
- Emitting or accepting 0x81 0x82 0x01 in RLE: it’s invalid; encoders must never write it.
