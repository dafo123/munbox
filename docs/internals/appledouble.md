
# AppleDouble File Format (Version 2)

The AppleDouble format is a method for storing Macintosh files on file systems that do not support the Mac's dual-fork structure (data fork and resource fork). It achieves this by splitting the file into two separate files on the foreign file system.

## References

- Developer’s Note: AppleSingle/AppleDouble Formats for Foreign Files 
- https://www.rfc-editor.org/rfc/rfc1740.html

## Overview

An AppleDouble representation consists of **two separate foreign files**:

1. **AppleDouble Data file** — contains only the **data fork** bytes, with **no header**.
2. **AppleDouble Header file** — contains everything else (e.g., **resource fork**, Finder info, dates, etc.) in a structured container **identical to AppleSingle** except for the magic number and the omission of the data-fork entry.

AppleDouble is versioned. This spec describes **version 2**.

## File Identification

* **Header magic** (4 bytes, big‑endian): `0x00051607` (decimal 3333127)
* **Version** (4 bytes, big‑endian): `0x00020000`

These values appear at the start of the **Header file**. The **Data file** has no header or magic.

## Byte Order

All multi-byte integers are **big‑endian** (most significant byte first).

## AppleDouble Header File Layout

The Header file contains a fixed header followed by an array of entry descriptors and then the corresponding entry data blocks.

### Fixed Header

| Field             | Size | Type  | Value / Notes                                                               |
| ----------------- | ---: | ----- | --------------------------------------------------------------------------- |
| Magic             |    4 | u32   | `0x00051607`                                                                |
| Version           |    4 | u32   | `0x00020000`                                                                |
| Filler            |   16 | bytes | All zero (`0x00`). Reserved.                                                |
| Number of Entries |    2 | u16   | Count **N** of entry descriptors. If **N>0**, exactly N descriptors follow. |

### Entry Descriptors (array of N)

Each descriptor points to one entry’s data payload in the same Header file.

| Field    | Size | Type | Notes                                                                                                               |
| -------- | ---: | ---- | ------------------------------------------------------------------------------------------------------------------- |
| Entry ID |    4 | u32  | Identifies entry type. `0` is invalid. `1..0x7FFFFFFF` reserved by Apple; `0x80000000..0xFFFFFFFF` for private use. |
| Offset   |    4 | u32  | Absolute byte offset **from start of Header file** to entry data.                                                   |
| Length   |    4 | u32  | Length in bytes of entry data (may be `0`).                                                                         |

### Entry Data Area

* The **data for each entry** is stored as a **single contiguous block** of exactly **Length** bytes at **Offset**.
* **No alignment** is required unless your implementation chooses to add padding between entries; if you do, it must be reflected in Offsets.
* **Order is arbitrary**. For efficiency, place frequently-read entries (e.g., Finder Info, File Dates) near the header, and place the **Resource Fork (ID 2)** last because it is commonly extended.

## Standard Entry IDs (v2)

> An AppleDouble Header file **does not** include Entry ID `1` (data fork); the data fork is stored in the separate Data file.

