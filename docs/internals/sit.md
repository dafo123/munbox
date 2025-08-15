# StuffIt (.sit) Format Specification

This document is the authoritative specification for the classic (1.x–4.x) and 5.x ("SIT5") archive
formats as implemented by this project. All multi‑byte integers are big‑endian unless explicitly stated.
Resource and data forks are treated as independent compressed streams.

## References and Acknowledgements

Primary external sources consulted:
- Wikipedia: https://en.wikipedia.org/wiki/StuffIt
- Historical reverse‑engineering notes: https://github.com/mietek/theunarchiver/wiki/StuffItFormat
- StuffIt 5 format notes: https://github.com/mietek/theunarchiver/wiki/StuffIt5Format
- Algorithm listing: https://github.com/mietek/theunarchiver/wiki/StuffItSpecs
- LZW background: https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Welch
- Huffman coding primer: https://web.stanford.edu/class/archive/cs/cs106b/cs106b.1214/handouts/290%20Huffman%20Coding.pdf

## Introduction

Two incompatible archive layouts share the .sit suffix:
- Classic (1.x–4.x): Simple linear sequence of fixed‑size file headers followed by fork data. Signatures such as
  `SIT!`, `ST46`, `ST50`, `ST60`, `ST65`, `STin`, `STi2`, `STi3`, `STi4` at offset 0 identify the variant; bytes 10–13
  always contain `rLau`.
- SIT5 (5.x): Redesigned object model using per‑entry linked headers, beginning with a long textual magic string and
  supporting newer compression methods. Folder hierarchy is implicit via parent offsets.

The implementation auto‑detects both formats even when embedded (e.g. inside SEA self‑extractors) by scanning input
data for the respective magic sequences.

## Classic Archive Structure

### Identification

Offset 0–3: magic (one of the listed signatures). Offset 10–13: `rLau`. Offset 14: version byte (semantics unknown but
preserved). Bytes 15–21: seven unknown bytes (treated as opaque). Everything is big‑endian.

### Main Archive Header (22 bytes)

| Offset | Size | Field | Notes |
| ------ | ---- | ----- | ----- |
| 0 | 4 | magic1 | One of `SIT!` `ST46` `ST50` `ST60` `ST65` `STin` `STi2` `STi3` `STi4` |
| 4 | 2 | file_count | Observed usage unclear (may exclude nested folder contents). Not trusted for iteration. |
| 6 | 4 | total_size | Total archive size (may be used for validation). |
| 10 | 4 | magic2 | Always `rLau`. |
| 14 | 1 | version | Preserved verbatim. |
| 15 | 7 | unknown | Copied through; not interpreted. |

Immediately following is the first 112‑byte file/folder header.

### File / Folder Header (fixed 112 bytes)

| Off | Size | Field | Description |
| --- | ---- | ----- | ----------- |
| 0 | 1 | rsrc_method_raw | Low nibble = compression method (0–15). 0x20 = folder start marker (see below). 0x21 = folder end marker. Bit 4 (0x10) indicates encryption (unsupported). Other bits ignored. |
| 1 | 1 | data_method_raw | Same layout for data fork. Folder markers may appear in either byte. |
| 2 | 1 | name_len | Length of file/folder name. |
| 3 | 63 | name | MacRoman, not NUL‑terminated. Remaining space padded/unused. |
| 66 | 4 | type | Mac file type (4 ASCII). |
| 70 | 4 | creator | Mac creator (4 ASCII). |
| 74 | 2 | finder_flags | Classic Finder flags. |
| 76 | 4 | create_time | Seconds since 1904‑01‑01 00:00:00 (Mac epoch). |
| 80 | 4 | mod_time | Same epoch. |
| 84 | 4 | rsrc_uncomp_len | Uncompressed resource fork length. |
| 88 | 4 | data_uncomp_len | Uncompressed data fork length. |
| 92 | 4 | rsrc_comp_len | Compressed resource fork byte count. |
| 96 | 4 | data_comp_len | Compressed data fork byte count. |
| 100 | 2 | rsrc_crc | CRC of resource fork (algorithm: see CRC section). |
| 102 | 2 | data_crc | CRC of data fork. |
| 104 | 6 | unknown | Stored verbatim. |
| 110 | 2 | header_crc | CRC over first 110 bytes (same algorithm). |

Folder markers:
- Start: either method_raw nibble == 0x20 (decimal 32). Name gives folder name. No fork data follows (comp lengths may
  be zero). Maintained as a push onto a directory stack.
- End: method_raw nibble == 0x21 (decimal 33). Pops one directory level. No data.

Following each non‑folder file header the resource fork compressed bytes (length rsrc_comp_len) are stored, then the
data fork (length data_comp_len). Either fork may be omitted if its *uncompressed* length is zero (comp length should
then also be zero). The next header begins immediately after the last compressed fork byte.

