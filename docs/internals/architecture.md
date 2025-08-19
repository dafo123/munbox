
# Architecture Description

## 1. Introduction

**munbox** is a modern, portable **C** library plus CLI for unpacking classic Macintosh container & archive formats: **BinHex** (`.hqx`), **MacBinary** (`.bin`), **StuffIt** (classic + SIT5, `.sit`), and **Compact Pro** (`.cpt`). Formats can be arbitrarily nested (e.g. `file.sit.hqx`) and munbox discovers and chains the necessary decoders automatically.

The project centres on a **unified layer abstraction**: every processing stage (decoder, archive iterator, wrapper) is a `munbox_layer_t`. Layers optionally provide fork / file iteration and/or stream transformation using the same tiny vtable: `open(FIRST|NEXT)` + `read()` + `close()`.

## 2. Core Design Principles

### 2.1 Pull‑Based Streaming
Data flows only when the consumer calls `read()`. This implicit back‑pressure keeps memory usage bounded and allows very large archives to be processed without full buffering (except for formats that inherently require in‑memory structures—SIT classic/SIT5 and CPT currently load directory data for random access within the archive).

### 2.2 Single Uniform Interface
Every stage is a `munbox_layer_t`. No separate *transformer* vs *extractor* types. A layer may:
* only transform bytes (HQX)
* only iterate archive entries (SIT / SIT5)
* do both (MacBinary, CPT)

### 2.3 Non‑Consuming Format Detection
Format factories probe by calling `open(MUNBOX_OPEN_FIRST)` then performing a **bounded** `read()` of signature bytes. If the probe succeeds the factory rewinds (another `open(FIRST)`) and returns a new layer; otherwise it returns `NULL` without altering the input stream state as observed by the next factory.

### 2.4 Forward‑Only Simplicity
Random seek is deliberately out of scope. When a format *requires* random access inside the archive (e.g. SIT directory tables) the layer may hold the archive bytes in memory, but the public interface remains forward sequential iteration of forks.

### 2.5 Minimal Surface Area
The public header (`include/munbox.h`) exposes only the layer struct, basic constructors, the process/detect function, and error helpers. Previous callback-based extraction APIs were removed; applications now implement their own file writing loops for maximum control.

## 3. Public Abstractions

```c
typedef enum { MUNBOX_OPEN_FIRST = 0, MUNBOX_OPEN_NEXT = 1 } munbox_open_t;
typedef enum { MUNBOX_FORK_DATA = 0, MUNBOX_FORK_RESOURCE = 1 } munbox_fork_t;

typedef struct munbox_file_info {
    char filename[256];
    uint32_t type;
    uint32_t creator;
    uint16_t finder_flags;
    uint32_t length;      /* length of currently opened fork (advisory) */
    int /* munbox_fork_t */ fork_type; /* which fork is active */
    bool has_metadata;
} munbox_file_info_t;

typedef struct munbox_layer {
    ssize_t (*read)(struct munbox_layer *self, void *buf, size_t cnt);
    void    (*close)(struct munbox_layer *self);
    int     (*open)(struct munbox_layer *self, munbox_open_t what, munbox_file_info_t *info);
    void   *internal_state;
} munbox_layer_t;
```

Usage contract: `open(FIRST, &info)` must succeed (return 1) before the first `read()`. Each `open(NEXT, &info)` either advances to the next fork/file (return 1) or returns 0 (end). `read()` returns bytes for the *current* fork until 0 (EOF for that fork). After EOF you must call `open(NEXT)` to continue.

## 4. Detection & Chaining (`munbox_process_new`)

```c
munbox_layer_t *munbox_process_new(munbox_layer_t *initial_layer);
```

Internally a static ordered table of `munbox_format_handler_t { name, layer_factory }` is traversed repeatedly. For the current tail layer we try each factory:
1. If factory returns NULL ➜ not recognised, try next factory.
2. If it returns a new layer ➜ print detection message, replace tail, restart from first factory (allowing multi‑stage chains such as HQX → MacBinary → SIT5).
3. When no factory matches, stop and return the final layer to the caller.

The caller then performs iteration (if `open` exists) or raw stream consumption (if only `read` exists). No internal callbacks or file I/O policies are imposed.

### Advantages
* Extremely small public API.
* Easy embedding in other programs (GUIs, scripting hosts, FUSE, etc.).
* Early abort = simply stop calling `read()` and `close()` the layer.

## 5. Implemented Layer Factories

| Layer | Purpose | Capabilities |
|-------|---------|-------------|
| File / Memory | Base sources | `open` + `read` (single data fork, no metadata) |
| HQX | BinHex 4.0 decoder | Transform + fork iteration (data then resource) |
| BIN | MacBinary II / II+ | Hybrid (transformation + iteration) |
| SIT (classic) | StuffIt classic variants | Archive iteration (loads archive in memory) |
| SIT5 | StuffIt 5.x | Archive iteration (loads archive in memory) |
| CPT | Compact Pro | Archive iteration + on‑demand decompression |

