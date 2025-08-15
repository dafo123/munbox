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

// munbox.c
// Core implementation of the munbox external API.

#include "munbox_internal.h"
#include <errno.h>
#include <stdarg.h>

#if __STDC_VERSION__ >= 201112L
#include <threads.h>
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#error "Compiler does not support thread-local storage"
#endif

// --- Error Handling Implementation ---

#define ERROR_BUFFER_SIZE 1024
static THREAD_LOCAL char g_error_buffer[ERROR_BUFFER_SIZE] = "No error";

// Returns the last error message from the thread-local error buffer
const char *munbox_last_error(void) { return g_error_buffer; }

// Sets an error message in the thread-local buffer and returns MUNBOX_ERROR
int munbox_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_error_buffer, sizeof(g_error_buffer), fmt, args);
    va_end(args);
    return MUNBOX_ERROR;
}

// --- File Layer Implementation ---

// Reads data from the file layer's underlying file stream
static ssize_t file_layer_read(struct munbox_layer *self, void *buf, size_t cnt) {
    file_layer_state_t *state = (file_layer_state_t *)self->internal_state;
    if (!state->opened)
        return munbox_error("read() called before open() on file layer");
    if (state->eof_reached)
        return 0;

    size_t bytes_read = fread(buf, 1, cnt, state->file);
    if (bytes_read < cnt) {
        if (ferror(state->file))
            return munbox_error("file read error: %s", strerror(errno));
        if (feof(state->file))
            state->eof_reached = true;
    }
    return (ssize_t)bytes_read;
}

// Opens the file for reading and provides file metadata
static int file_layer_open(struct munbox_layer *self, munbox_open_t what, munbox_file_info_t *info) {
    if (!self || !info)
        return munbox_error("Invalid parameters to file_layer_open");
    file_layer_state_t *state = (file_layer_state_t *)self->internal_state;
    if (!state || !state->file)
        return munbox_error("file layer has no state");
    if (what == MUNBOX_OPEN_FIRST) {
        if (fseek(state->file, 0, SEEK_SET) != 0)
            return munbox_error("file seek failed: %s", strerror(errno));
        state->eof_reached = false;
        state->opened = true;
        memset(info, 0, sizeof(*info));
        info->fork_type = MUNBOX_FORK_DATA;
        info->has_metadata = false;
        return 1; // single stream
    }
    return 0; // no NEXT
}

// Closes the file layer and frees associated resources
static void file_layer_close(struct munbox_layer *self) {
    if (!self)
        return;
    file_layer_state_t *state = (file_layer_state_t *)self->internal_state;
    if (state) {
        if (state->file)
            fclose(state->file);
        free(state);
    }
    free(self);
}

// Creates a new file layer for reading from the specified file path
munbox_layer_t *munbox_new_file_layer(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        munbox_error("could not open file '%s': %s", path, strerror(errno));
        return NULL;
    }

    munbox_layer_t *layer = malloc(sizeof(munbox_layer_t));
    file_layer_state_t *state = calloc(1, sizeof(file_layer_state_t));

    if (!layer || !state) {
        free(layer);
        free(state);
        fclose(f);
        munbox_error("out of memory");
        return NULL;
    }

    state->file = f;
    state->opened = false;
    layer->internal_state = state;
    layer->read = file_layer_read;
    layer->close = file_layer_close;
    layer->open = file_layer_open;

    return layer;
}

// --- Memory Layer Implementation ---

// Reads data from the memory layer's buffer
static ssize_t mem_layer_read(struct munbox_layer *self, void *buf, size_t cnt) {
    mem_layer_state_t *state = (mem_layer_state_t *)self->internal_state;
    if (!state->opened)
        return munbox_error("read() called before open() on memory layer");
    size_t remaining = state->size - state->pos;
    size_t bytes_to_read = (cnt < remaining) ? cnt : remaining;

    if (bytes_to_read > 0) {
        memcpy(buf, state->buffer + state->pos, bytes_to_read);
        state->pos += bytes_to_read;
    }
    return (ssize_t)bytes_to_read;
}

// Closes the memory layer and frees associated resources
static void mem_layer_close(struct munbox_layer *self) {
    if (!self)
        return;
    free(self->internal_state);
    free(self);
}

// Opens the memory buffer for reading and provides metadata
static int mem_layer_open(struct munbox_layer *self, munbox_open_t what, munbox_file_info_t *info) {
    if (!self || !info)
        return munbox_error("Invalid parameters to mem_layer_open");
    mem_layer_state_t *state = (mem_layer_state_t *)self->internal_state;
    if (!state)
        return munbox_error("memory layer has no state");
    if (what == MUNBOX_OPEN_FIRST) {
        state->pos = 0;
        state->opened = true;
        memset(info, 0, sizeof(*info));
        info->fork_type = MUNBOX_FORK_DATA;
        info->has_metadata = false;
        return 1;
    }
    return 0;
}

