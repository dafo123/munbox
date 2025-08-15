// SPDX-License-Identifier: MIT
/**
 *
 * MIT License
 *
 * Copyright (c) dafo123
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// sit.c
// StuffIt (.sit) format layer implementation for munbox.

#include "sit.h"
#include "munbox.h"
#include "munbox_internal.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Local helper: safely join parent path and name into destination buffer (always NUL terminates).
static void sit_join_path(char *dst, size_t dst_cap, const char *parent, const char *name) {
    if (!dst_cap) return;
    dst[0] = '\0';
    if (parent && parent[0]) {
        size_t pos = 0;
        size_t lp = strnlen(parent, dst_cap - 1);
        if (lp) {
            memcpy(dst, parent, lp);
            pos = lp;
        }
        if (pos < dst_cap - 1) {
            dst[pos++] = '/';
        }
        if (name && pos < dst_cap - 1) {
            size_t rem = dst_cap - 1 - pos;
            size_t ln = strnlen(name, rem);
            if (ln) memcpy(dst + pos, name, ln);
            pos += ln;
        }
        dst[pos < dst_cap ? pos : (dst_cap - 1)] = '\0';
    } else if (name) {
        size_t ln = strnlen(name, dst_cap - 1);
        memcpy(dst, name, ln);
        dst[ln] = '\0';
    }
}

// Forward declarations for LZW (method 2) streaming helpers
struct lzw_ctx;
// Initialize an LZW streaming context over a compressed buffer.
static struct lzw_ctx *lzw_init(const uint8_t *src, size_t src_len);
// Read up to `cap` bytes from the LZW context into `out`.
static ssize_t lzw_read(struct lzw_ctx *c, uint8_t *out, size_t cap);
// Free an LZW streaming context and its resources.
static void lzw_free(struct lzw_ctx *c);

// --- Utility Macros ---

#define LOAD_BE16(p) (((uint16_t)((uint8_t *)(p))[0] << 8) | ((uint16_t)((uint8_t *)(p))[1]))

#define LOAD_BE32(p)                                                                                                   \
    (((uint32_t)((uint8_t *)(p))[0] << 24) | ((uint32_t)((uint8_t *)(p))[1] << 16) |                                   \
     ((uint32_t)((uint8_t *)(p))[2] << 8) | ((uint32_t)((uint8_t *)(p))[3]))

// --- External Decompression Functions ---

// sit13 and sit15 are implemented in separate files

// --- SIT Layer State ---

// Define streaming kinds and state before the layer state (complete types needed)
// Represents the different decompression methods supported in SIT archives
enum sit_stream_kind { STRM_NONE = 0, STRM_COPY = 1, STRM_RLE90 = 2, STRM_SIT15 = 3, STRM_LZW = 4, STRM_SIT13 = 5 };
typedef enum sit_stream_kind sit_stream_kind_t;

// Holds state for streaming decompression of a single SIT file entry
typedef struct sit_stream_state {
    // common
    sit_stream_kind_t kind;
    const uint8_t *src;
    size_t src_len;
    size_t src_pos;
    size_t out_rem; // uncompressed bytes remaining to produce
    bool skip_crc;
    uint16_t crc_accum;
    // rle90 state
    uint8_t last_byte;
    size_t rep_rem; // remaining repeat count for last_byte
    // LZW state (method 2)
    struct lzw_ctx *lzw;
    // SIT13 state
    sit13_ctx_t *sit13;
} sit_stream_state_t;

typedef struct {
    // Descriptor for a fork: uncompressed/comp lengths, crc, method and pointer
    uint32_t uncomp_len;
    uint32_t comp_len;
    uint16_t crc;
    uint8_t method;
    const uint8_t *comp_ptr; // points into archive_data
} sit_fork_desc_t;

// Index entry describing a single file (path, metadata, and fork descriptors)
typedef struct {
    char path[512];
    uint32_t type;
    uint32_t creator;
    uint16_t finder_flags;
    sit_fork_desc_t data;
    sit_fork_desc_t rsrc;
} sit_index_entry_t;
typedef struct {
    munbox_layer_t *source;
    uint8_t *archive_data;
    size_t archive_size;
    bool is_sit5;

    // Indexed entries for iteration
    sit_index_entry_t *entries;
    size_t entry_count;

    // Iteration and streaming state
    size_t iter_entry;
    int iter_fork; // 0=data, 1=rsrc
    munbox_file_info_t cur_info;

    // Streaming fields
    sit_stream_kind_t cur_stream_kind; // current mode
    sit_stream_state_t stream; // streaming state for current fork
    uint16_t expected_crc; // expected CRC for current fork
    sit15_ctx_t *sit15_ctx; // SIT15 streaming context, if used
    sit13_ctx_t *sit13_ctx; // SIT13 streaming context, if used
    struct lzw_ctx *lzw_ctx; // LZW streaming context, if used
    bool opened; // require open() before read()
} sit_layer_state_t;

// Debug helper: enable verbose SIT logs if env var is set
// Returns true if SIT debug logging is enabled via MUNBOX_DEBUG_SIT env var
static bool sit_debug_enabled(void) {
    static int inited = 0;
    static bool enabled = false;
    if (!inited) {
        const char *v = getenv("MUNBOX_DEBUG_SIT");
        enabled = (v && *v && strcmp(v, "0") != 0);
        inited = 1;
    }
    return enabled;
}

// Forward declarations for vtable wiring: open/read implementations
static int sit_layer_open(munbox_layer_t *self, munbox_open_t what, munbox_file_info_t *info);
// Read uncompressed bytes from the current fork
static ssize_t sit_layer_read(munbox_layer_t *self, void *buf, size_t cnt);

// --- CRC Calculation ---
// Reflected CRC-16 table (poly 0x8005) shared by all CRC routines in this file
static const uint16_t sit_crc_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, 0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1,
    0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40, 0x0A00, 0xCAC1, 0xCB81, 0x0B40,
    0xC901, 0x09C0, 0x0880, 0xC841, 0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1,
    0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41, 0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1,
    0xF281, 0x3240, 0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441, 0x3C00, 0xFCC1, 0xFD81, 0x3D40,
    0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840, 0x2800, 0xE8C1,
    0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41, 0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640, 0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0,
    0x2080, 0xE041, 0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740,
    0xA501, 0x65C0, 0x6480, 0xA441, 0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41, 0xAA01, 0x6AC0,
    0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40, 0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1,
    0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041, 0x5000, 0x90C1, 0x9181, 0x5140,
    0x9301, 0x53C0, 0x5280, 0x9241, 0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0,
    0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40, 0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0,
    0x4C80, 0x8C41, 0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641, 0x8201, 0x42C0, 0x4380, 0x8341,
    0x4100, 0x81C1, 0x8081, 0x4040,
};

// Updates a CRC-16 (poly 0x8005 reflected) with the provided buffer
static uint16_t sit_crc_process(uint16_t crc, const uint8_t *buffer, size_t length) {
    while (length--) {
        crc = sit_crc_table[(crc ^ *buffer++) & 0xff] ^ (crc >> 8);
    }
    return crc;
}

// Computes CRC-16 over a full buffer starting from zero
static uint16_t sit_crc(const uint8_t *buffer, size_t length) { return sit_crc_process(0, buffer, length); }

// Incrementally updates an existing CRC-16 with new data
static uint16_t sit_crc_update(uint16_t crc, const uint8_t *buffer, size_t length) {
    return sit_crc_process(crc, buffer, length);
}

// (Legacy extraction code removed; layer uses open()/read())

// --- Layer Implementation ---

// SIT layer does not support peeking.

// Close an open SIT layer and free all associated resources.
// Frees internal contexts, buffers, and the layer object.
static void sit_layer_close(munbox_layer_t *self) {
    if (!self)
        return;
    sit_layer_state_t *state = (sit_layer_state_t *)self->internal_state;
    if (state) {
        if (state->source)
            state->source->close(state->source);
        free(state->archive_data);
        free(state->entries);
        if (state->sit15_ctx) {
            sit15_free(state->sit15_ctx);
            state->sit15_ctx = NULL;
        }
        if (state->sit13_ctx) {
            sit13_free(state->sit13_ctx);
            state->sit13_ctx = NULL;
        }
        if (state->lzw_ctx) {
            lzw_free(state->lzw_ctx);
            state->lzw_ctx = NULL;
        }
        free(state);
    }
    free(self);
}

// --- Extended Layer Functions ---

// (Legacy extraction code removed)

/* static int sit_extract(munbox_layer_t *self, const munbox_extract_callbacks_t *callbacks) { return MUNBOX_ERROR; } */

