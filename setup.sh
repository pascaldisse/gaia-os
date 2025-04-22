#!/bin/bash
#
# Gaia OS Setup Script
# This script helps initialize the Gaia OS development environment
# and supports multiple target environments: Asahi Linux, VM, and external boot

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Command line arguments
TARGET=${1:-"all"}  # Options: asahi, vm, external, all (default)

# Print header
echo -e "${GREEN}"
echo "====================================================="
echo "         Gaia OS - Development Environment Setup     "
echo "====================================================="
echo -e "${NC}"
echo -e "${BLUE}Target: ${TARGET}${NC}"

# Detect current OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    # Check if running in a VM
    if grep -q "hypervisor" /proc/cpuinfo || \
       grep -q "VMware" /sys/class/dmi/id/product_name 2>/dev/null || \
       grep -q "QEMU" /sys/class/dmi/id/product_name 2>/dev/null || \
       grep -q "VirtualBox" /sys/class/dmi/id/product_name 2>/dev/null; then
        OS="vm"
    elif grep -q "Asahi" /etc/os-release 2>/dev/null; then
        OS="asahi"
    elif grep -q "Arch" /etc/os-release 2>/dev/null; then
        OS="arch"
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
else
    OS="unknown"
fi

echo -e "Current OS: ${YELLOW}$OS${NC}"

# Create directories if they don't exist
echo -e "${YELLOW}Creating project directories...${NC}"
mkdir -p src/{assistant,tools,models,configs}
mkdir -p scripts
mkdir -p docs
mkdir -p iso/packages
mkdir -p iso/airootfs/etc/skel
mkdir -p boot/{grub,refind}

echo -e "${YELLOW}Making scripts executable...${NC}"
find . -name "*.sh" -exec chmod +x {} \;
find src -name "*.py" -exec chmod +x {} \;

# Add VM-specific files if needed
if [[ "$TARGET" == "vm" || "$TARGET" == "all" ]]; then
    echo -e "${BLUE}Setting up VM support files...${NC}"
    mkdir -p iso/airootfs/etc/X11/xorg.conf.d
    
    # Create placeholder VM detection script if it doesn't exist
    if [ ! -f "src/tools/vm_detect.py" ]; then
        cat > "src/tools/vm_detect.py" << 'EOF'
#!/usr/bin/env python3
# VM Detection utility for Gaia OS

import os
import sys
import subprocess

def is_vm():
    """Check if running in a VM environment"""
    
    # Check CPU info for hypervisor flag
    try:
        with open('/proc/cpuinfo', 'r') as f:
            for line in f:
                if 'hypervisor' in line:
                    return True
    except:
        pass
    
    # Check DMI info
    try:
        dmi_vendors = ['VMware', 'VirtualBox', 'QEMU', 'Parallels']
        dmi_path = '/sys/class/dmi/id/product_name'
        if os.path.exists(dmi_path):
            with open(dmi_path, 'r') as f:
                product = f.read().strip()
                for vendor in dmi_vendors:
                    if vendor in product:
                        return True
    except:
        pass
    
    return False

def get_vm_type():
    """Determine the specific type of VM"""
    
    try:
        dmi_path = '/sys/class/dmi/id/product_name'
        if os.path.exists(dmi_path):
            with open(dmi_path, 'r') as f:
                product = f.read().strip()
                if 'VMware' in product:
                    return 'vmware'
                elif 'VirtualBox' in product:
                    return 'virtualbox'
                elif 'QEMU' in product:
                    return 'qemu'
                elif 'Parallels' in product:
                    return 'parallels'
    except:
        pass
    
    return 'unknown'

def optimize_for_vm():
    """Apply VM-specific optimizations"""
    
    vm_type = get_vm_type()
    print(f"Detected VM type: {vm_type}")
    
    # Apply specific optimizations based on VM type
    # (This would be implemented based on specific VM needs)
    
    print("VM optimizations applied")

if __name__ == '__main__':
    if is_vm():
        print("Running in a virtual machine environment")
        if len(sys.argv) > 1 and sys.argv[1] == '--optimize':
            optimize_for_vm()
    else:
        print("Not running in a virtual machine environment")
EOF
        chmod +x "src/tools/vm_detect.py"
    fi
fi

# Add external boot support if needed
if [[ "$TARGET" == "external" || "$TARGET" == "all" ]]; then
    echo -e "${BLUE}Setting up external boot support files...${NC}"
    mkdir -p boot/efi/EFI/{BOOT,gaia}
    
    # Create rEFInd configuration template if it doesn't exist
    if [ ! -f "boot/refind/refind.conf" ]; then
        cat > "boot/refind/refind.conf" << 'EOF'
# Gaia OS rEFInd configuration

timeout 10
default_selection "Gaia OS"

menuentry "Gaia OS" {
    icon /EFI/BOOT/icons/os_linux.png
    volume GAIA_ROOT
    loader /efi/vmlinuz-linux-asahi
    initrd /efi/initramfs-linux-asahi.img
    options "root=LABEL=GAIA_ROOT rw quiet splash"
}
EOF
    fi
    
    # Create GRUB configuration template if it doesn't exist
    if [ ! -f "boot/grub/grub.cfg" ]; then
        cat > "boot/grub/grub.cfg" << 'EOF'
# Gaia OS GRUB configuration

set timeout=10
set default="Gaia OS"

menuentry "Gaia OS" {
    linux /boot/vmlinuz-linux-asahi root=LABEL=GAIA_ROOT rw quiet splash
    initrd /boot/initramfs-linux-asahi.img
}

menuentry "Gaia OS (Safe Mode)" {
    linux /boot/vmlinuz-linux-asahi root=LABEL=GAIA_ROOT rw single
    initrd /boot/initramfs-linux-asahi.img
}
EOF
    fi
