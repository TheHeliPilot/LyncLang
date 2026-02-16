#!/bin/bash

# Lync Compiler Uninstaller for Unix-like systems

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Lync Compiler Uninstaller${NC}"
echo -e "${BLUE}========================================${NC}"
echo

# Check if running with sudo/root privileges
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This uninstaller must be run as root/sudo!${NC}"
    echo "Please run: sudo ./uninstall.sh"
    echo
    exit 1
fi

echo -e "${BLUE}Removing Lync from system...${NC}"

# Remove binary
if [ -f "/usr/local/bin/lync" ]; then
    rm -f "/usr/local/bin/lync"
    echo "Removed /usr/local/bin/lync"
else
    echo "Binary /usr/local/bin/lync not found."
fi

# Remove libraries
if [ -d "/usr/local/lib/lync" ]; then
    rm -rf "/usr/local/lib/lync"
    echo "Removed /usr/local/lib/lync directory"
else
    echo "Library directory /usr/local/lib/lync not found."
fi

echo
echo -e "${GREEN}Lync has been successfully uninstalled.${NC}"
echo
echo -e "${BLUE}========================================${NC}"
echo
