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

#include <errno.h>
#include <getopt.h>
#include <libgen.h> // For dirname()
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "munbox.h"

// --- Global Configuration ---

// AppleDouble entry IDs
#define AD_DATA_FORK           1
#define AD_RESOURCE_FORK       2
#define AD_REAL_NAME           3
#define AD_COMMENT             4
#define AD_ICON_BW             5
#define AD_ICON_COLOR          6
#define AD_FILE_DATES_INFO     8
#define AD_FINDER_INFO         9
#define AD_MACINTOSH_FILE_INFO 10

// AppleDouble header structure
typedef struct {
    uint32_t magic; // 0x00051607
    uint32_t version; // 0x00020000
    uint8_t filler[16]; // All zeros
    uint16_t num_entries; // Number of entry descriptors
} __attribute__((packed)) appledouble_header_t;

// AppleDouble entry descriptor
typedef struct {
    uint32_t entry_id; // Entry type ID
    uint32_t offset; // Offset to entry data
    uint32_t length; // Length of entry data
} __attribute__((packed)) appledouble_entry_t;

// File dates info structure (32 bytes)
typedef struct {
    int32_t creation_date; // Seconds since Jan 1, 2000 GMT
    int32_t modification_date;
    int32_t backup_date;
    int32_t access_date;
    uint8_t reserved[16]; // Reserved bytes
} __attribute__((packed)) appledouble_dates_t;

// User data for AppleDouble file handling
typedef struct {
    FILE *data_file;
    FILE *header_file;
    char *base_path;
    bool has_resource_fork;
    munbox_file_info_t file_info;
    uint8_t *resource_fork_data;
    size_t resource_fork_size;
    char *header_path; // Path to the header file for updating
} appledouble_user_data_t;

typedef struct {
    bool use_apple_double;
    const char *output_dir;
    bool verbose;
} cli_options_t;

// A file-static pointer to the CLI options, accessible by the callbacks.
static const cli_options_t *g_cli_options = NULL;

// --- AppleDouble Helper Functions ---

// Convert host byte order to big-endian
static uint32_t htonl_portable(uint32_t hostlong) {
    uint32_t result = hostlong;
    uint8_t *bytes = (uint8_t *)&result;
    uint8_t temp;
    temp = bytes[0];
    bytes[0] = bytes[3];
    bytes[3] = temp;
    temp = bytes[1];
    bytes[1] = bytes[2];
    bytes[2] = temp;
    return result;
}

static uint16_t htons_portable(uint16_t hostshort) {
    uint16_t result = hostshort;
    uint8_t *bytes = (uint8_t *)&result;
    uint8_t temp = bytes[0];
    bytes[0] = bytes[1];
    bytes[1] = temp;
    return result;
}

// Convert big-endian to host byte order
static uint32_t ntohl_portable(uint32_t netlong) {
    return htonl_portable(netlong); // Same operation for byte swap
}

static uint16_t ntohs_portable(uint16_t netshort) {
    return htons_portable(netshort); // Same operation for byte swap
}