// Create a new SIT layer that scans the input for an embedded SIT archive and
// prepares the layer for open()/read() iteration (archive kept in memory).
munbox_layer_t *munbox_new_sit_layer(munbox_layer_t *input) {
    if (!input)
        return NULL;

    bool is_classic_sit = false;
    bool is_sit5 = false;

    // Read each fork fully into memory and search for a SIT header anywhere in the fork
    // (SEA payloads often embed the archive at a non-zero offset). If found, trim the
    // prefix and keep only the archive bytes for extraction.
    uint8_t *archive_data = NULL;
    size_t archive_size = 0;
    size_t archive_capacity = 0;

    if (input->open == NULL)
        return NULL;

    munbox_file_info_t info;
    int rc = input->open(input, MUNBOX_OPEN_FIRST, &info);
    while (rc == 1) {
        // Read the entire current fork into a temporary buffer
        size_t fork_cap = 1024 * 1024; // 1 MiB to start
        uint8_t *fork_buf = (uint8_t *)malloc(fork_cap);
        if (!fork_buf) {
            munbox_error("Out of memory");
            return NULL;
        }
        size_t fork_size = 0;
        ssize_t bytes_read;
        while ((bytes_read = input->read(input, fork_buf + fork_size, fork_cap - fork_size)) > 0) {
            fork_size += (size_t)bytes_read;
            if (fork_size == fork_cap) {
                size_t new_cap = fork_cap * 2;
                uint8_t *tmp = (uint8_t *)realloc(fork_buf, new_cap);
                if (!tmp) {
                    free(fork_buf);
                    munbox_error("Out of memory");
                    return NULL;
                }
                fork_buf = tmp;
                fork_cap = new_cap;
            }
        }
        if (bytes_read < 0) {
            free(fork_buf);
            return NULL;
        }

        // Fork scanned for SIT header

        // Search for SIT5 magic anywhere in the fork
        size_t match_off = (size_t)-1;
        bool match_is_sit5 = false;
        if (fork_size >= 80) {
            for (size_t i = 0; i + 80 <= fork_size; ++i) {
                if (memcmp(fork_buf + i, "StuffIt (c)1997-", 16) == 0 && i + 78 < fork_size &&
                    memcmp(fork_buf + i + 20, " Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/", 58) == 0) {
                    match_off = i;
                    match_is_sit5 = true;
                    break;
                }
            }
        }
        // If not SIT5, search for classic header anywhere (needs 14 bytes window)
        if (match_off == (size_t)-1 && fork_size >= 14) {
            const char *magic1[] = {"SIT!", "ST46", "ST50", "ST60", "ST65", "STin", "STi2", "STi3", "STi4"};
            for (size_t i = 0; i + 14 <= fork_size; ++i) {
                for (int m = 0; m < 9; ++m) {
                    if (memcmp(fork_buf + i, magic1[m], 4) == 0 && memcmp(fork_buf + i + 10, "rLau", 4) == 0) {
                        match_off = i;
                        match_is_sit5 = false;
                        break;
                    }
                }
                if (match_off != (size_t)-1)
                    break;
            }
        }

        if (match_off != (size_t)-1) {
            // Found a match in this fork; keep only the archive bytes starting at match
            is_sit5 = match_is_sit5;
            is_classic_sit = !match_is_sit5;
            archive_size = fork_size - match_off;
            archive_capacity = archive_size;
            archive_data = (uint8_t *)malloc(archive_capacity);
            if (!archive_data) {
                free(fork_buf);
                munbox_error("Out of memory");
                return NULL;
            }
            memcpy(archive_data, fork_buf + match_off, archive_size);
            free(fork_buf);
            break; // Done, we have the archive
        }

        // Not a match in this fork; free and move to next fork
        free(fork_buf);
        rc = input->open(input, MUNBOX_OPEN_NEXT, &info);
    }

    if (!is_classic_sit && !is_sit5) {
        return NULL;
        // Fallback without peek: restart from beginning, read a small header, then full stream if needed
    }

    // Create layer
    munbox_layer_t *layer = malloc(sizeof(munbox_layer_t));
    sit_layer_state_t *state = calloc(1, sizeof(sit_layer_state_t));

    if (!layer || !state) {
        free(layer);
        free(state);
        free(archive_data);
        munbox_error("Out of memory");
        return NULL;
    }

    state->source = input;
    state->archive_data = archive_data;
    state->archive_size = archive_size;
    state->is_sit5 = is_sit5;

    layer->internal_state = state;
    layer->read = NULL; // will be enabled after we build index and open()
    layer->close = sit_layer_close;
    layer->open = sit_layer_open;
    // no extract; use open/read

    return layer;
}