### Classic Iteration Rules

An extractor must process sequentially, maintaining a stack of active folder path components for nested names. Folder
start/end entries do not contribute to file_count and do not produce extractable forks. Validation should enforce that
compressed fork spans lie within the archive bounds and that CRCs match (unless method‑specific rules skip them, e.g.
method 15 — see below).

## SIT5 Archive Structure

### Identification & Top Header

Offset 0: 80‑byte ASCII magic: "StuffIt (c)1997-???? Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/" then
CR LF (0x0D 0x0A). Characters marked ? vary (year digits). Offsets after that (implementation‑confirmed layout):

| Offset | Size | Field | Notes |
| ------ | ---- | ----- | ----- |
| 80 | 4 | unknown0 | Purpose not interpreted. |
| 84 | 4 | total_size | Total archive size. |
| 88 | 4 | first_entry_offset | Byte offset of first entry header. (Historical docs mis‑placed this.) |
| 92 | 2 | declared_entry_count | Nominal number of top‑level headers (folders expand this). |
| 94 | 4 | initial_cursor | Starting cursor (usually equals first_entry_offset). |
| 98 | ... | reserved/unknown | Not currently parsed (minimum archive size check >= 100 bytes). |

### Entry Header (Header 1)

All entries (file or folder) begin with a primary header located at the cursor. Structure:

| Off | Size | Field | Description |
| --- | ---- | ----- | ----------- |
| 0 | 4 | id | Must be 0xA5A5A5A5. |
| 4 | 1 | version | Only version 1 supported by implementation. |
| 5 | 1 | unknown1 | Unused. |
| 6 | 2 | header_size | Size in bytes of header 1 (from offset 0). Used to locate header 2. |
| 8 | 1 | unknown2 | Unused. |
| 9 | 1 | flags | Bit 5 (0x20) encrypted (unsupported). Bit 6 (0x40) folder. Others unknown. |
| 10 | 4 | create_time | Mac epoch seconds. |
| 14 | 4 | mod_time | Mac epoch seconds. |
| 18 | 4 | prev_offset | Offset of previous entry (for doubly linked traversal). |
| 22 | 4 | next_offset | Offset of next entry. Used to advance after finishing payload. |
| 26 | 4 | parent_offset | Offset of parent folder entry (0 if top level). |
| 30 | 2 | name_len | File/folder name length. |
| 32 | 2 | header_crc | CRC over header 1 with bytes 32–33 zeroed before calculation. |
| 34 | 4 | data_uncomp_len | Uncompressed data fork length (0xFFFFFFFF for special folder markers). |
| 38 | 4 | data_comp_len | Compressed data fork length. |
| 42 | 2 | data_crc | Data fork CRC (zero if method 15). |
| 44 | 2 | unknown3 | Unused. |
| 46 | 1 | data_method | Low nibble compression method (0,13,15 observed). High bits encryption flag (same layout as classic). |
| 47 | 1 | data_pass_len | Password blob length. Non‑zero with encryption flag is unsupported. |
| 48 | N | password_blob | Skip N bytes. Unsupported if encryption flag set and lengths non‑zero. |
| 48+N | M | name | UTF‑8 or MacRoman (historical ambiguity; treat as bytes). |
| 48+N+M | 2 | comment_size | If present (file or folder). |
| 50+N+M | 2 | unknown4 | Unused. |
| 52+N+M | K | comment | K bytes of comment text. |
| ... | ... | (header1 end) | header_size defines end. |