// Update AppleDouble header with actual resource fork data
static int update_appledouble_header_with_resource_fork(const char *header_path, const uint8_t *rsrc_data,
                                                        size_t rsrc_size) {
    if (!rsrc_data || rsrc_size == 0) {
        return 0; // No resource fork to add
    }

    // Read the existing header
    FILE *header_file = fopen(header_path, "r+b");
    if (!header_file) {
        return -1;
    }

    // Read the header to get the current structure
    appledouble_header_t header;
    if (fread(&header, sizeof(header), 1, header_file) != 1) {
        fclose(header_file);
        return -1;
    }

    uint16_t num_entries = ntohs_portable(header.num_entries);

    // Find the resource fork entry
    bool found_rsrc_entry = false;
    appledouble_entry_t rsrc_entry;
    long rsrc_entry_pos = -1;

    for (int i = 0; i < num_entries; i++) {
        long entry_pos = ftell(header_file);
        appledouble_entry_t entry;
        if (fread(&entry, sizeof(entry), 1, header_file) != 1) {
            fclose(header_file);
            return -1;
        }

        if (ntohl_portable(entry.entry_id) == AD_RESOURCE_FORK) {
            rsrc_entry = entry;
            rsrc_entry_pos = entry_pos;
            found_rsrc_entry = true;
            break;
        }
    }

    if (!found_rsrc_entry) {
        // No resource entry present; we need to append a new descriptor block is not possible
        // because descriptors are contiguous at the start. Instead, rewrite header to include it.
        // Strategy: read whole file, then rewrite with num_entries+1 and new rsrc entry at end.
        fseek(header_file, 0, SEEK_END);
        long original_size = ftell(header_file);
        uint8_t *buffer = (uint8_t *)malloc((size_t)original_size);
        if (!buffer) {
            fclose(header_file);
            return -1;
        }
        fseek(header_file, 0, SEEK_SET);
        if (fread(buffer, 1, (size_t)original_size, header_file) != (size_t)original_size) {
            free(buffer);
            fclose(header_file);
            return -1;
        }

        // Parse existing header and descriptors
        appledouble_header_t *hdr = (appledouble_header_t *)buffer;
        uint16_t old_entries = ntohs_portable(hdr->num_entries);
        appledouble_entry_t *entries = (appledouble_entry_t *)(buffer + sizeof(appledouble_header_t));

        size_t new_entries = (size_t)old_entries + 1;
        size_t new_header_size = sizeof(appledouble_header_t) + new_entries * sizeof(appledouble_entry_t);
        size_t finder_data_len = 32; // we only write Finder Info block before

        // Allocate new buffer for expanded header
        size_t rebuilt_size = new_header_size + finder_data_len; // no rsrc data yet
        uint8_t *rebuilt = (uint8_t *)calloc(1, rebuilt_size);
        if (!rebuilt) {
            free(buffer);
            fclose(header_file);
            return -1;
        }

        // Fill new header
        appledouble_header_t *newhdr = (appledouble_header_t *)rebuilt;
        newhdr->magic = ((appledouble_header_t *)buffer)->magic;
        newhdr->version = ((appledouble_header_t *)buffer)->version;
        memset(newhdr->filler, 0, sizeof newhdr->filler);
        newhdr->num_entries = htons_portable((uint16_t)new_entries);

        appledouble_entry_t *newentries = (appledouble_entry_t *)(rebuilt + sizeof(appledouble_header_t));

        // Copy Finder Info entry as first
        // Assume first old entry is Finder Info
        newentries[0] = entries[0];
        // Recompute its offset to follow the new descriptor table
        uint32_t new_finder_off = (uint32_t)new_header_size;
        newentries[0].offset = htonl_portable(new_finder_off);
        newentries[0].length = htonl_portable(32);

        // Add Resource Fork descriptor as second
        newentries[1].entry_id = htonl_portable(AD_RESOURCE_FORK);
        newentries[1].offset = htonl_portable(new_finder_off + 32);
        newentries[1].length = htonl_portable(0);

        // Copy Finder Info data into place
        memcpy(rebuilt + new_finder_off, buffer + ntohl_portable(entries[0].offset), 32);

        // Rewrite file with rebuilt header
        FILE *reopened = freopen(header_path, "wb", header_file);
        if (!reopened) {
            free(buffer);
            free(rebuilt);
            fclose(header_file);
            return -1;
        }
        if (fwrite(rebuilt, 1, rebuilt_size, header_file) != rebuilt_size) {
            free(buffer);
            free(rebuilt);
            fclose(header_file);
            return -1;
        }
        fflush(header_file);

        // Set up rsrc_entry object and position for later update
        rsrc_entry_pos = sizeof(appledouble_header_t) + sizeof(appledouble_entry_t); // second descriptor
        rsrc_entry.entry_id = htonl_portable(AD_RESOURCE_FORK);
        rsrc_entry.offset = htonl_portable(new_finder_off + 32);
        rsrc_entry.length = htonl_portable(0);
        found_rsrc_entry = true;

        free(buffer);
        free(rebuilt);
    }

    // Get current file size to append resource fork data
    fseek(header_file, 0, SEEK_END);
    long current_file_size = ftell(header_file);

    // Update the resource fork entry with the new offset and length
    rsrc_entry.offset = htonl_portable((uint32_t)current_file_size);
    rsrc_entry.length = htonl_portable((uint32_t)rsrc_size);

    // Write the updated entry back
    fseek(header_file, rsrc_entry_pos, SEEK_SET);
    if (fwrite(&rsrc_entry, sizeof(rsrc_entry), 1, header_file) != 1) {
        fclose(header_file);
        return -1;
    }

    // Append the resource fork data at the end
    fseek(header_file, 0, SEEK_END);
    if (fwrite(rsrc_data, 1, rsrc_size, header_file) != rsrc_size) {
        fclose(header_file);
        return -1;
    }

    fclose(header_file);
    return 0;
}