// --- Index building for open()/read iteration ---

// Build index entries for classic SIT archives by parsing per-file headers.
// Populates st->entries with parsed file metadata and fork descriptors.
static int sit_build_index_classic(sit_layer_state_t *st) {
    uint8_t *data = st->archive_data;
    if (st->archive_size < 22)
        return munbox_error("SIT: archive too small");
    uint32_t numFiles = LOAD_BE16(data + 4);
    uint8_t *current = data + 22;

    // conservative upper bound allocation
    size_t cap = numFiles ? numFiles : 16;
    st->entries = (sit_index_entry_t *)calloc(cap, sizeof(sit_index_entry_t));
    if (!st->entries)
        return munbox_error("Out of memory");

    // Stack for folder paths
    char folder_stack[10][256];
    int folder_depth = 0;

    for (uint32_t i = 0; i < numFiles; i++) {
        if ((size_t)(current - data) + 112 > st->archive_size)
            return munbox_error("SIT: header beyond archive");
        uint8_t *header = current;
        uint8_t res_method = header[0];
        uint8_t data_method = header[1];

        // Folder start
        if (res_method == 32 || data_method == 32) {
            uint8_t name_len = header[2];
            char folder_name[64];
            memcpy(folder_name, header + 3, name_len);
            folder_name[name_len] = '\0';
            if (folder_depth < 10) {
                strncpy(folder_stack[folder_depth], folder_name, sizeof(folder_stack[folder_depth]) - 1);
                folder_stack[folder_depth][sizeof(folder_stack[folder_depth]) - 1] = '\0';
                folder_depth++;
            }
            current = header + 112;
            continue;
        }
        // Folder end
        if (res_method == 33 || data_method == 33) {
            if (folder_depth > 0)
                folder_depth--;
            current = header + 112;
            continue;
        }
        if ((res_method & 0xE0) || (data_method & 0xE0)) {
            // skip unknown folder markers
            current = header + 112;
            continue;
        }

        uint8_t name_len = header[2];
        char filename[64];
        memcpy(filename, header + 3, name_len);
        filename[name_len] = '\0';

        char full_filename[256];
        if (folder_depth > 0) {
            strcpy(full_filename, folder_stack[0]);
            for (int d = 1; d < folder_depth; d++) {
                strcat(full_filename, "/");
                strcat(full_filename, folder_stack[d]);
            }
            strcat(full_filename, "/");
            strcat(full_filename, filename);
        } else {
            strncpy(full_filename, filename, sizeof(full_filename) - 1);
            full_filename[sizeof(full_filename) - 1] = '\0';
        }

        uint32_t rsrc_len = LOAD_BE32(header + 84);
        uint32_t data_len = LOAD_BE32(header + 88);
        uint32_t rsrc_comp_len = LOAD_BE32(header + 92);
        uint32_t data_comp_len = LOAD_BE32(header + 96);
        uint16_t rsrc_crc = LOAD_BE16(header + 100);
        uint16_t data_crc = LOAD_BE16(header + 102);
        uint32_t type = LOAD_BE32(header + 66);
        uint32_t creator = LOAD_BE32(header + 70);
        uint16_t finder = LOAD_BE16(header + 74);

        uint8_t *comp_rsrc = header + 112;
        if ((size_t)(comp_rsrc - data) + rsrc_comp_len > st->archive_size)
            return munbox_error("SIT: rsrc fork out of range");
        uint8_t *comp_data = comp_rsrc + rsrc_comp_len;
        if ((size_t)(comp_data - data) + data_comp_len > st->archive_size)
            return munbox_error("SIT: data fork out of range");

        if (st->entry_count >= cap) {
            size_t ncap = cap * 2;
            if (ncap < 16)
                ncap = 16;
            void *nb = realloc(st->entries, ncap * sizeof(sit_index_entry_t));
            if (!nb)
                return munbox_error("Out of memory");
            st->entries = (sit_index_entry_t *)nb;
            cap = ncap;
        }
        sit_index_entry_t *e = &st->entries[st->entry_count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->path, full_filename, sizeof(e->path) - 1);
        e->type = type;
        e->creator = creator;
        e->finder_flags = finder;
        e->rsrc.uncomp_len = rsrc_len;
        e->rsrc.comp_len = rsrc_comp_len;
        e->rsrc.crc = rsrc_crc;
        e->rsrc.method = res_method & 0x0F;
        e->rsrc.comp_ptr = comp_rsrc;
        e->data.uncomp_len = data_len;
        e->data.comp_len = data_comp_len;
        e->data.crc = data_crc;
        e->data.method = data_method & 0x0F;
        e->data.comp_ptr = comp_data;

        current = comp_data + data_comp_len;
    }
    return 0;
}

