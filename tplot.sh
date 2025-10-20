#!/bin/bash
#
# tplot.sh - Process N most recent log files with threat plotter
#
# Usage: tplot.sh <num_files> [tplot_options...]
#
# Examples:
#   tplot.sh 7              # Process last 7 files with default options
#   tplot.sh 7 -p 5m        # Process last 7 files with 5-minute bins
#   tplot.sh 7 -d 1 -p 5m   # Process last 7 files with debug output
#

# Default log directory
LOG_DIR="logs"

# Path to tplot binary
TPLOT="./src/tplot"

# Check if number argument provided
if [ $# -lt 1 ]; then
    echo "Usage: $0 <num_files> [tplot_options...]" >&2
    echo "" >&2
    echo "Examples:" >&2
    echo "  $0 7              # Process last 7 files" >&2
    echo "  $0 7 -p 5m        # Process last 7 files with 5-minute bins" >&2
    echo "  $0 7 -d 1 -p 5m   # Process last 7 files with debug output" >&2
    exit 1
fi

# Get number of files to process
NUM_FILES="$1"
shift  # Remove first argument, leaving tplot options

# Validate number
if ! [[ "$NUM_FILES" =~ ^[0-9]+$ ]]; then
    echo "Error: Number of files must be a positive integer" >&2
    exit 1
fi

if [ "$NUM_FILES" -lt 1 ]; then
    echo "Error: Number of files must be at least 1" >&2
    exit 1
fi

# Check if log directory exists
if [ ! -d "$LOG_DIR" ]; then
    echo "Error: Log directory '$LOG_DIR' not found" >&2
    exit 1
fi

# Check if tplot binary exists
if [ ! -x "$TPLOT" ]; then
    echo "Error: tplot binary not found at '$TPLOT'" >&2
    echo "Run 'make' to build the project first" >&2
    exit 1
fi

# Find all .gz files in log directory
# Extract number from local.N.gz pattern, sort numerically descending (oldest first)
# Take first N files (oldest to newest order)

echo "Finding $NUM_FILES log files in $LOG_DIR..." >&2

# Build array of files sorted oldest to newest
FILES=()

# Method 1: Try to sort by local.N.gz pattern (where higher N = older)
# This matches files like local.0.gz, local.1.gz, ..., local.18.gz
# Sort -rn gives us descending order: 18, 17, 16... (oldest to newest)
PATTERN_FILES=$(find "$LOG_DIR" -name "local.[0-9]*.gz" -type f 2>/dev/null | \
    sed 's/.*local\.\([0-9]\+\)\.gz/\1 &/' | \
    sort -rn | \
    head -n "$NUM_FILES" | \
    cut -d' ' -f2-)

if [ -n "$PATTERN_FILES" ]; then
    # Found files matching pattern, use them
    while IFS= read -r file; do
        FILES+=("$file")
    done <<< "$PATTERN_FILES"
else
    # Fallback: Sort by file modification time (oldest first)
    echo "No local.N.gz pattern files found, sorting by file time..." >&2

    while IFS= read -r file; do
        FILES+=("$file")
    done < <(find "$LOG_DIR" -name "*.gz" -type f -printf '%T@ %p\n' 2>/dev/null | \
             sort -n | \
             head -n "$NUM_FILES" | \
             cut -d' ' -f2-)
fi

# Check if we found any files
if [ ${#FILES[@]} -eq 0 ]; then
    echo "Error: No .gz files found in $LOG_DIR" >&2
    exit 1
fi

# Display files that will be processed
echo "" >&2
echo "Processing ${#FILES[@]} file(s) (oldest to newest):" >&2
for file in "${FILES[@]}"; do
    echo "  $file" >&2
done
echo "" >&2

# Run tplot with the files and any additional options
echo "Running: $TPLOT $* ${FILES[*]}" >&2
echo "" >&2

exec "$TPLOT" "$@" "${FILES[@]}"