// Create AppleDouble header file
static int create_appledouble_header(const char *header_path, const munbox_file_info_t *file_info) {
    FILE *header_file = fopen(header_path, "wb");
    if (!header_file) {
        return -1;
    }

    // Calculate number of entries and total header size
    // Minimal AppleDouble: Finder Info always; Resource Fork only if present
    /* Fork sizes are not both known upfront (unified length field). Start with
        only Finder Info entry; resource fork entry added lazily if fork appears. */
    int num_entries = 1;
    size_t entries_data_offset = sizeof(appledouble_header_t) + (size_t)num_entries * sizeof(appledouble_entry_t);

    // Write header
    appledouble_header_t header = {0};
    header.magic = htonl_portable(0x00051607);
    header.version = htonl_portable(0x00020000);
    memset(header.filler, 0, 16);
    header.num_entries = htons_portable((uint16_t)num_entries);

    if (fwrite(&header, sizeof(header), 1, header_file) != 1) {
        fclose(header_file);
        return -1;
    }

    // Write entry descriptors
    size_t current_data_offset = entries_data_offset;

    // Finder Info entry (32 bytes: 16 bytes Finder info + 16 bytes extended)
    appledouble_entry_t finder_entry = {.entry_id = htonl_portable(AD_FINDER_INFO),
                                        .offset = htonl_portable((uint32_t)current_data_offset),
                                        .length = htonl_portable(32)};
    if (fwrite(&finder_entry, sizeof(finder_entry), 1, header_file) != 1) {
        fclose(header_file);
        return -1;
    }
    current_data_offset += 32;

    // Resource Fork entry (only if present initially; placeholder updated later)
    /* Resource fork entry omitted initially */

    // Write entry data

    // Finder Info data (32 bytes)
    uint8_t finder_info[32] = {0};
    if (file_info && file_info->has_metadata) {
        uint32_t *finder_data = (uint32_t *)finder_info;
        finder_data[0] = htonl_portable(file_info->type);
        finder_data[1] = htonl_portable(file_info->creator);
        uint16_t *finder_flags = (uint16_t *)(finder_info + 8);
        finder_flags[0] = htons_portable(file_info->finder_flags);
    }
    if (fwrite(finder_info, 32, 1, header_file) != 1) {
        fclose(header_file);
        return -1;
    }

    fclose(header_file);
    return 0;
}

// --- Filesystem Callbacks ---