// Creates a new memory layer for reading from the specified buffer
munbox_layer_t *munbox_new_mem_layer(const void *buffer, size_t size) {
    if (!buffer)
        return NULL;

    munbox_layer_t *layer = malloc(sizeof(munbox_layer_t));
    mem_layer_state_t *state = malloc(sizeof(mem_layer_state_t));

    if (!layer || !state) {
        free(layer);
        free(state);
        munbox_error("out of memory");
        return NULL;
    }

    state->buffer = (const uint8_t *)buffer;
    state->size = size;
    state->pos = 0;
    state->opened = false;

    layer->internal_state = state;
    layer->read = mem_layer_read;
    layer->close = mem_layer_close;
    layer->open = mem_layer_open; 

    return layer;
}

// --- Processing Pipeline ---

// extern munbox_layer_t* munbox_new_bin_layer(munbox_layer_t *input);
// extern munbox_layer_t* munbox_new_cpt_layer(munbox_layer_t *input);

// The static list of all known format handlers.
static const munbox_format_handler_t g_format_handlers[] = {
    {"sit", munbox_new_sit_layer},
    {"hqx", munbox_new_hqx_layer},
    {"bin", munbox_new_bin_layer},
    //{"MacBinary Layer", munbox_new_bin_layer},
    {"cpt", munbox_new_cpt_layer},
};
static const size_t g_num_format_handlers = sizeof(g_format_handlers) / sizeof(g_format_handlers[0]);