For folders (flags bit 6 set): bytes 46–47 instead store a 2‑byte number_of_contained_files. The implementation uses
this to extend the expected entry traversal count (declared_entry_count is augmented by each folder's child count).
Entries with data_uncomp_len = 0xFFFFFFFF are special markers and skipped.

### Secondary Header (Header 2)

Header 2 begins immediately at offset (entry_offset + header_size). Layout (version‑dependent spans):

| Off | Size | Field | Notes |
| --- | ---- | ----- | ----- |
| 0 | 2 | flags2 | Bit 0 indicates resource fork present. Other bits unknown. |
| 2 | 2 | unknown_a | Ignored. |
| 4 | 4 | file_type | Mac 4‑char type. |
| 8 | 4 | file_creator | Mac creator. |
| 12 | 2 | finder_flags | Finder flags. |
| 14 | 2 | unknown_b | Ignored. |
| 16 | 4 | maybe_date | Observed sometimes date in later versions. |
| 20 | 12 | unknown_c | Ignored. |
| 32 | 4 | unknown_d | Present in version 1 only (skipped via version logic). |
| 32/36 | 4 | rsrc_uncomp_len | Only if flags2 bit0 set. |
| 36/40 | 4 | rsrc_comp_len | Only if flags2 bit0 set. |
| 40/44 | 2 | rsrc_crc | Zero if method 15. (Only if resource fork present.) |
| 42/46 | 2 | unknown_e | Ignored. |
| 44/48 | 1 | rsrc_method | Low nibble compression method. |
| 45/49 | 1 | unknown_f | Ignored. |

If resource fork present the compressed resource fork bytes come first, immediately followed by compressed data fork
bytes. The next entry header is at next_offset from header 1 (not by scanning forward).

### SIT5 Iteration Rules

Traversal begins at initial_cursor. Each folder adds its declared child count to the running total of entries to be
visited. Encrypted entries (flag 0x20 with non‑zero password lengths) are rejected. Folder marker entries with
data_uncomp_len = 0xFFFFFFFF are ignored. Paths are constructed by recursively resolving parent_offset through a map
of directory entries already seen.

## Compression Methods

Method IDs (low nibble) shared by both formats:

| ID | Name | Implemented | Notes |
| -- | ---- | ----------- | ----- |
| 0 | None | Yes | Raw copy. |
| 1 | RLE90 | Yes | Escape‑based RLE (see below). |
| 2 | LZW | Yes | StuffIt variant (14‑bit max, block alignment). |
| 3 | Static Huffman | No (not currently implemented) | Historical classic method. |
| 5 | LZAH | No | Used by SIT5; unspecified here. |
| 8 | LZMW (Miller‑Wegman) | No | Used by SIT5. |
| 13 | SIT13 (LZSS+Huffman) | Yes | Streaming implementation provided. |
| 14 | Unknown | No | Reserved. |
| 15 | SIT15 (BWT+Arithmetic) | Yes | Streaming (details external / separate file). |

Unsupported methods must yield an error.

## Method 0: None

Copy exactly comp_len bytes to output. Validate total uncompressed bytes produced equals header's uncompressed length.
Apply CRC over the produced bytes (unless method 15 rules apply — see below).

## Method 1: RLE90 (Escape Run‑Length Encoding)

Stream is parsed byte by byte. State variable last_byte (initially 0) tracks the most recently output literal.

Algorithm:
1. Read next input byte b.
2. If b != 0x90: output b; set last_byte = b; continue.
3. If b == 0x90: read count byte n (must exist; else truncated error).
   - If n == 0: output a single literal 0x90 (last_byte unchanged).
   - If n == 1: output nothing (zero additional repeats of last_byte).
   - If n > 1: output last_byte repeated (n - 1) additional times (total of n copies counting the earlier literal).

Edge cases:
- A repeat marker appearing before any literal causes repetitions of initial last_byte value (0); such streams are
  tolerated (legacy encoders may avoid this).
- Count bytes never encode a repeat of 0x90 itself; literal 0x90 must use sequence 0x90 0x00.

Decode until the required uncompressed length has been produced; ignore any trailing input padding.

## Method 2: LZW (StuffIt Variant of UNIX compress)

Parameters:
- Initial dictionary codes 0–255 = single bytes; 256 = Clear; 257 = (not explicitly used as Stop by implementation; end
  of data is inferred by exhausting input or reaching uncompressed length).
- Initial code width = 9 bits; increases to 10,11,12,13,14 when dictionary size reaches 512,1024,2048,4096,8192 codes
  respectively (power‑of‑two trigger) up to a maximum of 14 bits (implementation allocates space for 16384 codes).
- Bit packing: Little‑endian within a byte stream position (implementation reads a 32‑bit little‑endian word then shifts
  right by bit_offset&7). For portability: treat the bitstream as a sequence where the least significant available bits
  of successive bytes form the code (this differs from some MSB‑first descriptions in older docs).
- Block mode: After a Clear code (256) the implementation discards remaining code slots in the current 8‑symbol block so
  that the next code begins on an 8‑code boundary at the current width (effect: it may skip padding codes). Dictionary
  resets to size 257 and width to 9.
- KwKwK case: If a code references the just‑to‑be‑inserted dictionary entry, output previous expansion + first byte of
  previous expansion.

Decoding loop:
1. Read next code c using current width.
2. If c == 256: perform Clear procedure above; continue.
3. Expand code (existing dictionary entry or KwKwK synthetic) into an output staging buffer; flush to caller respecting
   remaining uncompressed length.
4. If previous_code valid, add dictionary entry (previous_code + first_byte_of_current_expansion). Increase width when
   dictionary size becomes a power of two and < 1<<14.

Stop when produced uncompressed length matches header specification or input is exhausted (error if short).

## Method 13: SIT13 (LZSS + Dual Huffman Trees)

High‑level summary:
- 64 KiB sliding window (initialized to zeros).
- Two code trees (first_tree, second_tree) for literals/length symbols plus an offset tree for match offsets.
- First output symbol uses first_tree. After a literal, decoder switches back to first_tree. After completing a match
  copy, decoder switches to second_tree for the *next* symbol only, then first_tree again unless another match follows.

Stream layout:
1. Initial configuration byte (byte0). High nibble (byte0 >> 4) selects code set variant. If zero, dynamic trees are
   built via a meta‑code; otherwise one of 5 predefined length tables is used (see predefined tables in Appendix A).
2. If dynamic (code_set == 0): meta‑code Huffman tree (fixed) drives extraction of code length sequences for first_tree,
   optionally second_tree (bit 3 of byte0: if set share lengths), and offset_tree (size = (byte0 & 0x07) + 10 symbols).
3. If static (1..5): use built‑in canonical length arrays (first_tree_lengths / second_tree_lengths / offset_tree_lengths).

Symbol values:
- 0..255: literal byte.
- 256..317: match length = value - 253 (i.e. 3..64).
- 318: match length = next 10 bits + 65 (65..1088).
- 319: match length = next 15 bits + 65 (65..32832).
Any other value is invalid.

Offset decoding:
- Decode ov from offset_tree. If ov == 0: offset = 1. Else offset = (1 << (ov - 1)) + next_bits(ov - 1) + 1.
- Source position = current_output_position - offset. Copy length bytes forward (supports overlap) into window/output.

Match staging:
- If the full match won't fit in caller's immediate output buffer, decoder internally stages and resumes next call.

End of stream: Determined solely by total uncompressed length from container header.

## Method 15: SIT15 (BWT + Arithmetic Coding)

Implemented as a separate streaming module (not detailed here). CRCs for method 15 forks are skipped at container layer
because integrity is assumed to be verified internally (header CRC fields may be zero). A full standalone spec would
describe block structure, BWT primary index storage, symbol frequency modeling, and arithmetic coder state machine.

## Other Methods (3,5,8,14)

Not currently implemented. Historical descriptions exist (method 3: static Huffman; 5: LZAH; 8: Miller‑Wegman; 14:
unknown). Implementations should treat them as unsupported and report an error.

## CRC Calculation

Important: The working implementation uses a reflected CRC‑16 with polynomial 0x8005 (standard CRC‑16/IBM) initial
value 0, no final XOR, table‑driven. Historical documents refer to "CCITT 0x1021"; that is NOT what the implementation
currently computes. Header CRCs (classic header, SIT5 header1) and fork CRCs (except method 15) all use the 0x8005
reflected variant here. To reproduce compatibility you must match this algorithm (see sit.c sit_crc_table).

CRC over a header: zero the CRC field bytes before computing. CRC over fork data: stream incremental update of emitted
plaintext bytes (post‑decompression). For method 15 CRC fields may be zero and are not validated by the layer.

## Implementation Guidance

To build a compatible extractor:
1. Scan input for SIT5 or classic signature (allow offset > 0 for embedded archives). Prefer the earliest match.
2. Build an index (classic: sequentially parse headers; SIT5: follow linked entries starting at initial_cursor, tracking
   directories by offset to assemble paths).
3. For each file entry iterate data fork first, then resource fork (skip zero‑length forks). Initialize stream state per
   method.
4. Decompress streaming while updating CRC unless (a) method 15 or (b) encryption flag set (reject).
5. Validate CRC vs. stored value at logical EOF of fork.
6. Expose path (including any nested folder prefixes), Macintosh metadata (type, creator, finder flags), fork type, and
   uncompressed length to caller.

## Appendix A: Static Code Length Tables (Method 13)

Predefined canonical length arrays (first_tree_lengths, second_tree_lengths, offset_tree_lengths) define code sets 1..5.
For brevity they are not reproduced verbatim here; refer to the public domain implementation or ship identical tables.

If a fully self‑contained spec is required without source reference, embed literal numeric arrays exactly as in sit13.c.

## Appendix B: Error Handling Recommendations

An implementation should treat the following as fatal format errors:
- Header overruns beyond archive size.
- Fork compressed span exceeding archive bounds.
- Unsupported compression method ID.
- Encrypted flag set (bit 4 in classic method byte, bit 5 in SIT5 flags) with non‑zero password lengths.
- CRC mismatch (except method 15) after full fork decompression.
- Invalid Huffman or LZW symbol sequences (out‑of‑range code, premature EOF).

Graceful handling: for unknown optional fields, ignore but preserve when re‑packing (out of scope here).

## Appendix C: Future Work

Potential extensions:
- Document full method 15 (SIT15) transform and arithmetic coder.
- Reverse engineer methods 5 (LZAH) and 8 (LZMW) to add streaming support.
- Validate whether some archives actually use CCITT CRC (dual‑mode detection) and, if so, negotiate automatically.

## License

This specification text is provided under the same MIT license as the project source code.