Factories *must not* irreversibly consume input during probing.

## 6. Layer Implementation Notes

*Base Layers* allocate small structs tracking stream position & an `opened` flag to enforce correct ordering (`open` before `read`).

*Transformers* (e.g. HQX) maintain decode state machines (RLE, 6‑bit unpacking, CRC) and expose both forks sequentially.

*Archive Layers* (SIT/SIT5/CPT) currently copy archive bytes into memory to enable directory parsing and per‑file fork descriptors; streaming decompression (LZW, RLE90, SIT13, SIT15, LZH) is performed on demand as forks are read.

## 7. Consuming a Layer (Pattern)

Typical extraction loop (error handling elided):

```c
munbox_layer_t *L = munbox_new_file_layer("SomeArchive.sit.hqx");
L = munbox_process_new(L);              /* auto-detect chain */
if (L->open) {
    munbox_file_info_t info;            /* iteration over files & forks */
    int rc = L->open(L, MUNBOX_OPEN_FIRST, &info);
    char current_name[256] = ""; FILE *out = NULL;
    while (rc == 1) {
        if (strcmp(current_name, info.filename) != 0) { /* new file */
            if (out) fclose(out);
            strncpy(current_name, info.filename, sizeof current_name - 1);
            out = fopen(current_name, "wb");
        }
        uint8_t buf[65536];
        for (;;) { /* read this fork */
            ssize_t n = L->read(L, buf, sizeof buf);
            if (n < 0) { fprintf(stderr, "%s\n", munbox_last_error()); goto done; }
            if (n == 0) break; /* fork complete */
            if (info.fork_type == MUNBOX_FORK_DATA) fwrite(buf, 1, (size_t)n, out);
            /* else: resource fork – application decides how to store */
        }
        rc = L->open(L, MUNBOX_OPEN_NEXT, &info);
    }
    if (out) fclose(out);
} else if (L->read) {                   /* raw single stream */
    FILE *out = fopen("output.bin", "wb");
    uint8_t buf[65536]; ssize_t n;
    while ((n = L->read(L, buf, sizeof buf)) > 0) fwrite(buf, 1, (size_t)n, out);
    fclose(out);
}
done:
L->close(L);
```

The CLI in `cmd/main.c` adds AppleDouble metadata emission and directory creation on top of this pattern.

## 8. Metadata

Each successful `open()` populates a `munbox_file_info_t` describing the current fork: filename (relative path inside archive), Finder type/creator, Finder flags, fork length (if known) and which fork is active. Not all layers can determine lengths in advance; treat `length` as advisory.

### Extraction Flow
1. Layer parses header(s) & stores metadata internally.
2. `open(FIRST)` copies metadata into caller buffer and sets `fork_type` & `length` for the data fork.
3. After data fork EOF, caller invokes `open(NEXT)`; if a resource fork exists it becomes current and `fork_type` is updated.
4. When no more forks/files remain `open(NEXT)` returns 0.

## 9. Error Handling

`munbox_error(fmt, ...)` records a thread‑local descriptive message and returns `MUNBOX_ERROR` (-1). `munbox_last_error()` retrieves the last message. Propagation rules:
* Layers propagate `MUNBOX_ERROR` from their input unchanged.
* Applications stop processing on negative return values and query the message.

`MUNBOX_ABORT` (-2) is reserved for user‑initiated early termination (currently unused in the public interface but retained for future callback/interrupt scenarios).

## 10. Aborts

Errors propagate bottom‑up; once a layer begins producing a fork it either completes or reports an error—there is no mid‑fork restart facility.

## 11. Project Layout

```text
include/munbox.h        Public API
lib/munbox.c            Core: error system, base layers, detection loop
lib/layers/hqx.c        BinHex decoder
lib/layers/bin.c        MacBinary layer
lib/layers/sit.c        StuffIt classic + SIT5 (shared state & logic)
lib/layers/sit13.c/.h   SIT13 decompressor
lib/layers/sit15.c/.h   SIT15 decompressor
lib/layers/cpt.c        Compact Pro layer (LZH, RLE logic)
cmd/main.c              CLI (argument parsing, AppleDouble emitter)
test/ & expanded/       Sample archives & extracted outputs
build/                  CMake build tree
docs/                   Architectural & format documentation
```

Only **CMake** is required for building the library & CLI. A convenience `Makefile` may exist for streamlined workflows.

## 12. Shell Test Harness

`test/run_tests.sh` provides a lightweight regression harness using only POSIX shell + `md5sum`. It iterates test cases, extracts with the CLI, and verifies checksums.

### Features
* Zero external language dependencies.
* Deterministic sequential execution & colored summary.
* Options: `--verbose`, `--keep-files`, path overrides for executable & test dir.

### Exit Codes
* `0` when all test cases pass.
* `1` if any failure occurs.

---

This document reflects the current (callback‑free) unified architecture. Contributors adding new formats should implement a probing factory, supply iteration or transformation semantics via the layer interface, and update this file accordingly.


## 8. File Metadata System
