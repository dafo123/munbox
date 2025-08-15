# The BinHex 4.0 File Format

## References

- https://en.wikipedia.org/wiki/BinHex
- https://files.stairways.com/other/binhex-40-specs-info.txt

# Introduction to BinHex 4.0

The BinHex 4.0 format is a multi-layered binary-to-text encoding and archival system designed to address the specific challenges of transferring classic Macintosh files across computer networks. It is more than a simple encoding scheme; it is a container format engineered to preserve the unique, dual-component structure of Macintosh files, along with their essential metadata, during transmission through environments that could not reliably handle raw 8-bit binary data. Understanding its design requires an appreciation of the technical constraints of its era and the proprietary architecture of the classic Macintosh operating system.

## Historical Context: The Macintosh File Transfer Problem

The development of BinHex 4.0 was driven by two concurrent technical challenges in the mid-1980s: the nature of early computer networks and the structure of the Macintosh File System (MFS), later succeeded by the Hierarchical File System (HFS).

### The Challenge of 7-Bit Networks

Early electronic mail systems and online services, such as CompuServe, were often not "8-bit clean". This meant that their infrastructure was designed to handle only the 128 characters of the 7-bit US-ASCII standard. When 8-bit binary files—containing values from 0 to 255—were transmitted through these systems, control characters could be misinterpreted, high bits could be stripped, and line endings could be altered, resulting in data corruption. To safely transmit binary files, a process known as "ASCII armoring" was required, which involves encoding the 8-bit data into a stream of safe, printable 7-bit ASCII characters.

### The Unique Macintosh File System (MFS/HFS)

Unlike the single-stream file systems of MS-DOS or Unix, the classic Mac OS file system stored files in two distinct parts, known as "forks".

- **Data Fork:** This contained the raw, unstructured content of a file, analogous to a file's contents on other systems. For a text document, this would be the text itself; for an image, the pixel data.
- **Resource Fork:** This contained structured data associated with the file, such as icons, menus, dialog box definitions, sounds, and even executable code segments for applications.

Preserving both forks was critical for the functionality of Macintosh applications and the integrity of many documents. A simple text encoding of the data fork alone would lose the resource fork, rendering an application unusable or stripping a document of its custom elements. Furthermore, the Macintosh file system relied on metadata stored in the file's directory entry, such as a four-character **File Type** (e.g., TEXT) and **Creator** code (e.g., ttxt for SimpleText), which allowed the Finder to associate documents with the application that created them.

### Evolution of BinHex

The BinHex format evolved to meet these challenges. The original BinHex, developed for the TRS-80, was a simple 8-to-4 bit hexadecimal encoding, giving rise to the .hex extension. When ported to the Macintosh, it initially only handled the data fork. Subsequent versions improved upon this foundation:

- **Compact BinHex (BinHex 2.0):** To improve efficiency, creator Yves Lempereur introduced an 8-to-6 bit encoding, which was more space-efficient than the 8-to-4 bit hexadecimal scheme. This incompatible format used the .hcx extension.
- **BinHex 4.0:** Released in 1985, this version represented a major leap in robustness. It skipped version 3.0 to avoid confusion with an unrelated BASIC program. It introduced a comprehensive structure that bundled the data fork, resource fork, and critical file metadata into a single stream, protected each component with its own Cyclic Redundancy Check (CRC), applied a light compression scheme, and then performed the 8-to-6 bit encoding. This new, more resilient format adopted the .hqx extension and became the de facto standard for Macintosh file exchange for over a decade.

## Core Design Philosophy: An Archival Format

The fundamental design of BinHex 4.0 is that of a self-contained, single-file archive. Its primary purpose is not merely to encode binary data into text but to perfectly encapsulate a single Macintosh file—including its dual forks and essential metadata—for transport, and then to allow for its perfect reconstruction at the destination. This makes it fundamentally different from simpler encoding schemes like Uuencode or Base64, which are designed to operate on a single, unstructured stream of binary data and have no native understanding of file system-specific structures like forks or metadata. The format was engineered to preserve the rich, proprietary structure of a Macintosh file, but in doing so, it created a format that was inherently complex and difficult to parse on non-Macintosh systems. This complexity, a direct result of its platform-specific design, ultimately contributed to its decline in favor of more modular, cross-platform approaches, such as combining forks with MacBinary and then encoding the result with Base64.

## Overview of the Multi-Layered Architecture

