# munbox (Mac Unbox)

[![Build Status](https://github.com/dafo123/munbox/actions/workflows/test.yml/badge.svg)](https://github.com/dafo123/munbox/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C99](https://img.shields.io/badge/std-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)

**munbox** is a modern, portable, and modular C library and command-line tool for unpacking classic Macintosh archive formats such as StuffIt (.sit), BinHex (.hqx), and Compact Pro (.cpt).

The primary goal of this project is **preservation**. Many historical software titles, documents, and creative works from the Macintosh era are stored in these formats. Ensuring continued access to these files is vital — not only for retro computing enthusiasts, but also for digital historians, archivists, and anyone interested in the evolution of computing.

Preservation is achieved by prioritizing portability, avoiding lock-in to specific runtime environments, providing thorough documentation of formats and implementation details, and using a permissive software license. munbox is written entirely in standard C, with no special runtime requirements. All technical details are documented in the `docs/*.md` files, and the project is released under the MIT license.

## Supported Formats

The following formats are supported as of today:

* **BinHex 4.0** (.hqx)  
* **StuffIt** (.sit \- various versions)  
* **Compact Pro** (.cpt)  
* **MacBinary** (.bin)  

##  Acknowledgements and AI Notice

As part of this commitment to preserving knowledge about these old file formats, the `docs/internals` directory contains technical documentation (in markdown) about the internals of these different formats. This information has been gathered from many different sources across the internet, based on the work of numerous individuals. All of the credit is due to the people who have documented, analyzed, and reverse-engineered these formats over decades. munbox does not claim credit for their hard work — instead, it seeks to leverage and build upon that knowledge to create new, open implementations of these algorithms and tools. Wherever possible, we have tried to include references and attribution to the main sources of information under `docs/internals`. This project stands on the shoulders of those who have contributed to the understanding of classic Mac file formats.

Special thanks to Matthew Russotto (for documentation of the sit15 format), Dag Ågren (original author of The Unarchiver), and Stephan Sokolow (for StuffIt test images).

In the development of munbox, AI tools have been used to aid and accelerate both code development and research. AI has also been leveraged to help analyze and in some cases to reverse-engineer these file formats, making it possible to gather, synthesize, and document technical details more efficiently.

## **Building the Project**

The project uses CMake for building the library and the command-line tool.

### **Prerequisites**

* A C99-compliant C compiler (e.g., GCC, Clang, MSVC)  
* CMake (version 3.10 or newer)  
* Make (or another build tool like Ninja)

### **Build Steps (Canonical CMake Workflow)**

1. Clone the repository:  
    git clone https://github.com/dafo123/munbox.git  
    cd munbox
2. Configure:  
    cmake -S . -B build [-DMUNBOX_BUILD_SHARED=ON] [-DMUNBOX_ENABLE_ASAN=ON]
3. Build:  
    cmake --build build -j
4. (Optional) Run tests:  
    ctest --test-dir build --output-on-failure
5. (Optional) Install:  
    cmake --install build --prefix /desired/prefix

Key CMake options:
* MUNBOX_BUILD_CLI=ON/OFF (default ON)
* MUNBOX_BUILD_STATIC=ON/OFF (default ON)
* MUNBOX_BUILD_SHARED=ON/OFF (default OFF)
* MUNBOX_BUILD_TESTS=ON/OFF (default ON)
* MUNBOX_ENABLE_WARNINGS=ON/OFF (default ON)
* MUNBOX_ENABLE_ASAN=ON (requires Debug + Clang/GCC)

### **Convenience Makefile**
You can alternatively just run:
```
make            # configure + build
make test       # run CTest tests
make shell-tests# run legacy shell harness
make install    # install (set DESTDIR or CMAKE_INSTALL_PREFIX via CMake cache first)
```

Artifacts will appear under build/ (e.g. build/lib/libmunbox.a and build/cmd/munbox).

## **Usage**

### **Command-Line Tool**

The munbox tool unpacks one or more archive files into a specified output directory.  
**Syntax:**  
./build/munbox \[options\] \<archive1\> \[archive2...\]

**Example:**  
\# Unpack a BinHex-encoded StuffIt archive into the 'output' directory  
./build/munbox \-o ./output MyArchive.sit.hqx

\# Unpack multiple files  
./build/munbox \-o ./unpacked\_files stuff.sit images.cpt

### **Library API (libmunbox)**

You can use libmunbox in your own C/C++ projects to handle archive data programmatically.  
**Example C Code:**  
\#include \<stdio.h\>  
\#include \<munbox.h\>

*TODO*

## **How to Contribute**

Contributions are welcome\! Whether it's implementing a new format, fixing a bug, or improving documentation, your help is appreciated.

1. **Fork** the repository.  
2. Create a new **branch** for your feature (git checkout \-b feature/new-format-decoder).  
3. **Commit** your changes (git commit \-m 'Add support for Compact Pro').  
4. **Push** to the branch (git push origin feature/new-format-decoder).  
5. Open a **Pull Request**.

## **Disclaimer**

All product names, logos, brands, trademarks, and registered trademarks are property of their respective owners. All company, product, and service names used in this software and its documentation are for identification purposes only. Use of these names, logos, and brands does not imply endorsement.

## **License**

This project is licensed under the MIT License. See the [LICENSE](https://www.google.com/search?q=LICENSE) file for details.