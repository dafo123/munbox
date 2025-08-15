// SPDX-License-Identifier: MIT

/**
 * @file    munbox.h
 * @brief   Main header for the munbox library.
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

#ifndef MUNBOX_H
#define MUNBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h> // For ssize_t

/* --- Core Abstractions --- */

/**
 * @brief File metadata that can be extracted from archive formats.
 */
typedef struct munbox_file_info {
    char filename[256]; /**< Original filename (null-terminated) */
    uint32_t type; /**< Mac file type (e.g., 'TEXT', 'APPL') */
    uint32_t creator; /**< Mac creator code (e.g., 'MSWD', 'ttxt') */
    uint16_t finder_flags; /**< Mac Finder flags */
    /**
     * Length in bytes of the currently opened fork.
     * After a successful open(FIRST/NEXT), this is the size of the fork
     * identified by fork_type. Separate data/resource fork lengths were
     * removed because only one fork is active at a time.
     */
    uint32_t length;
    /**
     * @brief Which fork is currently opened when using munbox_layer_t.open().
     *
     * Layers that implement munbox_layer_t.open() will set this to indicate
     * whether the currently opened stream reads the data fork or the resource
     * fork. Callers that don't use open() can ignore this field.
     */
    int /* munbox_fork_t */ fork_type;
    bool has_metadata; /**< True if metadata fields are valid */
} munbox_file_info_t;

/**
 * @brief Iterator control for munbox_layer_t.open().
 */
typedef enum { MUNBOX_OPEN_FIRST = 0, MUNBOX_OPEN_NEXT = 1 } munbox_open_t;

/**
 * @brief Fork identifier for file streams opened via munbox_layer_t.open().
 */
typedef enum { MUNBOX_FORK_DATA = 0, MUNBOX_FORK_RESOURCE = 1 } munbox_fork_t;

/**
 * @brief Extended output callbacks that support resource fork data.
 */
typedef struct munbox_extract_callbacks {
    int (*new_file)(const char *path, const munbox_file_info_t *file_info, void **out_user_data);
    int (*write_data)(void *user_data, const void *buf, size_t cnt);
    int (*write_resource_fork)(void *user_data, const void *rsrc_data, size_t rsrc_size);
    int (*end_file)(void *user_data);
} munbox_extract_callbacks_t;

/**
 * @brief Represents a single layer in the processing pipeline.
 *
 * Each layer can optionally provide multiple capabilities:
 * - Stream transformation (read) to provide transformed bytes
 * - File iteration (open/read) to emit files/forks via callbacks; metadata is
 *   returned via open(FIRST/NEXT) in the munbox_file_info_t passed by caller
 */
typedef struct munbox_layer {
    /**
     * Read transformed data from this layer.
     *
     * Contract: A successful open(MUNBOX_OPEN_FIRST, ...) must be called
     * on this layer before the first read(). Calling read() before open()
     * is a usage error and will return MUNBOX_ERROR.
     */
    ssize_t (*read)(struct munbox_layer *self, void *buf, size_t cnt);

    /* Removed: peek() no longer used; identification uses open(FIRST)+read */

    /** Required: Release resources owned by this layer. */
    void (*close)(struct munbox_layer *self);

    /**
     * Optional: Open the first/next file or fork for reading from the beginning.
     * Returns 1 on success (and populates 'info'), 0 on end-of-archive, or -1 on error.
     */
    int (*open)(struct munbox_layer *self, munbox_open_t what, munbox_file_info_t *info);

    void *internal_state;
} munbox_layer_t;

/* --- Error Handling --- */

#define MUNBOX_ERROR (-1) /**< Generic failure. Call munbox_last_error() for details. */
#define MUNBOX_ABORT (-2) /**< Propagated user abort from a callback. */

const char *munbox_last_error(void);
int munbox_error(const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* --- File Metadata Utilities --- */

/**
 * @brief Initialize a file info structure with default values.
 * @param info The structure to initialize.
 */

/* get_file_info() removed: metadata is returned by open(FIRST/NEXT) */

/* --- Main Processing Function --- */

/**
 * @brief Processes a stream with extended callbacks that support resource forks.
 *
 * @param initial_layer The starting layer to process (e.g., from a file or memory).
 * @param callbacks The extended callbacks including resource fork support.
 * @return 0 on success, MUNBOX_ERROR on failure, or MUNBOX_ABORT if a callback aborted.
 */
int munbox_process(munbox_layer_t *initial_layer, const munbox_extract_callbacks_t *callbacks);

/* --- Individual Format Handlers (for manual chaining if needed) --- */

munbox_layer_t *munbox_new_mem_layer(const void *buffer, size_t size);
munbox_layer_t *munbox_new_file_layer(const char *path);

munbox_layer_t *munbox_new_hqx_layer(munbox_layer_t *input);
munbox_layer_t *munbox_new_sit_layer(munbox_layer_t *input);
munbox_layer_t *munbox_new_cpt_layer(munbox_layer_t *input);
munbox_layer_t *munbox_new_bin_layer(munbox_layer_t *input);

#ifdef __cplusplus
}
#endif

#endif // MUNBOX_H