fi

# Add Asahi Linux support if needed
if [[ "$TARGET" == "asahi" || "$TARGET" == "all" ]]; then
    echo -e "${BLUE}Setting up Asahi Linux support files...${NC}"
    
    # Create Neural Engine config if it doesn't exist
    if [ ! -f "src/configs/neural_engine.conf" ]; then
        cat > "src/configs/neural_engine.conf" << 'EOF'
# Gaia OS Neural Engine Configuration

# Enable or disable Neural Engine support
ENABLE_NEURAL_ENGINE=true

# Model optimization options
OPTIMIZE_FOR_NEURAL_ENGINE=true
FALLBACK_TO_CPU=true

# Default model paths
MODEL_DIR=/opt/gaia/ai/models
DATA_DIR=/opt/gaia/ai/data

# Performance settings
MAX_BATCH_SIZE=16
POWER_EFFICIENCY=balanced  # Options: performance, balanced, efficiency
EOF
    fi
    
    # Create placeholder neural engine model if it doesn't exist
    if [ ! -f "src/models/demo_neural_engine.py" ]; then
        cat > "src/models/demo_neural_engine.py" << 'EOF'
#!/usr/bin/env python3
# Demo Neural Engine Model for Gaia OS

import os
import sys
import numpy as np

def check_neural_engine():
    """Check if Apple Neural Engine is available"""
    # Check for ANE device nodes
    if os.path.exists('/dev/apple-m1n1/ane') or os.path.exists('/sys/devices/platform/apple-nvme/ANE'):
        return True
    return False

def demo_inference(input_data=None):
    """Run a demo inference on the Neural Engine or CPU"""
    if input_data is None:
        # Generate sample image data
        input_data = np.random.rand(1, 224, 224, 3).astype(np.float32)
    
    # In a real implementation, this would use ANE-specific libraries
    # Since those aren't fully available yet, this is just a placeholder
    if check_neural_engine():
        print("Simulating inference on Apple Neural Engine")
        # Simulate ANE processing
        result = {"class_id": 42, "confidence": 0.95, "device": "ANE"}
    else:
        print("Falling back to CPU inference")
        # Simulate CPU processing
        result = {"class_id": 42, "confidence": 0.93, "device": "CPU"}
    
    return result

if __name__ == '__main__':
    if check_neural_engine():
        print("Apple Neural Engine detected!")
    else:
        print("Apple Neural Engine not detected, will use CPU fallback")
    
    result = demo_inference()
    print(f"Demo inference result: {result}")
EOF
        chmod +x "src/models/demo_neural_engine.py"
    fi
fi

# Create a virtual environment for Python development on macOS
if [ "$OS" == "macos" ]; then
    echo -e "${YELLOW}Setting up Python virtual environment...${NC}"
    if command -v python3 >/dev/null 2>&1; then
        python3 -m venv venv
        echo "Virtual environment created at './venv'"
        echo "To activate it, run: source ./venv/bin/activate"
    else
        echo -e "${RED}Python 3 not found. Please install Python 3.${NC}"
    fi
    
    echo
    echo -e "${YELLOW}Note: You are currently running macOS.${NC}"
    echo "This setup script is preparing files for Gaia OS development."
    echo "You can build a bootable image using: ./scripts/build_iso.sh [vm|external|asahi|full]"
    echo
fi

# Check for necessary tools based on current OS
if [ "$OS" == "asahi" ] || [ "$OS" == "arch" ] || [ "$OS" == "linux" ] || [ "$OS" == "vm" ]; then
    echo -e "${YELLOW}Checking for required tools...${NC}"
    MISSING_TOOLS=""
    for tool in git make archiso; do
        if ! command -v $tool >/dev/null 2>&1; then
            MISSING_TOOLS="$MISSING_TOOLS $tool"
        fi
    done
    
    if [ -n "$MISSING_TOOLS" ]; then
        echo -e "${RED}Missing required tools:${NC}$MISSING_TOOLS"
        if [ "$OS" == "arch" ] || [ "$OS" == "asahi" ]; then
            echo "Install them with: sudo pacman -S$MISSING_TOOLS"
        else
            echo "Please install these tools using your distribution's package manager"
        fi
    else
        echo -e "${GREEN}All core tools are installed.${NC}"
    fi
    
    # If on Asahi Linux, check Neural Engine status
    if [ "$OS" == "asahi" ]; then
        echo -e "${YELLOW}Checking Neural Engine status...${NC}"
        if [ -d "/sys/devices/platform/apple-nvme/ANE" ] || [ -e "/dev/apple-m1n1/ane" ]; then
            echo -e "${GREEN}Neural Engine hardware detected!${NC}"
        else
            echo -e "${YELLOW}Neural Engine status unknown. May need additional drivers.${NC}"
        fi
    fi
fi

echo
echo -e "${GREEN}Setup complete!${NC}"
echo
echo "Next steps:"
echo "1. Review the documentation in the 'docs' directory"
echo "2. Customize the package list in 'iso/packages/gaia-packages.txt'"
echo "3. Build a custom ISO with options for your target environment:"
echo "   - For Apple Silicon: sudo ./scripts/build_iso.sh asahi"
echo "   - For VM testing: sudo ./scripts/build_iso.sh vm"
echo "   - For external boot: sudo ./scripts/build_iso.sh external"
echo "   - For all environments: sudo ./scripts/build_iso.sh full"
echo "4. After booting, run the environment setup: sudo ./scripts/environment_setup.sh"
echo
echo -e "${YELLOW}For more information, see the README.md and docs/getting_started.md files.${NC}"