| ID | Name                    | Payload Encoding (Length bytes)                                                                                                                                                              | Notes                                                                                                |
| -: | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
|  2 | **Resource Fork**       | Raw bytes of the Mac **resource fork**.                                                                                                                                                      | Typically placed last for growth.                                                                    |
|  3 | **Real Name**           | Opaque byte string (implementation-defined text for home FS name).                                                                                                                           | Optional; usually not used by AFP servers (they derive names from foreign filename).                 |
|  4 | **Comment**             | Opaque byte string (Mac comment).                                                                                                                                                            | Optional.                                                                                            |
|  5 | **Icon, B\&W**          | Opaque icon data.                                                                                                                                                                            | Optional.                                                                                            |
|  6 | **Icon, Color**         | Opaque icon data.                                                                                                                                                                            | Optional.                                                                                            |
|  8 | **File Dates Info**     | Four **signed 32-bit** integers: `create`, `modify`, `backup`, `access`, each = seconds relative to **00:00:00 GMT, Jan 1, 2000** (2000-01-01T00:00:00Z). Unknown times set to `0x80000000`. | Required if you track times.                                                                         |
|  9 | **Finder Info**         | 32 bytes: 16 bytes **Finder Info** followed by 16 bytes **Extended Finder Info** (exactly as in HFS `ioFlFndrInfo` + `ioFlXFndrInfo`).                                                       | Optional but common on HFS. See note on `frView` for directories.                                    |
| 10 | **Macintosh File Info** | 4 bytes (u32) bitfield for classic Mac file attributes (e.g., locked/protected).                                                                                                             | Times remain in ID 8.                                                                                |
| 11 | **ProDOS File Info**    | 8 bytes: 2-byte **Access**, 2-byte **File Type**, 4-byte **Aux Type**.                                                                                                                       | Times remain in ID 8.                                                                                |
| 12 | **MS‑DOS File Info**    | 2 bytes: DOS attribute bitfield.                                                                                                                                                             | Times remain in ID 8.                                                                                |
| 13 | **Short Name (AFP)**    | Opaque byte string.                                                                                                                                                                          | AFP servers store unique short names here (often auto-derived and starting with `!` if synthesized). |
| 14 | **AFP File Info**       | 2 bytes (u16) AFP attributes word.                                                                                                                                                           | `BackupNeeded` should be set when appropriate.                                                       |
| 15 | **Directory ID (AFP)**  | 4 bytes (u32) AFP directory ID.                                                                                                                                                              | For directories; maintained by server.                                                               |

> **Version 1 note (for readers):** Legacy Entry ID `7` (File Info) existed in v1 and is replaced in v2 by IDs **8** and **10/11/12**. Writers **must** output v2; readers **should** accept v1 and upgrade as needed.

### Finder Directory `frView` Guidance

When creating Finder Info for a **new directory**, set `frView` (in extended Finder info) to a **legal, nonzero** value (e.g., classic `closedView = 256`) so the Finder can display a stable default view if it cannot immediately initialize the directory metadata.

## Filename Derivation (Data/Header)

AppleDouble prescribes how to map a home file’s **Real Name** to **two foreign filenames** (Data + Header). Use character **substitution/deletion** for illegal characters and **truncate** to meet limits. When mapping, the **Data file** gets the content suffix appropriate for the foreign FS; the **Header file** uses a prefix or special extension, depending on the FS.

### ProDOS

* **Data**: truncate to **13 chars** (two less than the 15-char limit) after removing illegal chars.
* **Header**: prefix the Data filename with `R.` (uppercase R, dot).

### MS‑DOS (8.3)

* **Data**: truncate name to **8 chars**; then add the most appropriate **.EXT** (e.g., `.TXT`).
* **Header**: same 8-char stem, extension **`.ADF`**.

### UNIX / NFS

Choose a single convention per volume based on foreign FS capability:

1. **8‑bit**: filenames may contain any 8‑bit character except `/` (0x2F), NUL (0x00), and `%` (0x25). Escape these three by replacing with `%` + **two hex digits** of the byte value.
2. **7‑bit ASCII**: as above, **also** escape all bytes in `0x80..0xFF` using `%` + two hex digits.
3. **7‑bit alphanumeric**: start from 7‑bit ASCII, then **also** escape all non‑alphanumerics **except** underscore `_` (0x5F) and the **last** period `.` (0x2E).

**Header filename rule**: the **Header** filename is the **Data** filename **prefixed with `%`**. (Confusion with other `%`‑prefixed files is resolved by checking the Header magic.)

> Note: Escaping increases length; some systems may not accommodate the full expanded name. Behavior in such cases is unspecified by Apple; implementers may apply additional truncation strategies while preserving uniqueness.

## Usage & Semantics

* **Locking**: Header files **must be locked during access** to ensure integrity (and likewise any RootInfo or server-maintained metadata files you update).
* **Unknown entries**: Readers **must ignore** unknown Entry IDs but **preserve** them (including their bytes, Offsets, and Lengths) when copying or moving files.
* **Directories**: Directories may be represented as AppleDouble pairs (Header + empty Data) if created by the application/server and you need to store AppleDouble metadata for them.
* **Renames**: When the home file is renamed, rename the **foreign Data and Header files** according to the mapping rules to keep the pair associated.
* **Creation policy**: Do **not** create AppleDouble headers for preexisting foreign files/directories unless you need to store AppleDouble entries for them.
* **AFP specifics**: AFP servers maintain short-name mappings (ID 13), AFP attributes (ID 14), and directory IDs (ID 15). When synthesizing short names, use a scheme that guarantees uniqueness, commonly prefixing derived names with `!`.