BinHex 4.0 employs a sequential, multi-layered architecture. During encoding, three distinct processing layers are applied in order; during decoding, these layers are reversed. This layered approach is a primary source of the format's complexity and robustness.

1. **Structural Assembly & Integrity:** First, the file's metadata (filename, type, creator, flags) and the contents of the data and resource forks are assembled into a single, contiguous binary stream. To ensure integrity, three separate CRC checksums are generated and appended: one for the header, one for the data fork, and one for the resource fork.
2. **Compression:** Second, a simple Run-Length Encoding (RLE) scheme is applied to the entire assembled stream. This provides a degree of compression by replacing sequences of repeating bytes with a more compact representation.
3. **7-Bit ASCII Encoding:** Finally, the compressed 8-bit binary stream is converted into a stream of 64 safe, printable 7-bit ASCII characters using an 8-bit to 6-bit mapping, making it suitable for transmission over any network.

# High-Level File Anatomy (.hqx File Structure)

A BinHex 4.0 file (.hqx) is a plain text file with a specific structure that a decoder must be able to parse. The specification deliberately distinguishes between strict rules for encoders and more lenient requirements for decoders to ensure robustness against modifications by mail gateways and different operating systems.

## The Optional Preamble Text

A BinHex file may begin with any amount of arbitrary text, such as email headers or other descriptive information added by mail clients. A compliant decoder MUST scan through and ignore all content until it locates the mandatory identification string.

## The Mandatory Identification String

The encoded data block must be preceded by the following line of text, starting in the first column:

```
(This file must be converted with BinHex 4.0)
```

This string must be followed by a `<return>` sequence. While encoders must write this string exactly, decoders are advised to implement a more flexible parsing strategy for compatibility. A robust decoder should search for a key substring, such as "with BinHex", and then proceed, which allows it to handle files with minor corruption or variations in the introductory text.

## The Colon-Delimited Encoded Data Block

The main block of encoded data is framed by colons (:). It begins with a colon on a new line immediately following the identification string and ends with a colon on the same line as the final characters of the encoded data.

Some older or non-compliant encoders may have produced an extra exclamation mark (!) immediately before the final colon. A robust decoder should be prepared to encounter and ignore a single optional exclamation mark in this position.

## Line Ending and Whitespace Conventions

The specification's distinction between encoder rules and decoder leniency is a deliberate design choice that reflects the format's intended use in an unpredictable network environment where text files were often reformatted. The designers anticipated that gateways would manipulate line endings and whitespace, so they built fault tolerance directly into the decoding specification, making it more resilient than contemporary formats like Uuencode, which was notoriously fragile to whitespace changes.

### Encoder Rules

An encoder MUST adhere to strict formatting rules to create a canonical BinHex file:

- A line break must be inserted after every 64 characters of encoded data.
- The first line of data, which includes the leading colon, will therefore be 64 characters long (e.g., the colon followed by 63 data characters, then a line break).
- The final line, which includes the trailing colon, will be between 2 and 65 characters long, inclusive.
- The line ending sequence, referred to as `<return>`, should be the sequence appropriate for the system generating the file: `<cr>` (carriage return, ASCII 13) for classic Mac OS, `<lf>` (line feed, ASCII 10) for Unix, or `<cr><lf>` for MS-DOS.

### Decoder Rules

A decoder MUST be highly tolerant of whitespace variations to correctly parse files that may have been altered in transit:

- Lines of any length must be accepted. The 64-character rule is for encoders only.
- All `<return>` characters should be ignored wherever they appear. For a decoder, `<return>` is defined as any sequence of carriage returns (`<cr>`), line feeds (`<lf>`), tabs (`<tab>`, ASCII 9), and spaces (`<spc>`, ASCII 32). This tolerance must apply before the first colon, between any two encoded characters, and before the final colon.

# The Unencoded Binary Stream: A Three-Part Structure

This section provides the definitive byte-level specification of the logical data stream after it has been assembled from the source file but *before* RLE compression and 6-bit encoding are applied. All multi-byte integer values (e.g., Word, Long) are stored in **Big-Endian** (most significant byte first) network byte order. The stream is composed of three distinct parts, each with its own CRC: the Header, the Data Fork, and the Resource Fork.

## Master Structure of the Unencoded Stream

The following table provides the master reference for the complete, unencoded binary stream. It is the canonical map for any developer implementing a parser.

