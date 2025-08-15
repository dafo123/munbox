# MacBinary (Macintosh Binary Transfer Format)

This document describes MacBinary II and the MacBinary II+ extension. MacBinary serializes a **single classic Mac file** (MFS/HFS) into a single binary stream that preserves:

* the Finder metadata (name, type/creator, flags, positions, dates),
* the **data fork**, and
* the **resource fork**.

It was designed for storage/transfer on systems that don’t support dual-fork files or Finder metadata. A canonical MacBinary file has: a **128-byte header**, followed by the **data fork** (padded to a 128-byte boundary), then the **resource fork** (padded to a 128-byte boundary). An optional Finder comment may be sent afterward (rare in practice).

**MacBinary II** (1987) is the de-facto flavor in the wild; it adds fields and a header CRC to the original format. **MacBinary II+** (1993, little-used) extends II to package **directory trees** by inserting special “start/end” folder blocks in the stream, similar in spirit to `tar`.

---

## References and Acknowledgements

* Library of Congress: format overview, structure, versions, signifiers. ( https://loc.gov/preservation/digital/formats//fdd/fdd000589.shtml)
* MacBinary II changes/spec notes (field offsets, flags behavior, validation). (https://files.stairways.com/other/macbinaryii-standard-info.txt))
* MacBinary II+ preliminary spec (folder Start/End blocks, layout, rules). (https://files.stairways.com/other/macbinaryiiplus-spec-info.txt)
* CRC specifics: CiderPress2 notes (XMODEM variant); Maconv format doc (CCITT 0x1021, init 0). These describe the same algorithmic parameters in different terminology. (https://ciderpress2.com/formatdoc/MacBinary-notes.html)

---

## Global Rules

* **Byte order:** All multi-byte integers are big-endian (Motorola 68000 order).
* **Alignment:** The header is exactly 128 bytes. Each subsequent part (**data fork**, **resource fork**, and any optional extra blocks/comments in II+\*\*) is padded with `0x00` to the next 128-byte boundary.
* **Zero-fill:** Any header bytes not explicitly defined must be set to zero.

---

## MacBinary II File Layout

```
+-------------------------+  128-byte header (MacBinary II)
|  Header (128 bytes)     |
+-------------------------+
|  Data fork (N bytes)    |  raw data fork bytes
|  ...padding to 128-byte |
|  boundary with 0x00     |
+-------------------------+
|  Resource fork (M bytes)|
|  ...padding to 128-byte |
|  boundary with 0x00     |
+-------------------------+
[ Optional "Get Info" comment (FCMT) if length>0, padded to 128 bytes ]
```

---

## MacBinary II Header (128 bytes)

**Offsets are from start of file (0-based). Types: Byte=8b, Word=16b, Long=32b.**

|  Offset |     Size | Name / Meaning                                                                                                                                                                                                                        |
| ------: | -------: | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|       0 |        1 | **Old version number** — keep **0** for compatibility. Used to distinguish headers; see validation rules.                                                                                                  |
|       1 |        1 | **Filename length** (1..63). The filename is Pascal-style: this length and then up to 63 bytes at offsets 2..64.                                                                                           |
|    2–64 | up to 63 | **Filename** (only first “length” bytes significant; remaining bytes must be zero). No trailing NUL required.                                                                                               |
|      65 |        4 | **File type** (4 ASCII chars, e.g., `"TEXT"`).                                                                                                                                                              |
|      69 |        4 | **Creator** (4 ASCII chars).                                                                                                                                                                               |
|      73 |        1 | **Finder flags (high byte)** — original Finder flags (bits 15..8 of Finder’s `fdFlags`). Bit meanings include Locked, Invisible, Bundle, System, Busy, Changed, Inited; see note below.                 |
|      74 |        1 | **Zero fill** — must be 0.                                                                                                                                                                                |
|      75 |        2 | **File’s vertical position** (Finder window).                                                                                                                                                            |
|      77 |        2 | **File’s horizontal position** (Finder window).                                                                                                                                                       |
|      79 |        2 | **Window/folder ID** (Finder).                                                                                                                                                                            |
|      81 |        1 | **Protected flag** (low-order bit).                                                                                                                                                                        |
|      82 |        1 | **Zero fill** — must be 0 (also used in validation, see §7).                                                                                                                                         |
|      83 |        4 | **Data fork length** (bytes; may be 0). Upper bound recommended ≤ `0x007FFFFF`.                                                                                                                        |
|      87 |        4 | **Resource fork length** (bytes; may be 0). Upper bound recommended ≤ `0x007FFFFF`.                                                                                                                         |
|      91 |        4 | **Creation date** (Mac epoch; seconds since 1904-01-01).                                                                                                                                          |
|      95 |        4 | **Modification date** (Mac epoch).                                                                                                                                                                        |
|      99 |        2 | **Get Info comment length** (FCMT) — if non-zero, a Finder comment is sent **after** the resource fork. In practice rarely used; Apple discouraged R/W of comments at the time.                            |
|     101 |        1 | **Finder flags (low byte)** — bits 7..0 of Finder’s flags; together with offset 73 form the full 16-bit Finder flag word. (New in II.)                                                                   |
| 102–115 |       14 | **Reserved/zero** (see note: programs may store packaging info; but set to zero if not used).                                                                                                              |
|     116 |        4 | **Unpacked length** — for on-the-fly packers (e.g., PackIt). Uploaders of a **single file** must write **0**; downloaders that don’t unpack can ignore. (New in II.)                                      |
|     120 |        2 | **Secondary header length** — if non-zero, skip this many bytes (padded to 128) immediately after the main header. **When sending files, this must be 0**; field exists for future expansion. (New in II.) |
|     122 |        1 | **MacBinary II writer version** — **starts at 129** for II; (III uses 130, see note).                                                                                                                     |
|     123 |        1 | **Minimum MacBinary version required** — set **129** for II compatibility. If decoder’s capability < this value, it must treat content as opaque binary and warn user.                                    |
|     124 |        2 | **CRC-16 of bytes 0..123** (header CRC; see §6). (New in II.)                                                                                                                                          |

**Finder flags note & post-download sanitization:** When **downloading** (decoding) a MacBinary II file, **clear** the following Finder flag bits and location info to avoid stale or inappropriate UI state:

* Clear flags:
  desktop (bit 0 — “on Desktop”), bFOwnAppl (bit 1, internal), **Inited** (bit 8), **Changed** (bit 9), **Busy** (bit 10).
* Zero out `fdLocation` (position) and `fdFldr` (window/folder ID).

---

## Optional “Get Info” Comment (FCMT)

If header offset **99** (word) is non-zero, a Finder Comment of that length is present **after the resource fork** (and its alignment padding). Historically, Apple discouraged programs from reading/writing Finder comments; the feature remained for possible future use, and most implementations omitted it. Encoders should write **0**; decoders may ignore if present.

---

## Header CRC (MacBinary II)

* **Field:** 16-bit CRC at header offsets **124–125**, computed over header bytes **0..123** (inclusive).
* **Variant:** **CRC-16/XMODEM** (equivalently “CRC-16-CCITT (false)” in many libraries):

  * Polynomial: **0x1021**
  * Initial value: **0x0000**
  * Input reflected: **No**
  * Output reflected: **No**
  * Final XOR: **0x0000**

Multiple independent references confirm this choice for MacBinary II’s header CRC.

**Implementation tip:** Most CRC libraries expose this as “CRC-16/XMODEM” or “CRC-CCITT (0x0000 init)”. Verify with a known header: change any header byte and watch the CRC change; a header of all zeros yields a CRC of **0x0000**, which is relevant to validation heuristics (see below).

---

## Header Validation (MacBinary II)

A robust decoder should apply these checks, in this order (all from the II notes unless noted):

1. **Bytes 0 and 74 must both be 0.** If either is non-zero, reject as not MacBinary.
2. If (0,74 are zero), then either:

   * **CRC matches** → it **is** MacBinary II, **or**
   * **Byte 82 is zero** (and CRC is absent/ignored) → **may** be MacBinary I; accept under MB-I rules. (For II files, byte 82 is currently kept zero for MB-I compatibility.)
3. Additional sanity heuristics (helpful for discriminating random/bad input):

   * Offsets **101–125** should all be **0** *unless* you’re reading MacBinary II; a stricter II detector can use the CRC + version bytes.
   * Filename length (offset **1**) must be **1..63**.
   * Fork lengths (offsets **83** and **87**) should be **0..0x007FFFFF** (<= \~8 MB each; historical bound).

**Edge case:** A header of (mostly) zeros causes the CRC to be zero, which can fool simplistic validators. Don’t rely **only** on “CRC==0 means valid”; apply all header checks above.

---

## Encoding (MacBinary II) — Required Steps

1. **Gather metadata** from the source file (classic Mac semantics): filename (≤63 chars), type/creator, Finder flags/positions, dates; data/resource fork lengths.
2. **Assemble 128-byte header** per table in §4.

   * Write big-endian integers.
   * Set undefined bytes to **0x00**.
   * Set **122=129** (writer version), **123=129** (minimum version) for MacBinary II.
   * **120 (secondary header length)** must be **0**.
   * If you do not send a Finder comment, set **99=0**.
3. **Compute CRC-16/XMODEM** over bytes 0..123 and store at 124..125.
4. **Write data fork** bytes, then pad with zeros to the next 128-byte boundary.
5. **Write resource fork** bytes, then pad with zeros to the next 128-byte boundary.
6. **(Optional) Finder comment:** if **99>0**, write exactly that many bytes next, then pad to a 128-byte boundary. (Seldom used; most encoders omit.)

---

## Decoding (MacBinary II) — Required Steps

1. **Read 128-byte header**; verify **0** and **74** are zero. If not, reject.
2. **Verify CRC** (0..123 against 124..125). If it matches, treat as MacBinary II. If it doesn’t, but **82==0**, it may be MacBinary I; proceed cautiously (outside the scope here).
3. **Extract fork lengths** and **dates**; read **data fork**, then **resource fork**, honoring the stated lengths and skipping alignment padding to the next 128-byte boundary after each.
4. **Optionally read Finder comment** if header **99>0**; otherwise stop.
5. **Reconstitute file** on a system that supports HFS semantics; if not, store forks separately or in a host-specific container.
6. **Sanitize Finder flags** and zero window/folder positions per §4 note.

---

## MacBinary II+ (Directory Trees)

**Goal:** Extend MacBinary II to carry **folders and their contents** by inserting special **Start** and **End** blocks that bracket each folder. These are 128-byte blocks, similar in layout to the MacBinary II header but marked to be unmistakably folder delimiters. An **II+ stream** must start with a **Start Block** (representing the root folder). Files inside are encoded exactly as **MacBinary II** file records (version 0 in byte 0).

### Stream Layout (conceptual)

```
StartBlock("Root")                    <-- version byte = 1
  [ optional Secondary Header for StartBlock ]
  [ optional Comment for StartBlock ]
  File 1 (normal MacBinary II header+forks, version byte = 0)
  File 2 (normal MacBinary II header+forks, version byte = 0)
  StartBlock("Subfolder A")           <-- nested folder
    ...
  EndBlock                            <-- closes "Subfolder A"
  ...
EndBlock                              <-- closes "Root"
```

All blocks/files are padded to 128-byte boundaries exactly like MacBinary II.

### Start Block (Folder begin)

* **Offset 0 (Byte):** **version = 1** — **incompatible with previous decoders**, i.e., this marks II+ content.
* **Offset 1 (Byte):** **Folder name length** (1..63).
* **Offsets 2–64:** **Folder name** (Pascal-style, zero-padded).
* **Offset 65 (Long):** **File type = `'fold'`**
* **Offset 69 (Long):** **Creator = `0xFFFFFFFF`**
* **Offsets 73..79/81 etc.:** Finder flags (high), zero-fill, vertical/horizontal positions, window/folder ID, protected flag — same layout as MacBinary II.
* **Offsets 83,87 (Long):** **Data/Resource lengths = 0** for folders.
* **Offsets 91,95 (Long):** **Creation/Modification dates** for the folder.
* **Offset 99 (Word):** **Comment length** (optional; may be >0).
* **Offset 101 (Byte):** Finder flags (low).
* **Offset 116 (Long):** **Unpacked total length** of all files when unpacked; **may be zero** to avoid pre-parsing.
* **Offset 120 (Word):** **Secondary header length** — **may be non-zero**. If so, the **secondary header follows immediately** after the Start Block (padded to 128).
* **Offsets 122,123 (Byte,Byte):** **MacBinary II version bytes** — **130, 130** recommended in the II+ note.
* **Offset 124 (Word):** **CRC-16** over bytes 0..123 (same CRC rules as II).

**Placement of secondary header & comment (II+):**
If present, the **secondary header** comes immediately after the Start Block (padded to 128). If a **comment** is present (offset 99 > 0), it comes **immediately after** the Start Block **or** after its secondary header, then padded.

### End Block (Folder end)

* **Offset 0:** **version = 1** (folder delimiter).
* **Offset 65 (Long):** **File type = `'fold'`**
* **Offset 69 (Long):** **Creator = `0xFFFFFFFE`** (distinct from StartBlock’s `0xFFFFFFFF`).
* **Offset 116 (Long):** **Unpacked total length** (may be zero).
* **Offset 120 (Word):** **Secondary header length** (may be non-zero, same semantics).
* **Offsets 122,123:** **130, 130**
* **Offset 124:** **CRC-16** over bytes 0..123.

All other bytes in the End Block may be zero; **decoders must not rely on them**. Recognize **Start vs End** by the tuple *(version==1, type=='fold', creator==Start:0xFFFFFFFF vs End:0xFFFFFFFE)*.

### Files inside folders

**Internal files** inside a II+ stream are **standard MacBinary II records** with **offset 0 (version) = 0** and version bytes **122=129, 123=129**. Encoders should **zero-fill** any unspecified bytes in **all** headers/blocks, and **pad** every unit to a 128-byte boundary.

---

## Differences: MacBinary II vs. MacBinary II+

| Aspect           | MacBinary II                                                          | MacBinary II+                                                                                                                                                             |
| ---------------- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scope            | Encodes **one file** (header + data fork + resource fork + padding).  | Encodes a **directory tree** by bracketing folders with **Start/End blocks**; files inside are ordinary MacBinary II file records. ([files.stairways.com][3])             |
| Top-level marker | Byte 0 = **0** (old version field), CRC present at 124..125.          | Top of stream **must** be a **Start Block** with **byte 0 = 1**, type `'fold'`, creator `0xFFFFFFFF`. End of folder uses creator `0xFFFFFFFE`. ([files.stairways.com][3]) |
| CRC              | CRC-16/XMODEM over header bytes 0..123 stored at 124..125.            | Same CRC on Start/End blocks and on any internal file headers. ([ciderpress2.com][4], [Applefritter: Git][5])                                                             |
| Secondary header | Field is present (offset 120) but **must be 0** in files sent per II. | Start/End blocks **may** have a non-zero secondary header immediately following the block. ([files.stairways.com][2])                                                     |
| Comments         | FCMT comment after resource fork if header 99>0 (rare).               | Start Block may have its own comment; ordinary file records behave like II. ([files.stairways.com][3])                                                                    |
| Adoption         | Widely used historically.                                             | **Proposed** and implemented by few tools; **little-used** relative to II. ([The Library of Congress][1])                                                                 |

---

## Practical Notes & Edge Cases

* **File sizes and alignment:** Always trust the fork lengths in the header and then **skip padding** to the next 128-byte boundary before reading the next part. (Never infer length from file size alone.) ([The Library of Congress][1])
* **Filename constraints:** Length 1..63, Pascal-style; do not NUL-terminate or overrun 63 bytes. ([files.stairways.com][2])
* **Finder flags (low byte):** MacBinary II added offset **101** to capture the low byte of Finder flags; MacBinary I had only the high byte at 73. Ensure you preserve both across encode/decode. ([files.stairways.com][2])
* **Validation robustness:** Do **all** basic checks (0 & 74 == 0, reasonable lengths, reasonable filename length) **and** verify CRC. Don’t accept “CRC==0” as the only test. ([files.stairways.com][2], [giga.cps.unizar.es][6])
* **Version numbers:** For MacBinary II **write 122=129, 123=129**. MacBinary III later used 130; LoC notes the version landscape. (This spec does not cover III in detail.) ([files.stairways.com][2], [The Library of Congress][1])

---

## Minimal Decoder/Encoder Algorithms (Pseudocode)

### Decoder (MacBinary II + optional II+)

1. **Read 128 bytes** → `hdr`.
2. **If** `hdr[0]!=0 && !(hdr[0]==1 && type=='fold')` → not MacBinary II/II+; reject.
3. **If** `hdr[0]==1` → **II+ folder Start/End** path:

   * Verify `type=='fold'` and `creator` is `0xFFFFFFFF` (Start) **or** `0xFFFFFFFE` (End).
   * If Start: optionally read **secondary header** (len at 120), then **optional comment** (len at 99), then iterate children until matching EndBlock.
   * If End: return to parent folder. ([files.stairways.com][3])
4. **Else (hdr\[0]==0)** → **MacBinary II file** path:

   * Validate bytes 0 & 74 are 0; check filename length 1..63.
   * Verify CRC16(XMODEM) of bytes 0..123 against 124..125.
   * Read **data fork** length at 83; consume that many bytes, then pad to 128.
   * Read **resource fork** length at 87; consume that many bytes, then pad to 128.
   * If **comment length** (99) > 0, read it (and its padding).
   * Reconstitute file; **clear** flags (0,1,8,9,10) and zero location/folder ID. ([files.stairways.com][2])

### Encoder (MacBinary II)

1. Build header per §4 (set `0=0`, `74=0`, `82=0`, `122=129`, `123=129`, lengths at 83/87, dates at 91/95, flags at 73/101).
2. Compute CRC-16/XMODEM over 0..123; store at 124..125. ([ciderpress2.com][4], [Applefritter: Git][5])
3. Write header(128), data fork (+pad), resource fork (+pad), optional comment (+pad).

### Encoder (MacBinary II+ tree)

1. **StartBlock:** write a 128-byte block with `0=1`, `type='fold'`, `creator=0xFFFFFFFF`, name fields as folder name; set optional secondary header & comment if used; compute/store CRC.
2. For each child:

   * If file → write standard **MacBinary II** file record (as above).
   * If subfolder → recursively write **StartBlock**, children, **EndBlock**.
3. **EndBlock:** write a 128-byte block with `0=1`, `creator=0xFFFFFFFE`, CRC; other fields may be zero; decoders must not rely on them. ([files.stairways.com][3])

---

## Interop & Identification

* Typical extensions: `.bin`, `.macbin`; media type sometimes reported as `application/macbinary` or `application/x-macbinary`. ([The Library of Congress][1])
* Magic/identification: different PRONOM signatures exist for MacBinary I/II/III; however, **for II**, robust identification should follow §7 rather than rely solely on magic bytes, because many fields are **0x00** by design. LoC summarizes signifiers and PRONOM IDs. ([The Library of Congress][1])

---

## Conformance Checklist

To be **100% compatible** with MacBinary II and II+:

* [ ] Big-endian integers everywhere; undefined header bytes are **0x00**. ([files.stairways.com][2])
* [ ] Header is exactly 128 bytes; **CRC-16/XMODEM** over bytes 0..123 placed at 124..125. ([ciderpress2.com][4], [Applefritter: Git][5])
* [ ] Data fork and resource fork written in that order, each padded to 128-byte boundary. ([The Library of Congress][1])
* [ ] Finder comment only if header 99>0; then padded to 128. (Most tools omit.) ([files.stairways.com][2])
* [ ] On decode, clear specified Finder bits and zero positions/IDs before writing out. ([files.stairways.com][2])
* [ ] **II+ only:** Start with **StartBlock** (`0=1`, `'fold'`, creator `0xFFFFFFFF`), end each folder with **EndBlock** (creator `0xFFFFFFFE`); allow optional secondary headers/comments after StartBlock; files inside are ordinary MacBinary II records (`0=0`). ([files.stairways.com][3])