// Processes a layer through the pipeline of format handlers and extracts files
int munbox_process(munbox_layer_t *initial_layer, const munbox_extract_callbacks_t *callbacks) {
    if (!initial_layer)
        return munbox_error("initial_layer cannot be NULL");

    munbox_layer_t *current_layer = initial_layer;

    // Try all registered layer factories in sequence
    size_t last_transform_handler = (size_t)-1; // Skip re-running the same handler right after a transform
    for (size_t i = 0; i < g_num_format_handlers; ++i) {
        if (i == last_transform_handler) {
            continue; // Don't re-run the handler that just produced the transform
        }
        // Try to create a layer for this format
        munbox_layer_t *format_layer = g_format_handlers[i].layer_factory(current_layer);
        if (!format_layer) {
            // This format doesn't recognize the input, try next
            continue;
        }

        // This format recognizes the input. Check what capabilities it provides.
        if (format_layer->open && format_layer->read) {
            // Archive-like layer with iteration; stream files via callbacks
            int ret = 0;
            void *user_data = NULL;
            char current_name[256] = {0};
            munbox_file_info_t info;
            int rc = format_layer->open(format_layer, MUNBOX_OPEN_FIRST, &info);
            if (rc < 0) {
                format_layer->close(format_layer);
                return rc;
            }
            while (rc == 1) {
                // Start new file when filename changes
                if (current_name[0] == '\0' || strncmp(current_name, info.filename, sizeof(current_name)) != 0) {
                    if (user_data) {
                        if (callbacks->end_file(user_data) != 0) {
                            ret = MUNBOX_ABORT;
                            break;
                        }
                        user_data = NULL;
                    }
                    if (callbacks->new_file(info.filename, &info, &user_data) != 0) {
                        ret = MUNBOX_ABORT;
                        break;
                    }
                    strncpy(current_name, info.filename, sizeof(current_name) - 1);
                    current_name[sizeof(current_name) - 1] = '\0';
                }

                // Read current fork fully and write accordingly
                uint8_t *buf = NULL;
                size_t size = 0, cap = 0;
                for (;;) {
                    if (cap - size < 64 * 1024) {
                        size_t new_cap = cap ? cap * 2 : 64 * 1024;
                        void *nb = realloc(buf, new_cap);
                        if (!nb) {
                            free(buf);
                            ret = munbox_error("out of memory");
                            break;
                        }
                        buf = (uint8_t *)nb;
                        cap = new_cap;
                    }
                    ssize_t n = format_layer->read(format_layer, buf + size, cap - size);
                    if (n < 0) {
                        ret = MUNBOX_ERROR;
                        break;
                    }
                    if (n == 0)
                        break;
                    size += (size_t)n;
                }
                if (ret != 0) {
                    free(buf);
                    break;
                }
                munbox_file_info_t finfo = info;
                if (finfo.fork_type == (int)MUNBOX_FORK_RESOURCE) {
                    if (callbacks->write_resource_fork(user_data, buf, size) != 0) {
                        free(buf);
                        ret = MUNBOX_ABORT;
                    }
                } else {
                    if (size && callbacks->write_data(user_data, buf, size) != 0) {
                        free(buf);
                        ret = MUNBOX_ABORT;
                    }
                }
                free(buf);

                // Advance
                rc = format_layer->open(format_layer, MUNBOX_OPEN_NEXT, &info);
            }
            if (ret == 0 && user_data) {
                if (callbacks->end_file(user_data) != 0)
                    ret = MUNBOX_ABORT;
            }
            format_layer->close(format_layer);
            return ret;
        } else if (format_layer->read) {
            // This layer transforms the stream - wrap it and try the next format
            current_layer = format_layer;

            // Restart the search with the transformed stream, but skip re-running
            // the same handler that produced this transform to avoid consuming data
            // via peeks on the transformed stream.
            last_transform_handler = i;
            i = (size_t)-1; // Will wrap to 0 on increment
            continue;
        } else {
            // Layer doesn't provide either capability - this shouldn't happen
            format_layer->close(format_layer);
            return munbox_error("layer '%s' provides neither stream nor iteration capability",
                                g_format_handlers[i].name);
        }
    }

    // No format handler could extract files; fallback:
    // Open the final layer and iterate its forks, writing data and resource forks if available.
    munbox_file_info_t info;
    int rc_open = 0;
    if (current_layer->open) {
        rc_open = current_layer->open(current_layer, MUNBOX_OPEN_FIRST, &info);
        if (rc_open < 0) {
            current_layer->close(current_layer);
            return MUNBOX_ERROR;
        }
    }

    // Determine filename
    const char *filename = "untitled";
    munbox_file_info_t meta;
    memset(&meta, 0, sizeof(meta));
    if (rc_open == 1) {
        if (info.filename[0] != '\0')
            filename = info.filename;
        meta = info; // initial metadata
    }

    void *user_data = NULL;
    if (callbacks->new_file(filename, &meta, &user_data) != 0) {
        current_layer->close(current_layer);
        return MUNBOX_ABORT;
    }

    if (rc_open == 1) {
        // Iterate forks
        munbox_file_info_t cur = info;
        int more = 1;
        while (more == 1) {
            // Read current fork fully
            size_t size = 0, cap = 0;
            uint8_t *buf = NULL;
            for (;;) {
                if (cap - size < 64 * 1024) {
                    size_t ncap = cap ? cap * 2 : 64 * 1024;
                    void *nb = realloc(buf, ncap);
                    if (!nb) {
                        free(buf);
                        callbacks->end_file(user_data);
                        current_layer->close(current_layer);
                        return munbox_error("out of memory");
                    }
                    buf = (uint8_t *)nb;
                    cap = ncap;
                }
                ssize_t n = current_layer->read(current_layer, buf + size, cap - size);
                if (n < 0) {
                    free(buf);
                    callbacks->end_file(user_data);
                    current_layer->close(current_layer);
                    return MUNBOX_ERROR;
                }
                if (n == 0)
                    break;
                size += (size_t)n;
            }

            // Use current fork info from open()/iteration
            munbox_file_info_t finfo = cur;
            if (finfo.fork_type == (int)MUNBOX_FORK_RESOURCE) {
                if (callbacks->write_resource_fork(user_data, buf, size) != 0) {
                    free(buf);
                    callbacks->end_file(user_data);
                    current_layer->close(current_layer);
                    return MUNBOX_ABORT;
                }
            } else {
                if (size && callbacks->write_data(user_data, buf, size) != 0) {
                    free(buf);
                    callbacks->end_file(user_data);
                    current_layer->close(current_layer);
                    return MUNBOX_ABORT;
                }
            }
            free(buf);

            // Next fork
            munbox_file_info_t next;
            more = current_layer->open(current_layer, MUNBOX_OPEN_NEXT, &next);
            if (more < 0) {
                callbacks->end_file(user_data);
                current_layer->close(current_layer);
                return MUNBOX_ERROR;
            }
            if (more == 1)
                cur = next;
        }
    } else {
        // No open() provided; stream as raw data
        char buffer[4096];
        ssize_t n;
        while ((n = current_layer->read(current_layer, buffer, sizeof buffer)) > 0) {
            if (callbacks->write_data(user_data, buffer, (size_t)n) != 0) {
                callbacks->end_file(user_data);
                current_layer->close(current_layer);
                return MUNBOX_ABORT;
            }
        }
        if (n < 0) {
            callbacks->end_file(user_data);
            current_layer->close(current_layer);
            return MUNBOX_ERROR;
        }
    }

    if (callbacks->end_file(user_data) != 0) {
        current_layer->close(current_layer);
        return MUNBOX_ABORT;
    }
    current_layer->close(current_layer);
    return 0;
}