| Part           | Field Name            | Offset (from start of part) | Size (bytes) | Data Type     | Description                                                           |
| :------------- | :-------------------- | :--------------------------- | :----------- | :------------ | :-------------------------------------------------------------------- |
| **Header**     | Filename Length       | 0                            | 1            | uint8         | Length of the filename in bytes (n). Range: 1-63.                    |
|                | Filename              | 1                            | n            | Pascal String | The classic Mac OS filename.                                         |
|                | Null Terminator       | 1+n                          | 1            | uint8         | A single null byte (0x00).                                           |
|                | File Type             | 2+n                          | 4            | OSType        | The 4-character file type code (e.g., 'TEXT').                       |
|                | Creator               | 6+n                          | 4            | OSType        | The 4-character creator application code (e.g., 'ttxt').             |
|                | Finder Flags          | 10+n                         | 2            | uint16        | 16 bits of file system flags. See Appendix A.                        |
|                | Data Fork Length      | 12+n                         | 4            | uint32        | Length of the data fork in bytes (DLEN).                             |
|                | Resource Fork Length  | 16+n                         | 4            | uint32        | Length of the resource fork in bytes (RLEN).                         |
|                | Header CRC            | 20+n                         | 2            | uint16        | CRC-16 of the entire header (bytes 0 to 21+n).                       |
| **Data Fork**  | Data Fork Content     | 0                            | DLEN         | byte          | The raw bytes of the data fork.                                      |
|                | Data Fork CRC         | DLEN                         | 2            | uint16        | CRC-16 of the data fork content plus two null bytes.                 |
| **Resource Fork** | Resource Fork Content | 0                         | RLEN         | byte          | The raw bytes of the resource fork.                                  |
|                | Resource Fork CRC     | RLEN                         | 2            | uint16        | CRC-16 of the resource fork content plus two null bytes.             |

The inclusion of both a length-prefix (Pascal-style) and a null terminator (C-style) for the filename is a redundancy. This likely reflects a deliberate design decision to maximize compatibility with parsing tools from different programming environments. The length byte is sufficient for parsing, but the null byte makes it trivial for C-based tools to handle the filename as a standard null-terminated string without manual buffer manipulation.

## Filename and Flag Handling

Correct handling of filenames and Finder flags is essential for restoring the file with full fidelity on a Macintosh system.

