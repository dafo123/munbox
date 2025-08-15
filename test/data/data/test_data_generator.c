#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#define FILE_SIZE (20 * 1024)  // 20KB

void generate_run_length_data(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating run-length heavy data: %s\n", filename);
    
    size_t written = 0;
    while (written < FILE_SIZE) {
        // Generate runs of 10-200 bytes of the same value
        uint8_t value = rand() % 256;
        int run_length = 10 + (rand() % 191);  // 10-200
        
        // Don't exceed file size
        if (written + run_length > FILE_SIZE) {
            run_length = FILE_SIZE - written;
        }
        
        for (int i = 0; i < run_length; i++) {
            fputc(value, f);
        }
        written += run_length;
        
        // Occasionally add some random bytes to break up runs
        if (rand() % 5 == 0 && written < FILE_SIZE) {
            int random_bytes = 1 + (rand() % 5);
            if (written + random_bytes > FILE_SIZE) {
                random_bytes = FILE_SIZE - written;
            }
            for (int i = 0; i < random_bytes; i++) {
                fputc(rand() % 256, f);
            }
            written += random_bytes;
        }
    }
    
    fclose(f);
    printf("Generated %s with run-length patterns\n", filename);
}

void generate_skewed_distribution(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating skewed distribution data: %s\n", filename);
    
    // Define a set of overrepresented bytes (about 10% of possible values)
    uint8_t common_bytes[] = {0x00, 0x20, 0x41, 0x45, 0x49, 0x4F, 0x55, 0x61, 
                              0x65, 0x69, 0x6F, 0x75, 0x0A, 0x0D, 0x09, 0xFF,
                              0x01, 0x02, 0x03, 0x7F, 0x80, 0x81, 0x82, 0xFE};
    int num_common = sizeof(common_bytes) / sizeof(common_bytes[0]);
    
    for (size_t i = 0; i < FILE_SIZE; i++) {
        uint8_t byte;
        
        if (rand() % 100 < 70) {  // 70% chance of common byte
            byte = common_bytes[rand() % num_common];
        } else {  // 30% chance of any byte
            byte = rand() % 256;
        }
        
        fputc(byte, f);
    }
    
    fclose(f);
    printf("Generated %s with skewed byte distribution\n", filename);
}

void generate_white_noise(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file");
        return;
    }
    
    printf("Generating white noise data: %s\n", filename);
    
    for (size_t i = 0; i < FILE_SIZE; i++) {
        // Use multiple random sources to ensure good distribution
        uint8_t byte = (rand() ^ (rand() << 8) ^ (rand() << 16)) % 256;
        fputc(byte, f);
    }
    
    fclose(f);
    printf("Generated %s with white noise distribution\n", filename);
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
    int prev_byte = -1;
    int byte;
    size_t total_bytes = 0;
    
    while ((byte = fgetc(f)) != EOF) {
        byte_counts[byte]++;
        total_bytes++;
        
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
    
    // Calculate entropy and distribution stats
    uint32_t unique_bytes = 0;
    uint32_t most_common_count = 0;
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
    printf("  Number of runs (2+ consecutive identical bytes): %u\n", run_counts);
    printf("  Maximum run length: %u\n", max_run);
    printf("  Run density: %.2f runs per 1000 bytes\n", (run_counts * 1000.0) / total_bytes);
}

int main() {
    srand(time(NULL));
    
    printf("Generating 3 test files of %d bytes each...\n\n", FILE_SIZE);
    
    generate_run_length_data("/tmp/test_runlength.bin");
    generate_skewed_distribution("/tmp/test_skewed.bin");
    generate_white_noise("/tmp/test_whitenoise.bin");
    
    printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" 
           "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    
    analyze_file("/tmp/test_runlength.bin");
    analyze_file("/tmp/test_skewed.bin");
    analyze_file("/tmp/test_whitenoise.bin");
    
    printf("\nFiles created in /tmp/:\n");
    printf("  test_runlength.bin  - Heavy run-length patterns\n");
    printf("  test_skewed.bin     - Skewed byte distribution\n");
    printf("  test_whitenoise.bin - Random white noise\n");
    
    return 0;
}