// Build index entries for SIT5-style archives by walking SIT5 directory blocks.
// Validates header CRCs and constructs full paths for files and folders.
static int sit_build_index_sit5(sit_layer_state_t *st) {
    uint8_t *data = st->archive_data;
    if (st->archive_size < 100)
        return munbox_error("SIT5: archive too small");
    uint32_t num_entries = LOAD_BE16(data + 92);
    uint32_t cursor = LOAD_BE32(data + 94);

    size_t cap = num_entries ? num_entries : 16;
    st->entries = (sit_index_entry_t *)calloc(cap, sizeof(sit_index_entry_t));
    if (!st->entries)
        return munbox_error("Out of memory");

// directory map for building paths
#define MAX_DIRS 256
    struct {
        uint32_t offset;
        char path[512];
    } dir_map[MAX_DIRS];
    int dir_count = 0;

    for (uint32_t i = 0; i < num_entries; i++) {
        if (cursor == 0 || cursor >= st->archive_size)
            return munbox_error("SIT5: cursor out of range");
        uint32_t offs = cursor;
        uint8_t *header1 = data + offs;
        if (LOAD_BE32(header1) != 0xa5a5a5a5)
            return munbox_error("SIT5: invalid entry magic");
        if (header1[4] != 1)
            return munbox_error("SIT5: unsupported entry version");
        uint16_t header1_len = LOAD_BE16(header1 + 6);
        uint32_t header_end = offs + header1_len;
        // verify header CRC
        uint8_t *tmp = malloc(header1_len);
        if (!tmp)
            return munbox_error("Out of memory");
        memcpy(tmp, header1, header1_len);
        tmp[32] = tmp[33] = 0;
        if (sit_crc(tmp, header1_len) != LOAD_BE16(header1 + 32)) {
            free(tmp);
            return munbox_error("SIT5 header CRC mismatch");
        }
        free(tmp);

        uint8_t flags = header1[9];
        uint32_t diroffs = LOAD_BE32(header1 + 26);
        uint16_t namelen = LOAD_BE16(header1 + 30);
        uint32_t datalength = LOAD_BE32(header1 + 34);
        uint32_t datacomplen = LOAD_BE32(header1 + 38);
        uint16_t datacrc = LOAD_BE16(header1 + 42);
        char namebuf[256];
        size_t cpylen = namelen < sizeof(namebuf) - 1 ? namelen : sizeof(namebuf) - 1;
        memcpy(namebuf, header1 + 48, cpylen);
        namebuf[cpylen] = '\0';

        uint8_t *header2 = data + header_end;
        uint16_t something = LOAD_BE16(header2 + 0);
        (void)something;
        uint32_t filetype = LOAD_BE32(header2 + 4);
        uint32_t filecreator = LOAD_BE32(header2 + 8);
        uint16_t finderflags = LOAD_BE16(header2 + 12);
        uint32_t second_block_skip = (header1[4] == 1) ? 22 : 18;
        bool hasresource = (something & 0x01) != 0;
        uint32_t resourcelength = 0, resourcecomplen = 0;
        uint16_t resourcecrc = 0;
        uint8_t resourcemethod = 0;
        uint8_t res_passlen = 0;
        uint8_t *second_block_after_prefix = header2 + 14 + second_block_skip;
        uint8_t *datastart_ptr = second_block_after_prefix;
        if (hasresource) {
            resourcelength = LOAD_BE32(second_block_after_prefix + 0);
            resourcecomplen = LOAD_BE32(second_block_after_prefix + 4);
            resourcecrc = LOAD_BE16(second_block_after_prefix + 8);
            resourcemethod = *(second_block_after_prefix + 12);
            res_passlen = *(second_block_after_prefix + 13);
            datastart_ptr = second_block_after_prefix + 14 + res_passlen;
        }

        if (flags & 0x40) {
            uint16_t numfiles = LOAD_BE16(header1 + 46);
            if (datalength == 0xffffffff) {
                num_entries++;
                cursor = header_end;
                continue;
            }
            // parent path resolve
            char parent_path[512] = "";
            if (diroffs != 0) {
                for (int j = 0; j < dir_count; j++) {
                    if (dir_map[j].offset == diroffs) {
                        strncpy(parent_path, dir_map[j].path, sizeof(parent_path) - 1);
                        parent_path[sizeof(parent_path) - 1] = '\0';
                        break;
                    }
                }
            }
            char folder_path[512];
            sit_join_path(folder_path, sizeof(folder_path), parent_path, (const char *)namebuf);
            if (dir_count < MAX_DIRS) {
                dir_map[dir_count].offset = offs;
                strncpy(dir_map[dir_count].path, folder_path, sizeof(dir_map[dir_count].path) - 1);
                dir_map[dir_count].path[sizeof(dir_map[dir_count].path) - 1] = '\0';
                dir_count++;
            }
            num_entries += numfiles;
            cursor = (uint32_t)(datastart_ptr - data);
            continue;
        }

        if (datalength == 0xffffffff) {
            cursor = header_end;
            continue;
        }
        uint8_t datamethod = header1[46];
        uint8_t data_passlen = header1[47];
        if ((header1[9] & 0x20) && datalength && data_passlen) {
            return munbox_error("SIT5 encrypted entries are not supported");
        }
        char parent_path[512] = "";
        if (diroffs != 0) {
            for (int j = 0; j < dir_count; j++) {
                if (dir_map[j].offset == diroffs) {
                    strncpy(parent_path, dir_map[j].path, sizeof(parent_path) - 1);
                    parent_path[sizeof(parent_path) - 1] = '\0';
                    break;
                }
            }
        }
    char full_filename[512];
    sit_join_path(full_filename, sizeof(full_filename), parent_path, (const char *)namebuf);

        uint8_t *comp_rsrc = datastart_ptr;
        uint8_t *comp_data = datastart_ptr + (hasresource ? resourcecomplen : 0);
        if ((size_t)(comp_data - data) + datacomplen > st->archive_size)
            return munbox_error("SIT5: data fork out of range");

        if (st->entry_count >= cap) {
            size_t ncap = cap * 2;
            if (ncap < 16)
                ncap = 16;
            void *nb = realloc(st->entries, ncap * sizeof(sit_index_entry_t));
            if (!nb)
                return munbox_error("Out of memory");
            st->entries = (sit_index_entry_t *)nb;
            cap = ncap;
        }
        sit_index_entry_t *e = &st->entries[st->entry_count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->path, full_filename, sizeof(e->path) - 1);
        e->type = filetype;
        e->creator = filecreator;
        e->finder_flags = finderflags;
        if (hasresource) {
            e->rsrc.uncomp_len = resourcelength;
            e->rsrc.comp_len = resourcecomplen;
            e->rsrc.crc = resourcecrc;
            e->rsrc.method = resourcemethod & 0x0F;
            e->rsrc.comp_ptr = comp_rsrc;
        }
        e->data.uncomp_len = datalength;
        e->data.comp_len = datacomplen;
        e->data.crc = datacrc;
        e->data.method = datamethod & 0x0F;
        e->data.comp_ptr = comp_data;

        cursor = (uint32_t)((comp_data - data) + datacomplen);
    }
    return 0;
}

