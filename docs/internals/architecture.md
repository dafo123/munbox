
# Architecture Description

## 1. Introduction

**munbox** is a modern, portable, and modular **C** library **and** companion CLI application for unpacking classic Macintosh archive formats such as **StuffIt** (`.sit`), **BinHex** (`.hqx`), and **Compact Pro** (`.cpt`).  
The design uses a **unified layer architecture**: each processing stage is a *layer* that can optionally provide stream transformation and/or file iteration via `open(FIRST/NEXT)` + `read()`. This yields constant‑memory streaming, natural back‑pressure, and easy composability without artificial distinctions between "transformers" and "extractors".

## 2. Core Design Principles

### 2.1. Consumer‑Driven *Pull* Model
All processing is initiated by the final consumer (layer). Each stage in the pipeline produces data **only when the next stage requests it**, providing implicit back‑pressure and ensuring constant memory usage regardless of archive size.

### 2.2. Unified Layer Interface
Every processing stage uses the same interface: **`munbox_layer_t`**. Layers in munbox can optionally provide multiple capabilities:
- **Stream transformation** via `read()`
- **File iteration** via `open(FIRST/NEXT)` and `read()` per fork  
- **Metadata returned via `open(FIRST/NEXT)`**

### 2.3. Non‑consuming identification via open()+read
To decide which handler should process a stream, handlers call `open(MUNBOX_OPEN_FIRST)` to rewind, read a small signature from the start, then call `open(MUNBOX_OPEN_FIRST)` again to reset for real processing. Base layers implement `open()` to rewind safely.

---

## 3. The Unified Layer Abstraction

```c
typedef enum {
    MUNBOX_OPEN_FIRST = 0,
    MUNBOX_OPEN_NEXT  = 1
} munbox_open_t;

typedef enum {
    MUNBOX_FORK_DATA = 0,
    MUNBOX_FORK_RESOURCE = 1
} munbox_fork_t;

typedef struct munbox_file_info {
    char filename[256];
    uint32_t type;
    uint32_t creator;
    uint16_t finder_flags;
    uint32_t length; // size of the currently opened fork
    int /* munbox_fork_t */ fork_type; /* which fork is currently opened */
    bool has_metadata;
} munbox_file_info_t;

typedef struct munbox_layer {
    ssize_t (*read)(struct munbox_layer *self, void *buf, size_t cnt);
    void (*close)(struct munbox_layer *self);
    int (*open)(struct munbox_layer *self, munbox_open_t what, munbox_file_info_t *info);
    void *internal_state;
} munbox_layer_t;
```

### 3.1. Layer Capabilities
Each layer can implement one or more capabilities:

- **Stream Provider**: Implements `read()` to provide transformed byte streams
- **File Iterator**: Implements `open(FIRST/NEXT)` + `read()` to iterate files and forks (metadata is provided via `open()`)

### 3.2. Design Benefits
- **Conceptual Simplicity**: One layer type instead of separate transformers/extractors
- **Flexibility**: Layers can both transform streams AND extract files
- **Composability**: Natural chaining without artificial boundaries
- **Future-Proof**: Handles complex formats (nested archives, multi-stage processing)

**Key Design Decisions:**
- **Naming:** The parameter to `read()` is called **`self`** to emphasise that it refers to *this* layer, **not** the underlying input layer.
- **Ownership:** The constructor that allocates a layer is responsible for initialising `internal_state`; the layer's own `close()` implementation must free it.
- **Thread model:** All layers **must be confined to a single thread**—no internal mutexes or atomic operations are required. Up‑calls into user callbacks happen on that same thread.
- **Random access:** **Not a goal.** Layers are strictly forward‑only; formats that require seeking are out of scope.

**Critical Implementation Detail:** Layer constructors (like `munbox_new_hqx_layer`) must **NOT** consume data during construction. Use `open(FIRST)+read` to probe and immediately reset with `open(FIRST)`. Data consumption must happen only during `read()` or iteration via `open/read` to avoid circular dependencies in the layer stack.

