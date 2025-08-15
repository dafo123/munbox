#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <math.h>

#define FILE_SIZE (20 * 1024)  // 20KB

void generate_alternating_pattern(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating alternating pattern data: %s\n", filename);
    
    // Create patterns that challenge LZSS but have no runs for RLE
    for (size_t i = 0; i < FILE_SIZE; i++) {
        uint8_t byte;
        int pattern = (i / 100) % 4;
        
        switch (pattern) {
            case 0: byte = (i % 2) ? 0xAA : 0x55; break;  // Alternating bits
            case 1: byte = (i % 4) == 0 ? 0x00 : (i % 4) == 1 ? 0xFF : (i % 4) == 2 ? 0x00 : 0xFF; break;
            case 2: byte = i % 256; break;  // Sequential
            case 3: byte = ((i * 17) + 42) % 256; break;  // Pseudo-random sequence
        }
        fputc(byte, f);
    }
    
    fclose(f);
    printf("Generated %s with alternating patterns\n", filename);
}

void generate_text_like_data(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating text-like data: %s\n", filename);
    
    // Simulate text data with common ASCII patterns
    const char *words[] = {"the", "and", "for", "are", "but", "not", "you", "all", "can", "had", "her", "was", "one", "our", "out", "day", "get", "has", "him", "his", "how", "man", "new", "now", "old", "see", "two", "way", "who", "boy", "did", "its", "let", "put", "say", "she", "too", "use"};
    int num_words = sizeof(words) / sizeof(words[0]);
    
    size_t written = 0;
    while (written < FILE_SIZE) {
        // Write a word
        const char *word = words[rand() % num_words];
        size_t word_len = strlen(word);
        
        if (written + word_len + 1 > FILE_SIZE) break;
        
        fwrite(word, 1, word_len, f);
        written += word_len;
        
        // Add space or punctuation
        char separator = (rand() % 10 == 0) ? '\n' : (rand() % 20 == 0) ? '.' : ' ';
        fputc(separator, f);
        written++;
    }
    
    fclose(f);
    printf("Generated %s with text-like patterns\n", filename);
}

void generate_sparse_data(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating sparse data: %s\n", filename);
    
    // Mostly zeros with occasional data - common in binary files
    for (size_t i = 0; i < FILE_SIZE; i++) {
        uint8_t byte;
        
        if (rand() % 50 == 0) {  // 2% chance of non-zero
            byte = 1 + (rand() % 255);  // Any byte except 0
        } else {
            byte = 0x00;  // Mostly zeros
        }
        
        fputc(byte, f);
    }
    
    fclose(f);
    printf("Generated %s with sparse (mostly zero) data\n", filename);
}

void generate_escape_sequence_heavy(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating escape sequence heavy data: %s\n", filename);
    
    // Data with lots of 0x81 and 0x82 bytes to test RLE edge cases
    for (size_t i = 0; i < FILE_SIZE; i++) {
        uint8_t byte;
        
        int choice = rand() % 100;
        if (choice < 20) {
            byte = 0x81;  // 20% chance of RLE escape byte 1
        } else if (choice < 30) {
            byte = 0x82;  // 10% chance of RLE escape byte 2
        } else if (choice < 35) {
            byte = 0x00;  // 5% chance of null
        } else {
            byte = 32 + (rand() % 95);  // Printable ASCII
        }
        
        fputc(byte, f);
    }
    
    fclose(f);
    printf("Generated %s with heavy escape sequences (0x81/0x82)\n", filename);
}