// (duplicate sit_crc_update removed; uses shared sit_crc_process above)

// --- LZW (method 2) streaming implementation ---

// Dictionary node used by the LZW streaming decompressor.
typedef struct dict_node {
    uint16_t parent;
    uint16_t length;
    uint8_t character;
    uint8_t root;
} dict_node_t;

// LZW streaming context for method 2 decompression (state, dictionary, buffers).
typedef struct lzw_ctx {
    const uint8_t *src;
    size_t src_len;
    size_t bit_offset;
    int symbol_size;
    int dict_size;
    int last_symbol; // -1 invalid
    int num_symbols_in_block;
    dict_node_t dict[1 << 14];
    uint8_t out_buf[1 << 14];
    size_t out_pos;
    size_t out_len;
} lzw_ctx;

// Initialize LZW streaming context and base dictionary for method 2.
static lzw_ctx *lzw_init(const uint8_t *src, size_t src_len) {
    lzw_ctx *c = (lzw_ctx *)calloc(1, sizeof(lzw_ctx));
    if (!c)
        return NULL;
    c->src = src;
    c->src_len = src_len;
    c->bit_offset = 0;
    c->symbol_size = 9;
    c->dict_size = 257;
    c->last_symbol = -1;
    c->num_symbols_in_block = 0;
    for (int i = 0; i < 256; i++) {
        c->dict[i].character = (uint8_t)i;
        c->dict[i].parent = UINT16_MAX;
        c->dict[i].root = (uint8_t)i;
        c->dict[i].length = 1;
    }
    return c;
}

