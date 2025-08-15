#!/bin/bash

# Simple munbox test runner
# A minimal shell script replacement for the complex Python test system

# Default values
TEST_DIR="testfiles"
MUNBOX="../build/cmd/munbox"
OUTPUT_DIR="/tmp/munbox_test"
VERBOSE=false
KEEP_FILES=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Usage function
usage() {
    cat << EOF
Usage: $0 [options]

Options:
    --test-dir <dir>     Directory containing test cases (default: testfiles)
    --munbox <path>      Path to munbox executable (default: ../build/cmd/munbox)
    --output-dir <dir>   Temporary directory for test outputs (default: /tmp/munbox_test)
    --verbose            Enable verbose output
    --keep-files         Keep extracted files after testing (for debugging)
    --help               Show this help message

Test cases are subdirectories in the test directory that contain:
- testfile.* (input archive)
- md5sums.txt (expected checksums)

EOF
}

# Parse command line arguments
SINGLE_TEST=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --test-dir)
            TEST_DIR="$2"
            shift 2
            ;;
        --munbox)
            MUNBOX="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --keep-files)
            KEEP_FILES=true
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        --*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            # First non-option argument is the test case name
            if [[ -z "$SINGLE_TEST" ]]; then
                SINGLE_TEST="$1"
                shift
            else
                echo "Unknown extra argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
done

# Convert to absolute paths
TEST_DIR=$(realpath "$TEST_DIR")
MUNBOX=$(realpath "$MUNBOX")
OUTPUT_DIR=$(realpath "$OUTPUT_DIR")

# Validation
if [[ ! -d "$TEST_DIR" ]]; then
    echo -e "${RED}Error: Test directory not found: $TEST_DIR${NC}"
    exit 1
fi

if [[ ! -f "$MUNBOX" ]]; then
    echo -e "${RED}Error: munbox executable not found: $MUNBOX${NC}"
    echo "Please build the project first: cd .. && cmake --build build"
    exit 1
fi

if [[ ! -x "$MUNBOX" ]]; then
    echo -e "${RED}Error: munbox is not executable: $MUNBOX${NC}"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Find test cases