---

## 4. Unified Processing Pipeline

The core processing function `munbox_process_stream()` implements a simplified unified processing pipeline:

```c
int munbox_process(munbox_layer_t *initial_layer, 
                   const munbox_extract_callbacks_t *callbacks);
```

### 4.1. Processing Algorithm
The unified approach eliminates the artificial separation between transformation and extraction phases:

1. **Layer Factory Loop**: Try each registered layer factory in sequence
2. **Format Recognition**: Each factory uses open()+read to identify compatible formats  
3. **Capability Check**: If a layer is created, check its capabilities:
    - **File Iterator**: If `open()` is available, iterate entries and forks and stream via callbacks
    - **Stream Transformer**: If only `read()` is available, chain the layer and continue
4. **Fallback**: If no layer provides iteration, output the final transformed stream as a generic file

### 4.2. Benefits of Unified Pipeline
- **Simpler Logic**: Single loop instead of separate transformation and extraction phases
- **Natural Chaining**: Transformations chain automatically when layers provide `read()` capability
- **Direct Extraction**: Archive formats can extract directly without requiring stream transformation
- **Graceful Fallback**: Always produces output, even when format support is incomplete

### 4.3. Layer Factory Registry
The system uses a simplified registry of layer factories:

```c
typedef struct {
    const char* name;
    munbox_layer_t* (*layer_factory)(munbox_layer_t *input);
} munbox_format_handler_t;
```

This replaces the previous dual-type system with a single factory function signature for all formats.

---

## 5. Implementing Layers

### 5.1. Transformer Layers
A *transformer* converts one byte‑stream into another (e.g. BinHex → raw). No registry structure is needed—each transformer is exposed by a single factory function that wraps an input layer:

```c
/* Returns a new layer that decodes BinHex‑encoded data, or NULL if header not recognised. */
munbox_layer_t *munbox_new_hqx_layer(munbox_layer_t *input);
```

If the function cannot recognise the format after peeking, it returns **`NULL`** so the caller can try the next transformer.

**Implementation Note:** The BinHex transformer (`munbox_new_hqx_layer`) is currently the only implemented transformer. It recognizes BinHex 4.0 format by looking for the characteristic "(This file" header pattern and validates the format during construction. The BinHex layer also extracts and provides file metadata including filename, type, creator, and Finder flags.

### 5.2. Archive Layers
An archive layer interprets its input as an archive and exposes files/forks via `open(FIRST/NEXT)` and `read()`. The CLI (and any caller) uses callbacks to receive data and resource forks, grouping forks by filename.

---

## 6. Layer Implementation Details

**Base Layers:**
- `munbox_new_file_layer()`: Reads from filesystem using FILE*
- `munbox_new_mem_layer()`: Reads from memory buffer (non-owning)
- `open(MUNBOX_OPEN_*)` on base layers: Provides rewind for non-consuming probes

**Format Layers:**
- `munbox_new_hqx_layer()`: BinHex 4.0 decoder (stream transformer)
  - Recognizes "(This file" header pattern
  - Decodes 6-bit to 8-bit transformation
  - Validates CRC checksums
  - Extracts file metadata (filename, type, creator, Finder flags)
  - Provides `read()` capability for stream transformation
  - Currently the only implemented format layer

**Internal State Structures:**
Each layer type maintains its own state structure in `internal_state`:
- `file_layer_state_t`: FILE* handle and EOF tracking
- `mem_layer_state_t`: Buffer pointer, size, and position
- `peek_layer_state_t`: Source layer, buffer, capacity, fill level, and position
- `hqx_layer_state_t`: Decoder state, buffers, CRC validation, and file metadata

**Performance note:** Even though layers may add peek wrappers, real‑world pipelines rarely exceed **two layers**, so the additional buffer copy is negligible versus I/O cost.

---

## 7. Output Callback Interface

The output interface provides callbacks for file creation and resource forks:

```c
typedef struct munbox_extract_callbacks {
    int (*new_file)(const char *path, const munbox_file_info_t *file_info, void **out_user_data);
    int (*write_data)(void *user_data, const void *buf, size_t cnt);
    int (*write_resource_fork)(void *user_data, const void *rsrc_data, size_t rsrc_size);
    int (*end_file)(void *user_data);
} munbox_extract_callbacks_t;
```

**Callback Flow:**
1. `new_file()` is called with the file path and metadata, returns user_data handle
2. As each fork is opened, the caller writes the entire fork: use `write_data()` for data fork bytes and `write_resource_fork()` for resource fork bytes
3. `end_file()` is called to finalize the file after all forks

**File Metadata Parameter:**
The `file_info` parameter to `new_file()` contains extracted metadata when available:
- `filename`: Original filename from the archive
- `type`/`creator`: Mac file type and creator codes (4-byte values)
- `finder_flags`: Mac Finder flags
- `length`: Size (in bytes) of the currently opened fork (data or resource)
- `has_metadata`: Boolean indicating if metadata is valid

**CLI Implementation:** The command-line tool implements these callbacks to write files to the filesystem, with support for directory creation and optional verbose output that displays file metadata.

---

## 8. File Metadata System

The munbox library includes a comprehensive file metadata system designed to preserve classic Macintosh file attributes through the processing pipeline.

### 8.1. Metadata Structure
```c
typedef struct munbox_file_info {
    char filename[256];       /**< Original filename (null-terminated) */
    uint32_t type;           /**< Mac file type (e.g., 'TEXT', 'APPL') */
    uint32_t creator;        /**< Mac creator code (e.g., 'MSWD', 'ttxt') */
    uint16_t finder_flags;   /**< Mac Finder flags */
    uint32_t length;         /**< Length of the currently opened fork in bytes */
    bool has_metadata;       /**< True if metadata fields are valid */
} munbox_file_info_t;
```

### 8.2. Metadata Flow
1. **Extraction**: Transformer and archive layers extract metadata from format headers
2. **Propagation**: Metadata is stored in layer state and returned by `open(FIRST/NEXT)`
3. **Utilization**: Processing pipeline uses the `info` populated by `open()` and passes it to output callbacks
4. **Preservation**: CLI tools can use metadata for proper filename handling and verbose output

### 8.3. Utility Functions
- `munbox_fourcc_from_string()`: Convert 4-character codes to uint32_t
- `munbox_fourcc_to_string()`: Convert uint32_t codes to readable strings

### 8.4. Current Implementation
- **BinHex (.hqx)**: Fully extracts filename, type, creator, flags, and fork sizes
- **Base layers**: File and memory layers do not provide metadata
- **Fork Type**: When iterating, `munbox_file_info_t.fork_type` indicates whether the current stream is data or resource fork
- **Fallback behavior**: Uses original filename when available, falls back to "untitled"

---

## 9. Error Handling

munbox uses a **single generic error code** plus a *per‑thread* free‑text message buffer:

```c
#define MUNBOX_ERROR  (-1)   /* Generic failure */
#define MUNBOX_ABORT  (-2)   /* Propagated user abort */

/* Returns a human‑readable description of the most recent error on this thread. */
const char *munbox_last_error(void);

/* Helper to set the thread‑local error string and return MUNBOX_ERROR. */
int munbox_error(const char *fmt, ...) __attribute__((format(printf,1,2)));
```

Rules:
- Layers **must** call `munbox_error()` to record a descriptive message **before** returning `MUNBOX_ERROR`.
- When a layer detects that its *input* returned `MUNBOX_ERROR`, it should propagate the same code unchanged (bottom‑up aborts only).
- Callbacks may abort extraction early by returning non‑zero; the extractor translates this into `MUNBOX_ABORT` which higher layers propagate untouched.

**Thread-Local Error Storage:** Error messages are stored in thread-local storage, making the library thread-safe for concurrent processing of different archives on different threads.

---

## 10. Abortion Semantics

Aborts always propagate **bottom‑up**:

1. A low‑level layer (or the extractor) detects corruption and calls `munbox_error()` ➜ returns `MUNBOX_ERROR`.
2. Each upstream layer immediately returns the same code without alteration.
3. Application receives the error and prints `munbox_last_error()`.

The inverse (top‑down abort) is **not** supported; once a layer starts producing output it must either succeed or fail.

---

## 11. Build System and Project Structure

```text
/* Command‑line tool */
cmd/
│── main.c
└── CMakeLists.txt

/* Library implementation */
lib/
├── munbox.c              # Core processing pipeline
├── defaults.c            # Format handler registry initialization
├── stream.c              # Base layer implementations (file, memory, peek)
├── munbox_internal.h     # Internal definitions and state structures
    └── layers/
        ├── hqx.c            # BinHex transformer (implemented)
        ├── sit.c            # StuffIt extractor (commented out)
        └── cpt.c            # Compact Pro extractor (planned)

/* Public header(s) */
include/
└── munbox.h             # Public API definitions

/* Legacy standalone implementation */
app/                     # Old implementation, kept for reference
main.c                   # Legacy main with integrated format support
Makefile                 # Traditional make build system

/* Tests and sample archives */
testfiles/               # Extensive collection of test archives
├── *.sit.hqx           # BinHex-encoded StuffIt archives
├── *.img.sit           # Disk image archives  
└── *.hqx               # Various BinHex test files

/* Build output */
build/                   # CMake build directory
├── cmd/munbox          # Command-line executable
└── lib/libmunbox.a     # Static library
```

**Build Systems:**
- **CMake** (primary): Modern build system for library + CLI
- **Traditional Makefile**: Legacy build for standalone app
- Both systems support the codebase but serve different purposes

**Key Files:**
- `lib/munbox.c`: Contains the main `munbox_process_stream()` function
- `lib/defaults.c`: Initializes the format handler registry
- `lib/stream.c`: Implements base layers (file, memory, peek)
- `lib/layers/hqx.c`: Complete BinHex transformer implementation
- `cmd/main.c`: CLI tool with filesystem callbacks and argument processing

- **cmd/** builds the `munbox` CLI by linking against the library in **lib/**.
- **lib/** contains the core implementation and format‑specific layers under **lib/layers/**.
- **include/munbox.h** is the *only* public header; all other headers are private.
- **docs/** holds architectural and format documentation; contributors should update these when adding new formats.
- **testfiles/** contains extensive real-world test archives for validation.

---

## 13. Simplified Shell-Based Test System

### 13.1. Overview

A new, minimal shell script test runner (`run_tests.sh`) replaces the previous Python-based system. It is designed for simplicity, reliability, and ease of use, requiring only standard Unix tools (`bash`, `md5sum`) and the `munbox` CLI executable.

### 13.2. Features

- No Python dependency
- No parallelization or filtering complexity
- Runs all test cases sequentially
- Colored output and summary
- Configurable via command-line options
- Uses standard `md5sum -c` for checksum validation

### 13.3. Usage

```sh
./test/run_tests.sh [options]
```

**Options:**

- `--test-dir <dir>`: Directory containing test cases (default: `testfiles`)
- `--munbox <path>`: Path to munbox executable (default: `../build/cmd/munbox`)
- `--output-dir <dir>`: Temporary directory for test outputs (default: `munbox_test`)
- `--verbose`: Enable verbose output
- `--keep-files`: Keep extracted files after testing (for debugging)
- `--help`: Show help message

**Example:**

```sh
./test/run_tests.sh --verbose --keep-files
```

### 13.4. Test Case Structure

Each test case is a subdirectory under the test directory (default: `testfiles/`) containing:

- `testfile.*` (input archive)
- `md5sums.txt` (expected checksums)

### 13.5. Output

- Green **PASS** for successful tests, red **FAIL** for failures
- Summary of total, passed, failed, and success rate
- Verbose mode shows commands and details for debugging

### 13.6. Error Handling

- Script continues running all tests even if some fail
- Proper exit codes: `0` if all pass, `1` if any fail
- Clear error messages for missing files or executables