## Writing & Updating Rules (Normative)

1. **Choose v2**: Always write Version = `0x00020000` and Header Magic = `0x00051607`.
2. **Build entry table**:

   * Compute payloads for each entry you will write.
   * Lay out payload blocks contiguously (in any order). Common practice: place Resource Fork last.
   * Fill each descriptor’s `Entry ID`, `Offset`, and `Length`.
3. **Header assembly**:

   * Write the fixed header, then the **N** descriptors array, then the payload blocks exactly at the recorded Offsets.
   * Keep **Filler** = 16 zero bytes.
4. **Data file**:

   * Write the data-fork bytes as-is. No header or trailer.
5. **Atomicity** (recommended):

   * For updates that grow the Resource Fork, prefer appending in-place when file APIs allow, or write to a temp file and replace.
6. **Preservation**:

   * When rewriting, **preserve** entries you do not interpret; copy their bytes verbatim and update their Offsets if the layout changes.
7. **Timestamps**:

   * Store times in Entry ID 8 as **signed 32-bit** seconds relative to 2000‑01‑01T00:00:00Z. Unknown times = `0x80000000`.
8. **Finder directory view**:

   * If you create directory Finder info, ensure `frView` is set to a valid nonzero value.

## Reading Rules (Normative)

1. **Identify Header**: Verify magic=`0x00051607`, version=`0x00020000`.
2. **Parse**: Read N; validate that descriptor array fits, that each `Offset+Length` is within file bounds, and that Offsets are unique and non-overlapping (unless zero-length).
3. **Locate Data file**: By convention, construct the Data filename from the Header’s filename (or vice‑versa) using the mapping rules in §6. (AppleDouble does not mandate an in-band pointer.)
4. **Interpret entries you know** (IDs in §5). Treat other entries as opaque.
5. **Robustness**: Allow entries in any order; do not require alignment; accept zero-length entries.
6. **Version 1 compatibility** (reader only): If you must support v1, accept magic/version for v1 and map old File Info (ID 7) into v2’s IDs when exposing the content to callers.

## Conformance Checklist

* [ ] Header magic/version correct.
* [ ] Filler is 16×`0x00`.
* [ ] Number of entries matches descriptor count.
* [ ] No Entry ID `1` in AppleDouble Header.
* [ ] Offsets/Lengths validated; payloads contiguous; unknown entries preserved.
* [ ] File Dates Info uses 2000‑01‑01 UTC epoch, signed seconds; unknown= `0x80000000`.
* [ ] Finder Info payload is exactly 32 bytes (16 + 16); if directory, `frView` valid.
* [ ] ProDOS/MS‑DOS/AFP payload sizes exactly as specified.
* [ ] Resource Fork entry placed last (recommended).
* [ ] Name mapping per target FS; Header name `%`‑prefixed (UNIX/NFS) or `.ADF` (DOS) or `R.` (ProDOS).
* [ ] Locking enforced during updates; rename/move keeps Data/Header paired.

## Appendix: Constants (for reference)

```text
APPLEDOUBLE_MAGIC  = 0x00051607
APPLEDOUBLE_VERSION = 0x00020000
ID_RESOURCE_FORK    = 2
ID_REAL_NAME        = 3
ID_COMMENT          = 4
ID_ICON_BW          = 5
ID_ICON_COLOR       = 6
ID_FILE_DATES       = 8
ID_FINDER_INFO      = 9
ID_MAC_FILE_INFO    = 10
ID_PRODOS_FILE_INFO = 11
ID_MSDOS_FILE_INFO  = 12
ID_AFP_SHORT_NAME   = 13
ID_AFP_FILE_INFO    = 14
ID_AFP_DIRECTORY_ID = 15
```