echo "Discovering test cases..."
test_cases=()
for dir in "$TEST_DIR"/*; do
    if [[ -d "$dir" ]]; then
        testname=$(basename "$dir")
        testfile_found=false
        # Look for testfile.*
        for file in "$dir"/testfile.*; do
            if [[ -f "$file" ]]; then
                testfile_found=true
                break
            fi
        done
        # Check for md5sums.txt
        if [[ "$testfile_found" = true ]] && [[ -f "$dir/md5sums.txt" ]]; then
            test_cases+=("$testname")
        elif [[ "$VERBOSE" = true ]]; then
            echo "Skipping $testname: missing testfile.* or md5sums.txt"
        fi
    fi
done

# If a single test is specified, filter test_cases
if [[ -n "$SINGLE_TEST" ]]; then
    found=false
    for t in "${test_cases[@]}"; do
        if [[ "$t" == "$SINGLE_TEST" ]]; then
            test_cases=("$SINGLE_TEST")
            found=true
            break
        fi
    done
    if [[ "$found" = false ]]; then
        echo -e "${RED}Test case not found: $SINGLE_TEST${NC}"
        exit 1
    fi
fi

if [[ ${#test_cases[@]} -eq 0 ]]; then
    echo -e "${RED}No test cases found!${NC}"
    exit 1
fi

if [[ -n "$SINGLE_TEST" ]]; then
    echo "Found test case: $SINGLE_TEST"
else
    echo "Found ${#test_cases[@]} test cases"
fi

# Test execution
passed=0
failed=0
failed_tests=()

for testname in "${test_cases[@]}"; do
    test_dir="$TEST_DIR/$testname"
    test_output_dir="$OUTPUT_DIR/$testname"
    
    if [[ "$VERBOSE" = true ]]; then
        echo "Running test: $testname"
    fi
    
    # Find input file
    input_file=""
    for file in "$test_dir"/testfile.*; do
        if [[ -f "$file" ]]; then
            if [[ -n "$input_file" ]]; then
                echo -e "${RED}  FAIL: $testname - Multiple testfiles found${NC}"
                ((failed++))
                failed_tests+=("$testname: Multiple testfiles found")
                continue 2
            fi
            input_file="$file"
        fi
    done
    
    if [[ -z "$input_file" ]]; then
        echo -e "${RED}  FAIL: $testname - No testfile found${NC}"
        ((failed++))
        failed_tests+=("$testname: No testfile found")
        continue
    fi
    
    # Create clean output directory
    rm -rf "$test_output_dir"
    mkdir -p "$test_output_dir"
    
    # Run munbox
    start_time=$(date +%s)
    
    munbox_cmd=("$MUNBOX")
    if [[ "$VERBOSE" = true ]]; then
        munbox_cmd+=("-v")
    fi
    munbox_cmd+=("-a" "-o" "$test_output_dir" "$input_file")
    
    if [[ "$VERBOSE" = true ]]; then
        echo "  Command: ${munbox_cmd[*]}"
    fi
    
    if ! output=$("${munbox_cmd[@]}" 2>&1); then
        end_time=$(date +%s)
        execution_time=$((end_time - start_time))
        echo -e "${RED}  FAIL: $testname (${execution_time}s) - munbox failed${NC}"
        if [[ "$VERBOSE" = true ]]; then
            echo "---- munbox output ----"
            echo "$output"
            echo "----------------------"
        fi
        ((failed++))
        failed_tests+=("$testname: munbox execution failed")
        continue
    fi

    end_time=$(date +%s)
    execution_time=$((end_time - start_time))

    if [[ "$VERBOSE" = true ]]; then
        echo "---- munbox output ----"
        echo "$output"
        echo "----------------------"
    fi

    # Copy md5sums into output if referenced as ./md5sums.txt in list
    if grep -qE "[[:space:]]+\./md5sums.txt$" "$test_dir/md5sums.txt"; then
        cp "$test_dir/md5sums.txt" "$test_output_dir/md5sums.txt" 2>/dev/null || true
    fi

    # Validate checksums using md5sum -c from inside output dir
    if (cd "$test_output_dir" && md5sum -c "$test_dir/md5sums.txt" >/dev/null 2>&1); then
        echo -e "${GREEN}  PASS: $testname (${execution_time}s)${NC}"
        ((passed++))
    else
        echo -e "${RED}  FAIL: $testname (${execution_time}s) - checksum validation failed${NC}"

        if [[ "$VERBOSE" = true ]]; then
            echo "    Expected files:"
            while IFS= read -r line; do
                if [[ -n "$line" ]] && [[ ! "$line" =~ ^[[:space:]]*# ]]; then
                    filename=$(echo "$line" | awk '{print $2}')
                    if [[ -n "$filename" ]]; then
                        echo "      $filename"
                    fi
                fi
            done < "$test_dir/md5sums.txt"

            echo "    Actual files:"
            if [[ -d "$test_output_dir" ]]; then
                (cd "$test_output_dir" && find . -type f | sort | sed 's/^/      /')
            fi

            echo "    Checksum validation output:"
            (cd "$test_output_dir" && md5sum -c "$test_dir/md5sums.txt" 2>&1 | head -10) || true
        fi

        ((failed++))
        failed_tests+=("$testname: checksum validation failed")
    fi
    
    # Clean up unless keeping files
    if [[ "$KEEP_FILES" != true ]]; then
        rm -rf "$test_output_dir"
    fi
done

# Summary
total=$((passed + failed))
success_rate=0
if [[ $total -gt 0 ]]; then
    success_rate=$((passed * 100 / total))
fi

echo
echo "============================================================"
echo "TEST SUMMARY"
echo "============================================================"
echo "Total tests:     $total"
echo "Passed:          $passed"
echo "Failed:          $failed"
echo "Success rate:    ${success_rate}%"

if [[ $failed -gt 0 ]]; then
    echo
    echo "FAILED TESTS:"
    echo "------------------------------------------------------------"
    for failure in "${failed_tests[@]}"; do
        echo "  $failure"
    done
    echo
    echo "For detailed error information, run with --verbose"
    if [[ "$KEEP_FILES" != true ]]; then
        echo "To examine output files, run with --keep-files"
    fi
fi

# Exit with appropriate code
if [[ $failed -eq 0 ]]; then
    exit 0
else
    exit 1
fi