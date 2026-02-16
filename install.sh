#!/bin/bash

# Lync Compiler Installer for Unix-like systems
# This script builds and installs Lync to the system.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Lync Compiler Installer v0.3.2${NC}"
echo -e "${BLUE}========================================${NC}"
echo

# Check if running with sudo/root privileges
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This installer must be run as root/sudo!${NC}"
    echo "Please run: sudo ./install.sh"
    echo
    exit 1
fi

# Determine script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Check for dependencies
echo -e "${BLUE}Checking dependencies...${NC}"
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}ERROR: cmake is not installed.${NC}"
    exit 1
fi

if ! command -v make &> /dev/null && ! command -v ninja &> /dev/null; then
    echo -e "${RED}ERROR: Neither make nor ninja was found.${NC}"
    exit 1
fi

# Build the project
echo -e "${BLUE}Building Lync Compiler...${NC}"
mkdir -p "$BUILD_DIR"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo -e "${BLUE}Compiling...${NC}"
cmake --build "$BUILD_DIR" --config Release

# Install the project
echo -e "${BLUE}Installing to system...${NC}"
cmake --install "$BUILD_DIR"

echo
echo -e "${GREEN}Successfully installed Lync!${NC}"
echo

# Verify PATH
if ! command -v lync &> /dev/null; then
    echo -e "${YELLOW}WARNING: 'lync' is not in your PATH.${NC}"
    echo "You may need to add /usr/local/bin to your PATH environment variable."
    echo "Add this to your .bashrc or .zshrc:"
    echo "  export PATH=\$PATH:/usr/local/bin"
else
    echo -e "${GREEN}Lync is ready to use!${NC}"
fi

echo
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Installation Complete!${NC}"
echo -e "${BLUE}========================================${NC}"
echo
echo "To verify installation, run: lync --help"
echo
