# Munbox Project Style Guide

This document describes the coding, formatting, and documentation conventions for the Munbox project. All contributors should follow these guidelines to ensure consistency and readability.

## C Source Code

### General Formatting

- Use the formatting rules specified in `.clang-format`

### Naming Conventions

- Use `snake_case` for identifiers: `parse_header()`, `file_info`.
- Constants and macros are `ALL_CAPS_WITH_UNDERSCORES`.
- Prefix internal/private functions with `static` and, if needed, a module prefix: `static int hqx_decode_byte(...)`.

### Comments

- Use Doxygen-style comments for public APIs *.h:
  ```c
  /**
   * @brief Brief description.
   * @param param Description.
   * @return Description.
   */
  ```
- Don't use Doxygen-style comments for code that is not public APIs.
- Use `//` for short, inline comments.
- Keep comments up-to-date and relevant.
- For every significant statement or calculation, add a concise one-line comment explaining the purpose or reasoning behind the code—not just restating what the code does, but *why* it is done. This helps future readers understand the intent.
  - Example:
    ```c
    static int sum_positive(int *arr, int n) {
        int sum = 0;
        for (int i = 0; i < n; ++i) {
            // Only add positive numbers to the sum
            if (arr[i] > 0)
                sum += arr[i];
        }
        return sum;
    }
    ```
- Every function should have a one-line comment immediately above it describing its overall purpose or effect, not just repeating the function name or signature. Example:
    ```c
    // Copies up to 'cap' bytes from 'src' to 'dst'
    static size_t copy_bytes(const uint8_t *src, uint8_t *dst, size_t cap) {
        size_t copied = 0;
        while (copied < cap && src[copied]) {
            dst[copied] = src[copied];
            copied++;
        }
        return copied;
    }
    ```
- Every structure definition should have a one-line comment immediately above it describing its purpose or what it represents. Example:
    ```c
    // Holds configuration options for the parser
    struct parser_config {
        int max_depth;
        bool strict_mode;
    };
    ```
- Leave at least one completely empty/blank line between consecutive function and struct declarations. This improves visual separation and makes it easier to spot declaration boundaries.

- In normal `.c` files (excluding *public* headers), prefer `//` for one-line and inline comments rather than `/* ... */`. Reserve `/* ... */` for multi-line comments or the file license/header block; public headers may still use Doxygen-style block comments for API documentation.

- **In general, do not remove or change existing comments, unless they are clearly wrong, or if you are fixing language errors or formatting.**

### File Structure

- Every file must begin with a one-line SPDX license identifier at the very top:
    ```c
    // SPDX-License-Identifier: MIT
    ```
- Immediately after the SPDX identifier, include the MIT license header (should be followed EXACTLY):
    ```c
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
    ```
- Each file starts with a brief file-level comment describing its purpose. Example:
    ```c
    // foo.c
    // Implements the Foo module for Munbox.
    ```
- Include guards in all header files. Example:
    ```c
    #ifndef FOO_H
    #define FOO_H

    // ...header contents...

    #endif // FOO_H
    ```
- Group includes: standard library first, then project headers.
- Functions should be ordered: public API first, then static/internal helpers.

### Other Conventions

- Avoid magic numbers; use named constants.
- Use `bool`, `true`, `false` from `<stdbool.h>`.
- Free all allocated memory; avoid leaks.

---

## Markdown Documentation

### <a name="md-general-formatting"></a>General Formatting

- Use [CommonMark](https://commonmark.org/) compliant Markdown.
- Limit lines to 120 characters for readability.
- Use spaces, not tabs, for indentation.

### Headings

- Use `#` for top-level, `##` for sections, `###` for subsections.
- Leave a blank line before and after headings.

### Lists and Code Blocks

- Use `-` or `*` for unordered lists, `1.` for ordered lists.
- Indent code blocks with triple backticks and specify language when possible:
  ````markdown
  ```c
  int main(void) { return 0; }
  ```
````