void generate_binary_structure(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating binary structure data: %s\n", filename);
    
    // Simulate binary file with headers, padding, and data blocks
    size_t written = 0;
    
    while (written < FILE_SIZE) {
        // Write a "header" - 16 bytes with specific pattern
        uint8_t header[16] = {0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00,
                              0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
        size_t to_write = (written + 16 > FILE_SIZE) ? FILE_SIZE - written : 16;
        fwrite(header, 1, to_write, f);
        written += to_write;
        if (written >= FILE_SIZE) break;
        
        // Write some "data" - 50-200 bytes of varied content
        size_t data_size = 50 + (rand() % 151);
        if (written + data_size > FILE_SIZE) data_size = FILE_SIZE - written;
        
        for (size_t i = 0; i < data_size; i++) {
            uint8_t byte = (i + written) % 256;
            fputc(byte, f);
        }
        written += data_size;
        
        // Write padding - 4-16 zero bytes
        size_t pad_size = 4 + (rand() % 13);
        if (written + pad_size > FILE_SIZE) pad_size = FILE_SIZE - written;
        
        for (size_t i = 0; i < pad_size; i++) {
            fputc(0x00, f);
        }
        written += pad_size;
    }
    
    fclose(f);
    printf("Generated %s with binary structure patterns\n", filename);
}

void generate_gradual_change(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating gradual change data: %s\n", filename);
    
    // Data that changes gradually - tests LZSS window effectiveness
    double phase = 0.0;
    for (size_t i = 0; i < FILE_SIZE; i++) {
        // Slowly evolving waveform
        uint8_t byte = (uint8_t)(128 + 127 * sin(phase) * sin(phase * 0.1));
        fputc(byte, f);
        phase += 0.01;
    }
    
    fclose(f);
    printf("Generated %s with gradually changing patterns\n", filename);
}

void analyze_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open file for analysis");
        return;
    }
    
    uint32_t byte_counts[256] = {0};
    uint32_t run_counts = 0;
    uint32_t max_run = 0;
    uint32_t current_run = 1;
    uint32_t escape_81_count = 0;
    uint32_t escape_82_count = 0;
    uint32_t escape_pairs = 0;
    int prev_byte = -1;
    int byte;
    size_t total_bytes = 0;
    
    while ((byte = fgetc(f)) != EOF) {
        byte_counts[byte]++;
        total_bytes++;
        
        if (byte == 0x81) escape_81_count++;
        if (byte == 0x82) escape_82_count++;
        if (prev_byte == 0x81 && byte == 0x82) escape_pairs++;
        
        if (byte == prev_byte) {
            current_run++;
        } else {
            if (current_run > 1) {
                run_counts++;
                if (current_run > max_run) {
                    max_run = current_run;
                }
            }
            current_run = 1;
        }
        prev_byte = byte;
    }
    
    // Handle final run
    if (current_run > 1) {
        run_counts++;
        if (current_run > max_run) {
            max_run = current_run;
        }
    }
    
    fclose(f);
    
    // Calculate distribution stats
    uint32_t unique_bytes = 0;
    uint32_t most_common_count = 0;
    uint32_t zero_count = byte_counts[0];
    for (int i = 0; i < 256; i++) {
        if (byte_counts[i] > 0) {
            unique_bytes++;
            if (byte_counts[i] > most_common_count) {
                most_common_count = byte_counts[i];
            }
        }
    }
    
    printf("\nAnalysis of %s:\n", filename);
    printf("  Total bytes: %zu\n", total_bytes);
    printf("  Unique byte values: %u/256\n", unique_bytes);
    printf("  Most common byte appears: %u times (%.1f%%)\n", 
           most_common_count, (most_common_count * 100.0) / total_bytes);
    printf("  Zero bytes: %u (%.1f%%)\n", zero_count, (zero_count * 100.0) / total_bytes);
    printf("  0x81 bytes: %u, 0x82 bytes: %u, 0x81-0x82 pairs: %u\n", 
           escape_81_count, escape_82_count, escape_pairs);
    printf("  Runs (2+ consecutive): %u, max run: %u, density: %.2f/1000\n", 
           run_counts, max_run, (run_counts * 1000.0) / total_bytes);
}

int main() {
    srand(time(NULL));
    
    printf("Generating additional test data patterns...\n\n");
    
    generate_alternating_pattern("/tmp/test_alternating.bin");
    generate_text_like_data("/tmp/test_textlike.bin");
    generate_sparse_data("/tmp/test_sparse.bin");
    generate_escape_sequence_heavy("/tmp/test_escapes.bin");
    generate_binary_structure("/tmp/test_binary.bin");
    generate_gradual_change("/tmp/test_gradual.bin");
    
    printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" 
           "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    
    analyze_file("/tmp/test_alternating.bin");
    analyze_file("/tmp/test_textlike.bin");
    analyze_file("/tmp/test_sparse.bin");
    analyze_file("/tmp/test_escapes.bin");
    analyze_file("/tmp/test_binary.bin");
    analyze_file("/tmp/test_gradual.bin");
    
    printf("\nAdditional test files created in /tmp/:\n");
    printf("  test_alternating.bin - Alternating bit patterns (LZSS friendly, RLE hostile)\n");
    printf("  test_textlike.bin    - Text-like data with repeated words\n");
    printf("  test_sparse.bin      - Sparse data (mostly zeros)\n");
    printf("  test_escapes.bin     - Heavy 0x81/0x82 sequences (RLE edge cases)\n");
    printf("  test_binary.bin      - Binary file structure with headers/padding\n");
    printf("  test_gradual.bin     - Gradually changing patterns\n");
    
    return 0;
}