// Recursively creates a directory path.
static int ensure_dir_exists(const char *path) {
    // Create a mutable copy because dirname() can modify its argument.
    char *path_copy = strdup(path);
    if (!path_copy)
        return -1;

    char *dir = dirname(path_copy);
    char full_path[1024];

    // snprintf returns the number of characters that *would have been* written.
    if (snprintf(full_path, sizeof(full_path), "%s/%s", g_cli_options->output_dir, dir) >= (int)sizeof(full_path)) {
        fprintf(stderr, "Error: Output path is too long.\n");
        free(path_copy);
        return -1;
    }
    free(path_copy);

    // Sequentially create each directory component.
    char *p = full_path;
    if (*p == '/')
        p++;

    while ((p = strchr(p, '/'))) {
        *p = '\0';
        if (mkdir(full_path, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "Error creating directory '%s': %s\n", full_path, strerror(errno));
            *p = '/';
            return -1;
        }
        *p = '/';
        p++;
    }

    // Create the final directory component.
    if (mkdir(full_path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating directory '%s': %s\n", full_path, strerror(errno));
        return -1;
    }

    return 0;
}

static int cli_new_file(const char *path, const munbox_file_info_t *file_info, void **out_user_data) {
    if (g_cli_options->verbose) {
        printf("Extracting: %s", path);
        printf("\n");
    }

    if (ensure_dir_exists(path) != 0) {
        return MUNBOX_ABORT;
    }

    char full_path[1024];
    if (snprintf(full_path, sizeof(full_path), "%s/%s", g_cli_options->output_dir, path) >= (int)sizeof(full_path)) {
        fprintf(stderr, "Error: Output path is too long: %s/%s\n", g_cli_options->output_dir, path);
        return MUNBOX_ABORT;
    }

    if (g_cli_options->use_apple_double) {
        // Create AppleDouble format: data file + header file
        appledouble_user_data_t *user_data = malloc(sizeof(appledouble_user_data_t));
        if (!user_data) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return MUNBOX_ABORT;
        }

        user_data->base_path = strdup(full_path);
        user_data->has_resource_fork = false;
        user_data->resource_fork_data = NULL;
        user_data->resource_fork_size = 0;
        if (file_info) {
            user_data->file_info = *file_info;
        } else {
            memset(&user_data->file_info, 0, sizeof(munbox_file_info_t));
        }

        // Open data file
        user_data->data_file = fopen(full_path, "wb");
        if (!user_data->data_file) {
            fprintf(stderr, "Error opening data file '%s': %s\n", full_path, strerror(errno));
            free(user_data->base_path);
            free(user_data);
            return MUNBOX_ABORT;
        }

        // Create header file path (prefix with ._)
        char header_path[1024];
        char *dir_part = strdup(full_path);
        char *file_part = strdup(full_path);
        char *dir = dirname(dir_part);
        char *filename = basename(file_part);

        if (snprintf(header_path, sizeof(header_path), "%s/._%s", dir, filename) >= (int)sizeof(header_path)) {
            fprintf(stderr, "Error: Header path is too long\n");
            fclose(user_data->data_file);
            free(user_data->base_path);
            free(user_data);
            free(dir_part);
            free(file_part);
            return MUNBOX_ABORT;
        }

        free(dir_part);
        free(file_part);

        // Store the header path for later resource fork updates
        user_data->header_path = strdup(header_path);

        printf("Creating AppleDouble header file: %s\n", header_path);

        // Create AppleDouble header file
        if (create_appledouble_header(header_path, file_info) != 0) {
            fprintf(stderr, "Error creating AppleDouble header file '%s': %s\n", header_path, strerror(errno));
            fclose(user_data->data_file);
            free(user_data->base_path);
            free(user_data);
            return MUNBOX_ABORT;
        }

        *out_user_data = user_data;
        return 0;
    } else {
        // Standard single-file output
        FILE *f = fopen(full_path, "wb");
        if (!f) {
            fprintf(stderr, "Error opening output file '%s': %s\n", full_path, strerror(errno));
            return MUNBOX_ABORT;
        }

        *out_user_data = f;
        return 0;
    }
}

static int cli_write_data(void *user_data, const void *buf, size_t cnt) {

    /* debug removed */

    if (g_cli_options->use_apple_double) {
        appledouble_user_data_t *ad_data = (appledouble_user_data_t *)user_data;
        if (fwrite(buf, 1, cnt, ad_data->data_file) != cnt) {
            fprintf(stderr, "Error writing to data file: %s\n", strerror(errno));
            return MUNBOX_ABORT;
        }
    } else {
        FILE *f = (FILE *)user_data;
        if (fwrite(buf, 1, cnt, f) != cnt) {
            fprintf(stderr, "Error writing to output file: %s\n", strerror(errno));
            return MUNBOX_ABORT;
        }
    }
    return 0;
}

static int cli_write_resource_fork(void *user_data, const void *buf, size_t cnt) {
    if (g_cli_options->use_apple_double) {
        appledouble_user_data_t *ad_data = (appledouble_user_data_t *)user_data;

        if (g_cli_options->verbose) {
            printf("  Writing resource fork (%zu bytes)\n", cnt);
        }

        // Update the AppleDouble header with actual resource fork data
        if (update_appledouble_header_with_resource_fork(ad_data->header_path, (const uint8_t *)buf, cnt) != 0) {
            fprintf(stderr, "Error updating AppleDouble header with resource fork: %s\n", strerror(errno));
            return MUNBOX_ABORT;
        }

        ad_data->has_resource_fork = true;
    }
    // For non-AppleDouble mode, resource fork is ignored
    return 0;
}

static int cli_end_file(void *user_data) {
    if (g_cli_options->use_apple_double) {
        appledouble_user_data_t *ad_data = (appledouble_user_data_t *)user_data;
        if (ad_data->data_file) {
            fclose(ad_data->data_file);
        }
        free(ad_data->base_path);
        free(ad_data->header_path);
        free(ad_data);
    } else {
        FILE *f = (FILE *)user_data;
        if (f) {
            fclose(f);
        }
    }
    return 0;
}

// --- Main Processing Logic ---