// Peek up to 4 bytes safely from the source buffer at an offset.
// Used to fetch a small window for bit-aligned symbol extraction.
static inline uint32_t lzw_peek_u32(const uint8_t *p, size_t len, size_t off_bytes) {
    uint32_t v = 0;
    if (off_bytes < len) {
        size_t rem = len - off_bytes;
        if (rem > 4)
            rem = 4;
        memcpy(&v, p + off_bytes, rem);
    }
    return v;
}

// Read the next symbol from the bitstream using the current symbol size.
// Returns -1 on end-of-input or the symbol value.
static int lzw_read_symbol(lzw_ctx *c) {
    if ((c->bit_offset >> 3) >= c->src_len)
        return -1;
    uint32_t bits = lzw_peek_u32(c->src, c->src_len, c->bit_offset >> 3);
    int mask = (1 << c->symbol_size) - 1;
    int sym = (int)((bits >> (c->bit_offset & 7)) & (uint32_t)mask);
    c->bit_offset += (size_t)c->symbol_size;
    c->num_symbols_in_block++;
    return sym;
}

// Expand a dictionary string identified by `symbol` into the LZW output buffer.
// Writes the expanded bytes into the internal output buffer for later reads.
static void lzw_output_string(lzw_ctx *c, int symbol) {
    size_t len = c->dict[symbol].length;
    if (len > sizeof(c->out_buf))
        len = sizeof(c->out_buf);
    size_t pos = len;
    int cur = symbol;
    while (cur != (int)UINT16_MAX && pos > 0) {
        c->out_buf[--pos] = c->dict[cur].character;
        cur = c->dict[cur].parent;
    }
    memmove(c->out_buf, c->out_buf + pos, len - pos);
    c->out_len = len - pos;
    c->out_pos = 0;
}

// Produce up to `cap` decompressed bytes from the LZW context into `out`.
// Returns number of bytes written or 0 on EOF.
static ssize_t lzw_read(lzw_ctx *c, uint8_t *out, size_t cap) {
    size_t produced = 0;
    while (produced < cap) {
        if (c->out_pos < c->out_len) {
            size_t n = c->out_len - c->out_pos;
            if (n > cap - produced)
                n = cap - produced;
            memcpy(out + produced, c->out_buf + c->out_pos, n);
            c->out_pos += n;
            produced += n;
            continue;
        }
        int sym = lzw_read_symbol(c);
        if (sym < 0)
            break;
        if (sym == 256) {
            if (c->num_symbols_in_block & 7) {
                c->bit_offset += (size_t)(c->symbol_size * (8 - (c->num_symbols_in_block & 7)));
            }
            c->dict_size = 257;
            c->last_symbol = -1;
            c->symbol_size = 9;
            c->num_symbols_in_block = 0;
            continue;
        }
        if (c->last_symbol < 0) {
            if (sym < 256)
                out[produced++] = (uint8_t)sym;
            c->last_symbol = sym;
            continue;
        }
        uint8_t new_char = (sym < c->dict_size) ? c->dict[c->dict[sym].root].character
                                                : c->dict[c->dict[c->last_symbol].root].character;
        if (c->dict_size < (int)(sizeof(c->dict) / sizeof(c->dict[0]))) {
            c->dict[c->dict_size].parent = (uint16_t)c->last_symbol;
            c->dict[c->dict_size].length = c->dict[c->last_symbol].length + 1;
            c->dict[c->dict_size].character = new_char;
            c->dict[c->dict_size].root = c->dict[c->last_symbol].root;
            c->dict_size++;
            if (c->dict_size < (int)(sizeof(c->dict) / sizeof(c->dict[0])) &&
                (c->dict_size & (c->dict_size - 1)) == 0 && c->symbol_size < 14) {
                c->symbol_size++;
            }
        }
        if (sym < c->dict_size) {
            lzw_output_string(c, sym);
        } else {
            // Special KwKwK case: output last string + new_char
            size_t len = (size_t)c->dict[c->last_symbol].length + 1;
            if (len > sizeof(c->out_buf))
                len = sizeof(c->out_buf);
            size_t pos = len;
            c->out_buf[--pos] = new_char;
            int cur = c->last_symbol;
            while (cur != (int)UINT16_MAX && pos > 0) {
                c->out_buf[--pos] = c->dict[cur].character;
                cur = c->dict[cur].parent;
            }
            memmove(c->out_buf, c->out_buf + pos, len - pos);
            c->out_len = len - pos;
            c->out_pos = 0;
        }
        c->last_symbol = sym;
    }
    return (ssize_t)produced;
}

// Free an LZW streaming context and its resources.
static void lzw_free(lzw_ctx *c) { free(c); }

