#!/bin/bash

TARGET_DIR="test/source_files"
OUT_DIR="test/source_files/test_manifest.txt"

OUT_FILE=$(basename "$OUT_DIR")
SCRIPT_NAME=$(basename "$0")

find "$TARGET_DIR" -maxdepth 1 -type f \
  ! -name "$OUT_FILE" \
  ! -name "$SCRIPT_NAME" \
  -exec basename {} \; >"$OUT_DIR"

echo "Successfully wrote $(wc -l <"$OUT_DIR") filenames to $OUT_DIR"