static int process_file(const char *filepath) {
    printf("Processing '%s'...\n", filepath);

    // Build an input layer.
    munbox_layer_t *current = munbox_new_file_layer(filepath);
    if (!current) {
        fprintf(stderr, "munbox: %s\n", munbox_last_error());
        return 1;
    }

    // Handler search order mirrors the core: SIT -> HQX -> BIN -> CPT
    // We'll iteratively add transform layers (HQX/BIN) and, if SIT/CPT is recognized,
    // delegate extraction to that archive layer. Otherwise, we will fall back to
    // open()/read iteration to dump the final transformed stream to disk.

    bool restarted;
    do {
        restarted = false;

        // Try SIT (archive): iterate with open/read instead of extract()
        munbox_layer_t *maybe = munbox_new_sit_layer(current);
        if (maybe) {
            int status = 0;
            if (maybe->open) {
                void *user_data = NULL;
                char current_name[256] = "";
                bool have_open_file = false;
                munbox_file_info_t info;
                int orc = maybe->open(maybe, MUNBOX_OPEN_FIRST, &info);
                if (g_cli_options->verbose) {
                    printf("[SIT] open(FIRST) rc=%d, read=%s\n", orc, maybe->read ? "set" : "NULL");
                }
                if (orc < 0) {
                    status = 1;
                }
                if (orc == 1 && !maybe->read) {
                    if (g_cli_options->verbose)
                        printf("[SIT] read not set after open()\n");
                    status = 1;
                }
                while (orc == 1 && status == 0) {
                    // Start a new output file if filename changed
                    if (!have_open_file || strcmp(current_name, info.filename) != 0) {
                        if (have_open_file) {
                            if (cli_end_file(user_data) != 0) {
                                status = 1;
                                break;
                            }
                            have_open_file = false;
                            user_data = NULL;
                        }
                        if (cli_new_file(info.filename, &info, &user_data) != 0) {
                            status = 1;
                            break;
                        }
                        strncpy(current_name, info.filename, sizeof(current_name) - 1);
                        current_name[sizeof(current_name) - 1] = '\0';
                        have_open_file = true;
                    }

                    // Read entire current fork and write it
                    uint8_t *buf = NULL;
                    size_t size = 0, cap = 0;
                    for (;;) {
                        if (cap - size < 64 * 1024) {
                            size_t new_cap = cap ? cap * 2 : 64 * 1024;
                            uint8_t *nb = (uint8_t *)realloc(buf, new_cap);
                            if (!nb) {
                                free(buf);
                                status = 1;
                                break;
                            }
                            buf = nb;
                            cap = new_cap;
                        }
                        ssize_t n = maybe->read(maybe, buf + size, cap - size);
                        if (n < 0) {
                            free(buf);
                            status = 1;
                            break;
                        }
                        if (n == 0)
                            break;
                        size += (size_t)n;
                    }
                    if (status != 0)
                        break;

                    // Refresh info (fork_type) if available
                    munbox_file_info_t finfo = info;
                    if (finfo.fork_type == (int)MUNBOX_FORK_RESOURCE) {
                        if (cli_write_resource_fork(user_data, buf, size) != 0) {
                            free(buf);
                            status = 1;
                            break;
                        }
                    } else {
                        if (size && cli_write_data(user_data, buf, size) != 0) {
                            free(buf);
                            status = 1;
                            break;
                        }
                    }
                    free(buf);

                    // Next fork/file
                    orc = maybe->open(maybe, MUNBOX_OPEN_NEXT, &info);
                    if (g_cli_options->verbose) {
                        printf("[SIT] open(NEXT) rc=%d\n", orc);
                    }
                }
                if (status == 0 && have_open_file) {
                    if (cli_end_file(user_data) != 0)
                        status = 1;
                }
            } else {
                status = 1;
            }
            maybe->close(maybe);
            if (status != 0) {
                fprintf(stderr, "munbox: extraction failed for '%s': %s\n", filepath, munbox_last_error());
                return 1;
            }
            printf("Successfully extracted '%s'.\n", filepath);
            return 0;
        }

        // Try HQX (transform)
        maybe = munbox_new_hqx_layer(current);
        if (maybe && maybe->read) {
            current = maybe;
            restarted = true;
            continue;
        }

        // Try BIN (transform)
        maybe = munbox_new_bin_layer(current);
        if (maybe && maybe->read) {
            current = maybe;
            restarted = true;
            continue;
        }

        // Try CPT (archive): iterate with open/read instead of extract()
        maybe = munbox_new_cpt_layer(current);
        if (maybe) {
            int status = 0;
            if (maybe->open) {
                void *user_data = NULL;
                char current_name[256] = "";
                bool have_open_file = false;
                munbox_file_info_t info;
                int orc = maybe->open(maybe, MUNBOX_OPEN_FIRST, &info);
                if (g_cli_options->verbose) {
                    printf("[CPT] open(FIRST) rc=%d, read=%s\n", orc, maybe->read ? "set" : "NULL");
                }
                if (orc < 0) {
                    status = 1;
                }
                if (orc == 1 && !maybe->read) {
                    if (g_cli_options->verbose)
                        printf("[CPT] read not set after open()\n");
                    status = 1;
                }
                while (orc == 1 && status == 0) {
                    if (!have_open_file || strcmp(current_name, info.filename) != 0) {
                        if (have_open_file) {
                            if (cli_end_file(user_data) != 0) {
                                status = 1;
                                break;
                            }
                            have_open_file = false;
                            user_data = NULL;
                        }
                        if (cli_new_file(info.filename, &info, &user_data) != 0) {
                            status = 1;
                            break;
                        }
                        strncpy(current_name, info.filename, sizeof(current_name) - 1);
                        current_name[sizeof(current_name) - 1] = '\0';
                        have_open_file = true;
                    }

                    uint8_t *buf = NULL;
                    size_t size = 0, cap = 0;
                    for (;;) {
                        if (cap - size < 64 * 1024) {
                            size_t new_cap = cap ? cap * 2 : 64 * 1024;
                            uint8_t *nb = (uint8_t *)realloc(buf, new_cap);
                            if (!nb) {
                                free(buf);
                                status = 1;
                                break;
                            }
                            buf = nb;
                            cap = new_cap;
                        }
                        ssize_t n = maybe->read(maybe, buf + size, cap - size);
                        if (n < 0) {
                            free(buf);
                            status = 1;
                            break;
                        }
                        if (n == 0)
                            break;
                        size += (size_t)n;
                    }
                    if (status != 0)
                        break;

                    munbox_file_info_t finfo = info;
                    if (finfo.fork_type == (int)MUNBOX_FORK_RESOURCE) {
                        if (cli_write_resource_fork(user_data, buf, size) != 0) {
                            free(buf);
                            status = 1;
                            break;
                        }
                    } else {
                        if (size && cli_write_data(user_data, buf, size) != 0) {
                            free(buf);
                            status = 1;
                            break;
                        }
                    }
                    free(buf);

                    orc = maybe->open(maybe, MUNBOX_OPEN_NEXT, &info);
                    if (g_cli_options->verbose) {
                        printf("[CPT] open(NEXT) rc=%d\n", orc);
                    }
                }
                if (status == 0 && have_open_file) {
                    if (cli_end_file(user_data) != 0)
                        status = 1;
                }
            } else {
                status = 1;
            }
            maybe->close(maybe);
            if (status != 0) {
                fprintf(stderr, "munbox: extraction failed for '%s': %s\n", filepath, munbox_last_error());
                return 1;
            }
            printf("Successfully extracted '%s'.\n", filepath);
            return 0;
        }
    } while (restarted);

    // Fallback: No archive layer recognized. Use open()/read iteration if available
    // to dump forks in a fork-aware way; otherwise, stream bytes to a single file.

    const char *out_name = "untitled";

    void *user_data = NULL;
    munbox_file_info_t first_info;
    bool used_open = false;
    if (current->open) {
        int rc = current->open(current, MUNBOX_OPEN_FIRST, &first_info);
        if (rc < 0) {
            current->close(current);
            fprintf(stderr, "munbox: %s\n", munbox_last_error());
            return 1;
        }
        if (rc == 1) {
            // Use the filename from first fork if present
            if (first_info.filename[0])
                out_name = first_info.filename;
            // Create destination
            if (cli_new_file(out_name, &first_info, &user_data) != 0) {
                current->close(current);
                return 1;
            }

            // Iterate forks and write appropriately
            used_open = true;
            for (;;) {
                // Read entire current fork
                uint8_t *buf = NULL;
                size_t size = 0, cap = 0;
                for (;;) {
                    if (cap - size < 64 * 1024) {
                        size_t new_cap = cap ? cap * 2 : 64 * 1024;
                        uint8_t *nb = (uint8_t *)realloc(buf, new_cap);
                        if (!nb) {
                            free(buf);
                            current->close(current);
                            cli_end_file(user_data);
                            fprintf(stderr, "munbox: out of memory\n");
                            return 1;
                        }
                        buf = nb;
                        cap = new_cap;
                    }
                    ssize_t n = current->read(current, buf + size, cap - size);
                    if (n < 0) {
                        free(buf);
                        current->close(current);
                        cli_end_file(user_data);
                        fprintf(stderr, "munbox: %s\n", munbox_last_error());
                        return 1;
                    }
                    if (n == 0)
                        break;
                    size += (size_t)n;
                }

                // Dispatch based on fork type
                munbox_file_info_t cur_info = first_info;
                if (cur_info.fork_type == (int)MUNBOX_FORK_RESOURCE) {
                    // Resource fork: update AppleDouble header
                    if (cli_write_resource_fork(user_data, buf, size) != 0) {
                        free(buf);
                        current->close(current);
                        cli_end_file(user_data);
                        return 1;
                    }
                } else {
                    // Data fork: stream to data file
                    if (size) {
                        if (cli_write_data(user_data, buf, size) != 0) {
                            free(buf);
                            current->close(current);
                            cli_end_file(user_data);
                            return 1;
                        }
                    }
                }
                free(buf);

                // Next fork
                munbox_file_info_t next_info;
                int more = current->open(current, MUNBOX_OPEN_NEXT, &next_info);
                if (more < 0) {
                    current->close(current);
                    cli_end_file(user_data);
                    fprintf(stderr, "munbox: %s\n", munbox_last_error());
                    return 1;
                }
                if (more == 0)
                    break; // done
                first_info = next_info;
            }

            if (cli_end_file(user_data) != 0) {
                current->close(current);
                return 1;
            }
        }
    }

    if (!used_open) {
        // Single-stream fallback
        if (cli_new_file(out_name, NULL, &user_data) != 0) {
            current->close(current);
            return 1;
        }
        char buffer[4096];
        ssize_t n;
        while ((n = current->read(current, buffer, sizeof buffer)) > 0) {
            if (cli_write_data(user_data, buffer, (size_t)n) != 0) {
                cli_end_file(user_data);
                current->close(current);
                return 1;
            }
        }
        if (n < 0) {
            cli_end_file(user_data);
            current->close(current);
            fprintf(stderr, "munbox: %s\n", munbox_last_error());
            return 1;
        }
        if (cli_end_file(user_data) != 0) {
            current->close(current);
            return 1;
        }
    }

    current->close(current);
    printf("Successfully extracted '%s'.\n", filepath);
    return 0;
}