// Fill `dst` with up to `cap` decompressed bytes from the given streaming state.
// Supports raw copy, RLE90, LZW, SIT13 and SIT15 streaming kinds.
static ssize_t sit_stream_fill(sit_stream_state_t *ss, uint8_t *dst, size_t cap, sit15_ctx_t *sit15_ctx) {
    size_t produced = 0;
    if (ss->out_rem == 0)
        return 0;
    while (produced < cap && ss->out_rem > 0) {
        if (ss->kind == STRM_COPY) {
            size_t n = ss->src_len - ss->src_pos;
            if (n > ss->out_rem)
                n = ss->out_rem;
            if (n > cap - produced)
                n = cap - produced;
            memcpy(dst + produced, ss->src + ss->src_pos, n);
            ss->src_pos += n;
            ss->out_rem -= n;
            produced += n;
            if (!ss->skip_crc)
                ss->crc_accum = sit_crc_update(ss->crc_accum, dst + produced - n, n);
        } else if (ss->kind == STRM_RLE90) {
            // produce one byte at a time to honor rep counts
            if (ss->rep_rem > 0) {
                dst[produced++] = ss->last_byte;
                ss->rep_rem--;
                ss->out_rem--;
                if (!ss->skip_crc)
                    ss->crc_accum = sit_crc_update(ss->crc_accum, &dst[produced - 1], 1);
                continue;
            }
            if (ss->src_pos >= ss->src_len)
                break; // input exhausted unexpectedly
            uint8_t b = ss->src[ss->src_pos++];
            if (b == 0x90) {
                if (ss->src_pos >= ss->src_len)
                    break;
                uint8_t n = ss->src[ss->src_pos++];
                if (n == 0x00) {
                    // Literal 0x90; do NOT update last_byte (matches one-shot behavior)
                    dst[produced++] = 0x90;
                    ss->out_rem--;
                    if (!ss->skip_crc)
                        ss->crc_accum = sit_crc_update(ss->crc_accum, &dst[produced - 1], 1);
                } else {
                    if (n > 1) {
                        ss->rep_rem = (size_t)n - 1; // emit last_byte rep_rem times
                    } else {
                        // n==1 means repeat last byte zero times; nothing to do
                    }
                }
            } else {
                dst[produced++] = b;
                ss->last_byte = b;
                ss->out_rem--;
                if (!ss->skip_crc)
                    ss->crc_accum = sit_crc_update(ss->crc_accum, &dst[produced - 1], 1);
            }
        } else if (ss->kind == STRM_SIT15) {
            size_t want = ss->out_rem < (cap - produced) ? ss->out_rem : (cap - produced);
            ssize_t n = sit15_read(sit15_ctx, dst + produced, want);
            if (n < 0)
                return MUNBOX_ERROR;
            if (n == 0)
                break;
            if (!ss->skip_crc)
                ss->crc_accum = sit_crc_update(ss->crc_accum, dst + produced, (size_t)n);
            produced += (size_t)n;
            ss->out_rem -= (size_t)n;
        } else if (ss->kind == STRM_LZW) {
            size_t want = ss->out_rem < (cap - produced) ? ss->out_rem : (cap - produced);
            if (!ss->lzw)
                return MUNBOX_ERROR;
            ssize_t n = lzw_read(ss->lzw, dst + produced, want);
            if (n < 0)
                return MUNBOX_ERROR;
            if (n == 0)
                break;
            if (!ss->skip_crc)
                ss->crc_accum = sit_crc_update(ss->crc_accum, dst + produced, (size_t)n);
            produced += (size_t)n;
            ss->out_rem -= (size_t)n;
        } else if (ss->kind == STRM_SIT13) {
            size_t want = ss->out_rem < (cap - produced) ? ss->out_rem : (cap - produced);
            if (!ss->sit13)
                return MUNBOX_ERROR;
            ssize_t n = sit13_read(ss->sit13, dst + produced, want);
            if (n < 0)
                return MUNBOX_ERROR;
            if (n == 0)
                break;
            if (!ss->skip_crc)
                ss->crc_accum = sit_crc_update(ss->crc_accum, dst + produced, (size_t)n);
            produced += (size_t)n;
            ss->out_rem -= (size_t)n;
        } else {
            return MUNBOX_ERROR;
        }
    }
    return (ssize_t)produced;
}

// Read uncompressed bytes from the currently opened SIT fork into `buf`.
// Delegates to sit_stream_fill and performs CRC check after EOF of fork.
static ssize_t sit_layer_read(munbox_layer_t *self, void *buf, size_t cnt) {
    sit_layer_state_t *st = (sit_layer_state_t *)self->internal_state;
    if (!st)
        return MUNBOX_ERROR;
    if (!st->opened)
        return munbox_error("read() called before open() on sit layer");
    // Streaming path
    ssize_t n = sit_stream_fill(&st->stream, (uint8_t *)buf, cnt, st->sit15_ctx);
    if (st->stream.out_rem == 0) {
        // verify CRC if applicable
        if (!st->stream.skip_crc) {
            if (st->stream.crc_accum != st->expected_crc) {
                if (sit_debug_enabled()) {
                    fprintf(stderr, "[SIT] CRC mismatch: expected=%04x computed=%04x (file='%s', fork=%s)\n",
                            (unsigned)st->expected_crc, (unsigned)st->stream.crc_accum, st->cur_info.filename,
                            st->cur_info.fork_type == (int)MUNBOX_FORK_RESOURCE ? "rsrc" : "data");
                }
                return munbox_error("SIT fork CRC mismatch");
            }
        }
    }
    return n;
}

