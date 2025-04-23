#!/bin/bash
#
# Gaia OS Restart Script
# This script restarts the main.gaia process with symbolic number representation

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
GAIA_BIN_DIR="/opt/gaia/bin"
GAIA_MAIN="main.gaia"
NUMBER_SYSTEM="$PROJECT_ROOT/src/tools/number_system.py"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to convert numbers to symbolic representation
sym() {
    if [ -x "$NUMBER_SYSTEM" ]; then
        "$NUMBER_SYSTEM" "$1"
    else
        echo "$1"
    fi
}

# Print header
echo -e "${GREEN}"
echo "≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡"
echo "           Gaia OS - Service Restart                 "
echo "≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡"
echo -e "${NC}"

# Check if VM detection is available
if [ -f "$PROJECT_ROOT/src/tools/vm_detect.py" ]; then
    IS_VM=$(python3 "$PROJECT_ROOT/src/tools/vm_detect.py" | grep "Running" | grep -ci "virtual machine")
    if [ "$IS_VM" -gt 0 ]; then
        echo -e "${BLUE}Running in virtual machine environment${NC}"
        VM_TYPE=$(python3 "$PROJECT_ROOT/src/tools/vm_detect.py" --optimize | grep "Detected VM type" | cut -d':' -f2 | tr -d ' ')
        echo -e "${BLUE}VM type: $VM_TYPE${NC}"
    fi
fi

# Stopping existing service if running
echo -e "${YELLOW}Checking for running Gaia services...${NC}"
if pgrep -f "$GAIA_MAIN" > /dev/null 2>&1; then
    echo -e "${YELLOW}Stopping existing Gaia service...${NC}"
    pkill -f "$GAIA_MAIN" || true
    sleep 2
fi

# Starting the service
echo -e "${YELLOW}Starting Gaia main service...${NC}"
if [ -f "$GAIA_BIN_DIR/$GAIA_MAIN" ]; then
    # Create log directory if it doesn't exist
    mkdir -p /var/log/gaia
    mkdir -p /var/run

    # Start the service
    nohup "$GAIA_BIN_DIR/$GAIA_MAIN" > /var/log/gaia/main.log 2>&1 &
    PID=$!
    
    # Check if process started successfully
    if ps -p $PID > /dev/null; then
        # Convert PID to symbolic representation
        if [ -x "$NUMBER_SYSTEM" ]; then
            SYM_PID=$(python3 "$NUMBER_SYSTEM" "$PID" | grep "Symbolic:" | cut -d' ' -f2)
            echo -e "${GREEN}Gaia service started successfully with PID: $SYM_PID${NC}"
            
            # Save symbolic PID for reference
            echo "$SYM_PID" > /var/run/gaia.sym
        else
            echo -e "${GREEN}Gaia service started successfully with PID: $PID${NC}"
        fi
    else
        echo -e "${RED}Failed to start Gaia service${NC}"
        exit 1
    fi
else
    echo -e "${RED}Error: $GAIA_BIN_DIR/$GAIA_MAIN not found${NC}"
    echo -e "${YELLOW}Creating directory structure...${NC}"
    
    # Create necessary directories
    mkdir -p "$GAIA_BIN_DIR"
    mkdir -p "/var/log/gaia"
    mkdir -p "/var/run"
    
    echo -e "${RED}Please install Gaia main service before running this script${NC}"
    exit 1
fi

# Report system stats using symbolic numbers
if [ -x "$NUMBER_SYSTEM" ]; then
    echo -e "${BLUE}System Stats (Symbolic Representation):${NC}"
    
    # Memory usage
    MEM_TOTAL=$(free -b | awk '/^Mem:/ {print $2}')
    MEM_USED=$(free -b | awk '/^Mem:/ {print $3}')
    
    SYM_MEM_TOTAL=$(python3 "$NUMBER_SYSTEM" "$MEM_TOTAL" | grep "Symbolic:" | cut -d' ' -f2)
    SYM_MEM_USED=$(python3 "$NUMBER_SYSTEM" "$MEM_USED" | grep "Symbolic:" | cut -d' ' -f2)
    
    echo -e "Memory: $SYM_MEM_USED / $SYM_MEM_TOTAL bytes"
    
    # Disk usage
    DISK_TOTAL=$(df -B1 / | awk 'NR==2 {print $2}')
    DISK_USED=$(df -B1 / | awk 'NR==2 {print $3}')
    
    SYM_DISK_TOTAL=$(python3 "$NUMBER_SYSTEM" "$DISK_TOTAL" | grep "Symbolic:" | cut -d' ' -f2)
    SYM_DISK_USED=$(python3 "$NUMBER_SYSTEM" "$DISK_USED" | grep "Symbolic:" | cut -d' ' -f2)
    
    echo -e "Disk: $SYM_DISK_USED / $SYM_DISK_TOTAL bytes"
fi

echo -e "${GREEN}Done!${NC}"