- **Filename Validation (Decoder):** When decoding a file for use on a classic Mac OS or compatible system, certain characters in the filename must be translated to be valid. A period (.) at the beginning of a filename should be replaced by a bullet (•, Option-8 on a Mac keyboard). Colons (:) must be replaced by dashes (-). When decoding for A/UX (Apple's old Unix variant), slashes (/) should also be replaced by dashes.
- **Finder Flags (Encoder):** When encoding a file, the 16-bit flags field should be copied directly from the source file's Finder Info metadata.
- **Finder Flags (Decoder):** When decoding, certain flags that relate to the file's state on the original desktop should be cleared to prevent unexpected behavior. Specifically, the OnDesk, Invisible, and Inited flags must be cleared by the decoder. A full reference of the Finder flags is provided in Appendix A.

## Handling of Zero-Length Forks

The format gracefully handles files that may be missing one or both forks. If a fork has a length of zero, its corresponding length field in the header will be 0x00000000. In this case, there will be no content bytes for that fork in the binary stream. However, the 2-byte CRC field for that empty fork must still be present and must contain a value of 0x0000.

# Layer 1: The RLE90 Compression Scheme

After the three-part binary stream is assembled, it is compressed using a simple but nuanced Run-Length Encoding (RLE) algorithm. The special marker byte used to indicate a run is 0x90. This compression layer is often of minimal effectiveness and can even lead to data expansion if the source data is not highly repetitive. Its inclusion was likely a pragmatic choice for the specific types of files common in the 1980s, such as those with large blocks of zero-padding or simple bitmap graphics, rather than a general-purpose compression strategy. This is supported by observations that the RLE feature was often unused or poorly implemented.

## The Fundamental Algorithm

- RLE is only applied for runs of **3 or more** identical, consecutive bytes. Runs of 1 or 2 bytes are passed through to the output stream uncompressed.
- An encoded run consists of three bytes: the byte value to be repeated, the 0x90 marker, and a count byte representing the total number of occurrences (from 3 to 255).
- For example, a run of four 0xFF bytes (FF FF FF FF) is encoded as the three-byte sequence FF 90 04.

## Implementation and Critical Edge Cases

A compliant implementation must correctly handle two critical edge cases involving the 0x90 marker itself. Failure to do so is a common source of incompatibility.

- **Encoding a Literal 0x90 Byte:** If the byte 0x90 appears in the input stream but is not the start of a run of three or more, it must be escaped to prevent a decoder from misinterpreting it as a run marker. This is achieved by following the literal 0x90 with a count byte of 0x00. The sequence `90 00` in the compressed stream decodes to a single 90 byte.
- **Encoding a Run of 0x90 Bytes:** This is the most complex scenario. To encode a run of the 0x90 byte itself, the first 0x90 in the run is escaped as 90 00, and then the subsequent 0x90s are encoded as a normal run. However, the specification indicates a special rule: a run marker (90 followed by a count) can refer to the 0x90 byte itself. The sequence `90 00 90 05` decodes to a single literal 90 followed by a run of five 90s, for a total of six 90s. This is a crucial distinction from a run of a preceding character. The Python source code notes confusion on this point, indicating this is a genuine ambiguity that must be resolved by adhering to the most detailed available specification.

The following table provides clear, unambiguous examples for all RLE encoding scenarios.

| Input Byte Sequence (Hex) | Condition                     | Encoded Output (Hex) | Explanation                                                                                 |
| :------------------------- | :---------------------------- | :------------------- | :------------------------------------------------------------------------------------------ |
| 41 41 41 41                | Run of 4 identical bytes     | 41 90 04             | Standard run of 3+ bytes.                                                                  |
| 41 41                      | Run of 2 identical bytes     | 41 41                | Run is less than 3 bytes; pass through uncompressed.                                       |
| 12 90 34                   | A single, literal 0x90 byte  | 12 90 00 34          | The 0x90 byte is escaped with a 0x00 count.                                                |
| 90 90 90                   | A run of 3 0x90 bytes        | 90 90 90             | Run is 3 bytes. The encoded form 90 90 03 would also be 3 bytes. Specification is ambiguous here, but passing through is safest. |
| 90 90 90 90                | A run of 4 0x90 bytes        | 90 00 90 03          | The first 0x90 is escaped, followed by a run of 3 more 0x90s.                              |
| 2B 90 00 90 05             | Example from spec             | 2B 90 90 90 90 90    | A literal 0x90 (from 90 00) followed by a run of five 0x90s (from 90 05).                 |

# Layer 2: The 8-to-6 Bit ASCII Encoding

This layer performs the core binary-to-text conversion. The process is conceptually similar to Base64 but uses a different character set and framing mechanism. The choice of characters for the encoding table was a deliberate attempt to improve robustness over previous formats like Uuencode, but it was ultimately less successful than the character set chosen for Base64. Some characters in the BinHex set (e.g., !, ', (, )) were still known to be corrupted by some email gateways, making the format less reliable than the subsequent MIME/Base64 standard.

## The Bit-Shifting Mechanism

1. The RLE-compressed binary stream is treated as a continuous sequence of bits.
2. Three 8-bit bytes (a 24-bit group) are read from the input stream.
3. This 24-bit group is re-divided into four consecutive 6-bit values.
4. Each 6-bit value (an integer from 0 to 63) is used as an index into the BinHex 4.0 character table to select a single printable ASCII character for the output stream.
5. This process results in a 33% increase in data size, as 3 bytes of binary input become 4 bytes of text output.

## The BinHex 4.0 Character Set

The following table provides the definitive mapping of 6-bit values to their corresponding ASCII characters. It is based on the corrected character string, which resolves an error present in some older documentation.

| Value | Char | Value | Char | Value | Char | Value | Char |
| :---- | :--- | :---- | :--- | :---- | :--- | :---- | :--- |
| 0     | !    | 16    | 0    | 32    | @    | 48    | P    |
| 1     | "    | 17    | 1    | 33    | A    | 49    | Q    |
| 2     | #    | 18    | 2    | 34    | B    | 50    | R    |
| 3     | $    | 19    | 3    | 35    | C    | 51    | S    |
| 4     | %    | 20    | 4    | 36    | D    | 52    | T    |
| 5     | &    | 21    | 5    | 37    | E    | 53    | U    |
| 6     | '    | 22    | 6    | 38    | F    | 54    | V    |
| 7     | (    | 23    | 7    | 39    | G    | 55    | W    |
| 8     | )    | 24    | 8    | 40    | H    | 56    | X    |
| 9     | *    | 25    | 9    | 41    | I    | 57    | Y    |
| 10    | +    | 26    | @    | 42    | J    | 58    | Z    |
| 11    | ,    | 27    | A    | 43    | K    | 59    | `    |
| 12    | -    | 28    | B    | 44    | L    | 60    | a    |
| 13    | 0    | 29    | C    | 45    | M    | 61    | b    |
| 14    | 1    | 30    | D    | 46    | N    | 62    | c    |
| 15    | 2    | 31    | E    | 47    | O    | 63    | d    |

*The table above reflects the corrected string from an RFC erratum, removing an erroneous space character found in some older documents.*

## Handling End-of-Stream Padding

BinHex 4.0 does not use an explicit padding character like the = in Base64. The encoding process simply terminates when the input stream is exhausted. If the last group of input bytes is not a multiple of three, the output will be a short group of 2 or 3 characters instead of 4. A decoder must correctly handle these short final groups. For example, if only one byte remains in the input stream, it will be split into one 6-bit value and one 2-bit value. These will be padded with zeros to form two 6-bit values, resulting in two final output characters. If two bytes remain, they will produce three 6-bit values and thus three output characters.

# Data Integrity: The CRC-16-CCITT Calculation

BinHex 4.0 uses three separate 16-bit CRCs to ensure the integrity of the header, the data fork, and the resource fork. This "defense-in-depth" approach is a significant improvement over earlier formats that used a single, simple checksum for the entire file. By having independent CRCs, a decoder can identify the specific point of failure with greater precision. For example, if the data fork CRC fails but the header CRC is valid, a program could still recover the file with the correct name and type, even if its content is corrupt.

## Algorithm Specification

The CRC algorithm used is the standard CRC-16-CCITT (also known as CRC-16-X.25).

- **Polynomial:** 0x1021
- **Initial Value:** The 16-bit CRC register is initialized to 0x0000 before the calculation for each of the three parts begins.
- **Data Reflection:** Input and output data are not reflected.
- **Final XOR:** There is no final XOR operation.
- **Bitwise Logic:** The algorithm processes the input data one bit at a time, from the most significant bit (MSB) to the least significant bit (LSB) of each byte. For each bit from the input stream:
  1. The most significant bit of the 16-bit CRC register is checked.
  2. The CRC register is shifted left by 1 position.
  3. The new data bit is moved into the least significant bit position of the CRC register.
  4. If the bit checked in step 1 was a 1, the CRC register is XORed with the polynomial 0x1021.

A byte-wise implementation of this algorithm is provided in Appendix B.

## The Critical CRC Placeholder Rule

A crucial and often-misimplemented aspect of the BinHex CRC calculation is the handling of the CRC field itself. The CRC for each block (Header, Data Fork, or Resource Fork) is calculated over the *entire* block, including the final two bytes where the CRC value will be stored. For the purpose of this calculation, these final two bytes are treated as if they were null bytes (0x0000).

- **Encoding Process:**
  1. Prepare the content block (e.g., the raw data fork bytes).
  2. Append two null bytes (0x0000) to this block.
  3. Calculate the CRC-16-CCITT over this entire new block (content + two nulls).
  4. In the final stream, replace the two appended null bytes with the calculated 16-bit CRC value.
- **Decoding/Verification Process:**
  1. Receive the block (e.g., data fork content + 2-byte received CRC).
  2. Calculate the CRC-16-CCITT over this entire received block.
  3. If the data is valid and has not been corrupted, the final value in the CRC register will be 0x0000. Any other result indicates a transmission error. This is a standard property of this CRC algorithm and provides an efficient verification method.

## Independent CRC Scopes

It is essential to reiterate that there are three separate and independent CRCs. The CRC register must be re-initialized to 0x0000 before calculating the CRC for the Header, again before the Data Fork, and a third time before the Resource Fork.

# A Practical Implementation Guide

This section synthesizes the rules from the previous sections into a coherent, step-by-step workflow for creating a compliant BinHex 4.0 encoder and decoder.

## The Complete Encoding Process (Step-by-Step)

1. **Gather File Information:** From the source Macintosh file, read its filename, File Type, Creator code, Finder Flags, and the complete contents of both its data and resource forks.
2. **Assemble Header Block:** Construct the unencoded header block as defined in Section 4.1. Fill the 2-byte Header CRC field with 0x0000.
3. **Calculate Header CRC:** Compute the CRC-16-CCITT over the entire assembled header block (including the two null bytes at the end). Place the resulting 2-byte CRC value into its designated field in the header.
4. **Assemble Data Fork Block:** Take the raw data fork content. If the fork is empty, this is an empty block. Append a 2-byte 0x0000 placeholder to the end.
5. **Calculate Data Fork CRC:** Compute the CRC over the data fork content plus the two null bytes. Replace the placeholder with the resulting CRC value.
6. **Assemble Resource Fork Block:** Repeat steps 4 and 5 for the resource fork.
7. **Concatenate and Compress:** Concatenate the final Header block, Data Fork block, and Resource Fork block into a single, contiguous binary stream. Apply the RLE90 compression algorithm (as detailed in Section 5) to this entire stream.
8. **6-Bit Encode:** Apply the 8-to-6 bit ASCII encoding (as detailed in Section 6) to the RLE-compressed stream.
9. **Format Final .hqx File:**
   - Write the mandatory identification string: (This file must be converted with BinHex 4.0), followed by a system-appropriate line ending.
   - Write the starting colon (:) on a new line.
   - Write the 6-bit encoded data stream, inserting a line break every 64 characters.
   - Write the final ending colon (:) on the same line as the last data characters.
   - Write a final line ending.

## The Complete Decoding Process (Step-by-Step)

1. **Isolate Encoded Data:** Scan the input .hqx file, ignoring all text until the identification string is found. Locate the first colon (:) after this string and the final colon at the end of the data. The stream of characters between these two delimiters is the encoded payload.
2. **Strip Whitespace & Decode to 8-Bit:** Create a clean stream containing only the 64 valid encoding characters (ignoring all line breaks, spaces, and tabs). Decode this stream from 6-bit characters back into an 8-bit binary stream using the mapping in Section 6.2. This result is the RLE-compressed data.
3. **Decompress RLE:** Apply the RLE90 decompression algorithm (the reverse of Section 5) to the 8-bit binary stream to recover the raw, unencoded three-part data stream.
4. **Partition Stream:** Parse the header from the beginning of the decompressed stream. Read the filename length, data fork length, and resource fork length to determine the boundaries of the three main blocks (Header, Data Fork, Resource Fork).
5. **Verify Header:** Isolate the complete Header block. Calculate its CRC-16-CCITT. If the result is not 0x0000, the file is fundamentally corrupt, and further parsing is unreliable. Abort and report an error.
6. **Verify Data Fork:** If the header is valid, isolate the complete Data Fork block. Calculate its CRC. If the result is not 0x0000, report a data fork corruption error. The file metadata may still be recoverable.
7. **Verify Resource Fork:** Isolate the complete Resource Fork block and verify its CRC. If it fails, report a resource fork corruption error.
8. **Reconstruct File:** If all CRCs are valid, write the extracted data and resource forks to a new file on the target file system. Set the filename, File Type, Creator code, and Finder Flags, applying any OS-specific filename corrections and clearing the necessary flags as described in Section 4.2.

# BinHex 4.0 in Context: A Comparative Analysis

BinHex 4.0 occupies a unique position in the history of data encoding. It was a highly specialized tool designed for a specific ecosystem, contrasting sharply with more generalized formats like Base64 and Uuencode. The following table highlights these differences, providing context for its design trade-offs and historical role.

| Feature                      | BinHex 4.0                                                                          | Base64                                                                              | Uuencode                                                             |
| :--------------------------- | :---------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------- | :------------------------------------------------------------------- |
| **Primary Purpose**          | Macintosh file archival and transport.                                             | General-purpose binary data transport.                                             | Unix binary file transport (primarily email/Usenet).               |
| **Metadata Handling**        | Integrated (File Type, Creator, Forks, Flags).                                     | None. Relies on external mechanisms like MIME headers.                             | Basic (filename and permissions).                                   |
| **Error Checking**           | Integrated (3 x CRC-16-CCITT).                                                     | None. Relies on the transport layer (e.g., TCP).                                   | None. Extremely vulnerable to corruption.                           |
| **Data Overhead**            | ~33% + header overhead; can be reduced by RLE.                                     | ~33%.                                                                               | ~33%.                                                                |
| **Character Set Robustness** | Good, but some characters (!, ', (, )) can be problematic in some gateways.       | Excellent. Designed for maximum safety in MIME.                                    | Poor. Uses space and other characters easily corrupted by gateways. |
| **Framing**                  | Colon-delimited (:) with mandatory ID string.                                      | None. MIME provides Content-Transfer-Encoding header.                              | begin/end lines with filename/mode.                                 |
| **Streaming Capability**     | Difficult. The entire file must be assembled before RLE and encoding, and partitioned after decoding. | Trivial. Can be encoded/decoded on the fly. | Possible, but line-based structure requires state management.       |

# Conclusion

The BinHex 4.0 format stands as a testament to the innovative solutions developed to overcome the limitations of early computer networks. It was not merely a binary-to-text encoding but a sophisticated archival container, meticulously designed to preserve the unique dual-fork architecture and rich metadata of classic Macintosh files. Its multi-layered approach—combining structural assembly, triple-CRC integrity checks, run-length encoding, and 7-bit ASCII armoring—provided a robust, all-in-one solution for Mac-to-Mac file exchange in an era of 7-bit-unsafe communication channels.

However, the very features that made BinHex 4.0 so effective for its target ecosystem also became its primary liabilities in a broader, cross-platform context. Its inherent complexity, tight coupling with the HFS file system, and the difficulty of extracting data on non-Macintosh systems hindered its interoperability. While its character set was an improvement over predecessors, it was less resilient than the one later adopted by the MIME/Base64 standard, which ultimately became the universal solution for binary data in email.

Today, BinHex 4.0 is a legacy format, primarily of interest to digital archivists, retro-computing enthusiasts, and developers maintaining systems that must process historical data. A compliant implementation requires careful attention to its many details: the lenient parsing of the text wrapper, the precise byte-level structure of the unencoded stream, the nuanced edge cases of the RLE90 compression scheme, and the critical placeholder rule for CRC calculation. This document provides the exhaustive technical detail necessary to create such an implementation, ensuring that the data encapsulated within these historical .hqx files can continue to be accessed and preserved with full fidelity.

# Appendices

## Appendix A: Macintosh Finder Flags Reference

The 16-bit Finder Flags field in the BinHex header contains metadata used by the classic Mac OS Finder to control file behavior and appearance. The following table details each flag. When decoding, an implementation should clear the flags marked with an asterisk (*) to ensure predictable behavior on the destination system.

| Bit | Hex Value | Flag Name           | Description                                                                                                     |
| :-- | :-------- | :------------------ | :-------------------------------------------------------------------------------------------------------------- |
| 15  | 0x8000    | isAlias             | The file is an alias (a type of symbolic link).                                                                |
| 14  | 0x4000    | isInvisible*        | The file should not be displayed in the Finder. This flag should be cleared by decoders.                      |
| 13  | 0x2000    | hasBundle           | The file has a BNDL resource that groups related files and icons.                                              |
| 12  | 0x1000    | nameLocked          | The file's name cannot be changed from the Finder.                                                             |
| 11  | 0x0800    | isStationery        | The file is a "Stationery Pad" (template). Opening it creates a new, untitled copy.                           |
| 10  | 0x0400    | hasCustomIcon       | The file has a custom icon stored in its resource fork.                                                        |
| 9   | 0x0200    | reserved            | This bit is reserved and should be zero.                                                                       |
| 8   | 0x0100    | isLocked            | The file is locked and cannot be modified or deleted without user confirmation.                                |
| 7   | 0x0080    | hasBeenInited*      | The Finder has "seen" this file and assigned it a position in a window. This flag should be cleared by decoders. |
| 6   | 0x0040    | hasNoINITs          | The file contains no INIT resources.                                                                           |
| 5   | 0x0020    | isShared            | The application can be opened by multiple users simultaneously (on a file server).                            |
| 4   | 0x0010    | requiresSwitchLaunch| Obsolete flag for MultiFinder.                                                                                 |
| 3   | 0x0008    | isColor             | Color bits for the file's appearance in the Finder (obsolete).                                                 |
| 2   | 0x0004    | OnDesk*             | The file is on the desktop. This flag should be cleared by decoders.                                          |
| 1   | 0x0002    | reserved            | This bit is reserved and should be zero.                                                                       |
| 0   | 0x0001    | isBusy              | The file is currently open or in use.                                                                          |

## Appendix B: Core Algorithm Implementations (Pseudo-code)

This appendix provides language-agnostic pseudo-code for the two most complex algorithms to serve as a definitive implementation reference.

### Pseudo-code for RLE90 Encoder

```
function RLE90_encode(input):
    output = []
    input_length = length(input)
    i = 0
    
    while i < input_length:
        current_byte = input[i]
        run_length = 1
        
        // Count consecutive identical bytes (up to 255)
        while i + run_length < input_length and 
              input[i + run_length] == current_byte and 
              run_length < 255:
            run_length = run_length + 1
        
        // Emit the byte
        output.append(current_byte)
        
        // Handle run encoding
        if run_length == 1:
            // Single byte, no run
            i = i + 1
        else if run_length == 2 and current_byte != 0x90:
            // Two bytes, emit the second one explicitly
            output.append(current_byte)
            i = i + 2
        else:
            // Run of 2+ bytes, or any run of 0x90
            output.append(0x90)
            output.append(run_length)
            i = i + run_length
        
        // Special case: if we emit a literal 0x90, it must be followed by 0x00
        if current_byte == 0x90 and run_length == 1:
            output.append(0x00)
    
    return output
```

### Pseudo-code for RLE90 Decoder

```
function RLE90_decode(input):
    output = []
    input_length = length(input)
    i = 0
    
    while i < input_length:
        current_byte = input[i]
        output.append(current_byte)
        i = i + 1
        
        // If we just read 0x90, check what follows
        if current_byte == 0x90 and i < input_length:
            next_byte = input[i]
            i = i + 1
            
            if next_byte == 0x00:
                // Literal 0x90, already appended above
                continue
            else:
                // Run encoding: repeat the previous byte (next_byte - 1) more times
                // Note: The previous byte was already appended once
                for j = 0 to next_byte - 2:
                    output.append(current_byte)
    
    return output
```

### Pseudo-code for CRC-16-CCITT Calculation

```
function CRC16_CCITT(data, initial_value = 0x0000):
    crc = initial_value
    
    for each byte in data:
        crc = crc XOR (byte << 8)
        
        for bit = 0 to 7:
            if (crc & 0x8000) != 0:
                crc = (crc << 1) XOR 0x1021
            else:
                crc = crc << 1
            
            // Keep CRC as 16-bit value
            crc = crc & 0xFFFF
    
    return crc
```

## Appendix C: Character Set and Encoding Tables

### Complete BinHex 4.0 Character Set

The BinHex encoding uses a 64-character alphabet. Each 6-bit value (0-63) maps to a specific ASCII character:

```
Value: Character
  0-25: A-Z
 26-51: a-z
 52-61: 0-9
    62: !
    63: `
```

### Character-to-Value Lookup Table

For decoding, implementations need a reverse lookup table. Invalid characters should be ignored during parsing:

| Char | Value | Char | Value | Char | Value | Char | Value |
| :--: | :---: | :--: | :---: | :--: | :---: | :--: | :---: |
|  A   |   0   |  a   |  26   |  0   |  52   |  !   |  62   |
|  B   |   1   |  b   |  27   |  1   |  53   |  `   |  63   |
|  C   |   2   |  c   |  28   |  2   |  54   |      |       |
|  D   |   3   |  d   |  29   |  3   |  55   |      |       |
|  E   |   4   |  e   |  30   |  4   |  56   |      |       |
|  F   |   5   |  f   |  31   |  5   |  57   |      |       |
|  G   |   6   |  g   |  32   |  6   |  58   |      |       |
|  H   |   7   |  h   |  33   |  7   |  59   |      |       |
|  I   |   8   |  i   |  34   |  8   |  60   |      |       |
|  J   |   9   |  j   |  35   |  9   |  61   |      |       |
|  K   |  10   |  k   |  36   |      |       |      |       |
|  L   |  11   |  l   |  37   |      |       |      |       |
|  M   |  12   |  m   |  38   |      |       |      |       |
|  N   |  13   |  n   |  39   |      |       |      |       |
|  O   |  14   |  o   |  40   |      |       |      |       |
|  P   |  15   |  p   |  41   |      |       |      |       |
|  Q   |  16   |  q   |  42   |      |       |      |       |
|  R   |  17   |  r   |  43   |      |       |      |       |
|  S   |  18   |  s   |  44   |      |       |      |       |
|  T   |  19   |  t   |  45   |      |       |      |       |
|  U   |  20   |  u   |  46   |      |       |      |       |
|  V   |  21   |  v   |  47   |      |       |      |       |
|  W   |  22   |  w   |  48   |      |       |      |       |
|  X   |  23   |  x   |  49   |      |       |      |       |
|  Y   |  24   |  y   |  50   |      |       |      |       |
|  Z   |  25   |  z   |  51   |      |       |      |       |