// Open (or advance) the current file/fork in the SIT archive and return file info.
// Prepares per-fork streaming contexts and fills `info` with metadata.
static int sit_layer_open(munbox_layer_t *self, munbox_open_t what, munbox_file_info_t *info) {
    sit_layer_state_t *st = (sit_layer_state_t *)self->internal_state;
    if (!st || !info)
        return munbox_error("Invalid parameters to sit_layer_open");
    // Build index on first use
    if (!st->entries) {
        int r = st->is_sit5 ? sit_build_index_sit5(st) : sit_build_index_classic(st);
        if (r < 0)
            return r;
        if (st->entry_count == 0)
            return 0;
        self->read = sit_layer_read;
        /* get_file_info removed; metadata available via open() */
        // debug
        // fprintf(stderr, "[SIT] index built: %zu entries\n", st->entry_count);
    }
    st->opened = true;
    if (what == MUNBOX_OPEN_FIRST) {
        st->iter_entry = 0;
        st->iter_fork = 0; // start with data
    } else {
        if (st->iter_entry >= st->entry_count)
            return 0;
        // advance fork then entry
        if (st->iter_fork == 0 && st->entries[st->iter_entry].rsrc.uncomp_len > 0) {
            st->iter_fork = 1;
        } else {
            st->iter_entry++;
            st->iter_fork = 0;
        }
    }
    // Skip empty forks and out-of-range
    while (st->iter_entry < st->entry_count) {
        sit_index_entry_t *e = &st->entries[st->iter_entry];
        if (st->iter_fork == 0 && e->data.uncomp_len == 0) {
            st->iter_fork = 1;
            continue;
        }
        if (st->iter_fork == 1 && e->rsrc.uncomp_len == 0) {
            st->iter_entry++;
            st->iter_fork = 0;
            continue;
        }
        break;
    }
    if (st->iter_entry >= st->entry_count)
        return 0;

    // Reset any previous streaming state/buffer
    if (st->sit15_ctx) {
        sit15_free(st->sit15_ctx);
        st->sit15_ctx = NULL;
    }
    if (st->sit13_ctx) {
        sit13_free(st->sit13_ctx);
        st->sit13_ctx = NULL;
    }
    if (st->lzw_ctx) {
        lzw_free(st->lzw_ctx);
        st->lzw_ctx = NULL;
    }
    st->cur_stream_kind = STRM_NONE;
    sit_index_entry_t *e = &st->entries[st->iter_entry];
    sit_fork_desc_t *fd = (st->iter_fork == 0) ? &e->data : &e->rsrc;
    if (fd->uncomp_len > 0) {
        // 100% streaming implementation for methods 0,1,2,13,15
        st->stream.src = fd->comp_ptr;
        st->stream.src_len = fd->comp_len;
        st->stream.src_pos = 0;
        st->stream.out_rem = fd->uncomp_len;
        st->stream.skip_crc = false;
        st->expected_crc = fd->crc;
        st->stream.crc_accum = 0; // SIT CRC starts from 0
        st->stream.last_byte = 0;
        st->stream.rep_rem = 0;
        if (sit_debug_enabled()) {
            fprintf(stderr, "[SIT] fork open: file='%s' fork=%s method=%u comp=%u uncomp=%u crc=%04x\n", e->path,
                    (st->iter_fork == 0) ? "data" : "rsrc", (unsigned)fd->method, (unsigned)fd->comp_len,
                    (unsigned)fd->uncomp_len, (unsigned)fd->crc);
        }
        if (fd->method == 0) {
            st->cur_stream_kind = STRM_COPY;
            st->stream.kind = STRM_COPY;
        } else if (fd->method == 1) {
            st->cur_stream_kind = STRM_RLE90;
            st->stream.kind = STRM_RLE90;
        } else if (fd->method == 2) {
            // Initialize LZW streaming
            st->lzw_ctx = lzw_init(fd->comp_ptr, fd->comp_len);
            if (!st->lzw_ctx)
                return munbox_error("Out of memory");
            st->stream.lzw = st->lzw_ctx;
            st->cur_stream_kind = STRM_LZW;
            st->stream.kind = STRM_LZW;
        } else if (fd->method == 13) {
            st->sit13_ctx = sit13_init(fd->comp_ptr, fd->comp_len);
            if (!st->sit13_ctx)
                return munbox_error("SIT13 init failed");
            st->stream.sit13 = st->sit13_ctx;
            st->cur_stream_kind = STRM_SIT13;
            st->stream.kind = STRM_SIT13;
        } else if (fd->method == 15) {
            st->sit15_ctx = sit15_init(fd->comp_ptr, fd->comp_len);
            if (!st->sit15_ctx)
                return munbox_error("SIT15 init failed");
            st->cur_stream_kind = STRM_SIT15;
            st->stream.kind = STRM_SIT15;
            st->stream.skip_crc = true; // method 15 CRC validated internally
        } else {
            return munbox_error("Unsupported SIT compression method: %d", fd->method);
        }
    }

    memset(&st->cur_info, 0, sizeof(st->cur_info));
    strncpy(st->cur_info.filename, e->path, sizeof(st->cur_info.filename) - 1);
    st->cur_info.type = e->type;
    st->cur_info.creator = e->creator;
    st->cur_info.finder_flags = e->finder_flags;
    st->cur_info.length = fd->uncomp_len;
    st->cur_info.has_metadata = true;
    st->cur_info.fork_type = (st->iter_fork == 0) ? MUNBOX_FORK_DATA : MUNBOX_FORK_RESOURCE;
    *info = st->cur_info;
    return 1;
}