void print_usage(const char *prog_name) {
    printf("Usage: %s [options] <archive1> [<archive2> ...]\n", prog_name);
    printf("Unpacks classic Macintosh archives like StuffIt (.sit) and BinHex (.hqx).\n\n");
    printf("Options:\n");
    printf("  -o, --output-dir <dir>  Extract files to a specific directory (default: .)\n");
    printf("  -a, --apple-double      Use AppleDouble format for preserving Mac metadata\n");
    printf("                          Creates ._filename header files with resource forks\n");
    printf("  -v, --verbose           Enable verbose output\n");
    printf("  -h, --help              Show this help message\n");
}

int main(int argc, char *argv[]) {
    cli_options_t opts = {.use_apple_double = false, .output_dir = ".", .verbose = false};

    // Set the global options pointer for the callbacks to use.
    g_cli_options = &opts;

    const struct option long_options[] = {{"output-dir", required_argument, 0, 'o'},
                                          {"apple-double", no_argument, 0, 'a'},
                                          {"verbose", no_argument, 0, 'v'},
                                          {"help", no_argument, 0, 'h'},
                                          {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "o:avh", long_options, NULL)) != -1) {
        switch (c) {
        case 'o':
            opts.output_dir = optarg;
            break;
        case 'a':
            opts.use_apple_double = true;
            break;
        case 'v':
            opts.verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case '?':
            return EXIT_FAILURE;
        default:
            abort();
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "munbox: no input files specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Ensure output directory exists before processing files.
    if (mkdir(opts.output_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "munbox: could not create output directory '%s': %s\n", opts.output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    int overall_status = 0;
    for (int i = optind; i < argc; i++) {
        if (process_file(argv[i]) != 0) {
            overall_status = 1;
        }
    }

    return (overall_status == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}