#!/bin/bash
#
# Minimal CLI Linux ISO Build Script
# This script builds a minimal CLI-only Arch Linux ISO
# for use in Parallels or other virtual machines.

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ISO_DIR="$PROJECT_ROOT/iso"
WORK_DIR="$PROJECT_ROOT/work"
OUT_DIR="$PROJECT_ROOT/out"
PACKAGES_FILE="$ISO_DIR/packages/minimal-cli.txt"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print header
echo -e "${GREEN}"
echo "====================================================="
echo "      Minimal CLI Linux - Custom ISO Builder         "
echo "====================================================="
echo -e "${NC}"
echo

# Check if running with root privileges
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}Error: This script must be run as root.${NC}"
    echo "Please run: sudo $0"
    exit 1
fi

# Check if archiso is installed
if ! command -v mkarchiso &> /dev/null; then
    echo -e "${YELLOW}Installing archiso...${NC}"
    pacman -Sy --noconfirm archiso
fi

echo -e "${YELLOW}Preparing build environment...${NC}"

# Create necessary directories
mkdir -p "$WORK_DIR" "$OUT_DIR"

# Set up profile directory
PROFILE_DIR="$WORK_DIR/profile-minimal"
if [ -d "$PROFILE_DIR" ]; then
    echo -e "${YELLOW}Cleaning up previous profile directory...${NC}"
    rm -rf "$PROFILE_DIR"
fi

echo -e "${YELLOW}Creating profile directory...${NC}"
mkdir -p "$PROFILE_DIR"
cp -r /usr/share/archiso/configs/releng/* "$PROFILE_DIR/"

# Create boot directories for bootloaders
mkdir -p "$PROFILE_DIR/airootfs/boot/grub"
mkdir -p "$PROFILE_DIR/airootfs/boot/refind"
mkdir -p "$PROFILE_DIR/airootfs/boot/efi/EFI/boot"

# Customize the profile
echo -e "${YELLOW}Customizing ISO profile...${NC}"

# Copy custom packages list
if [ -f "$PACKAGES_FILE" ]; then
    echo -e "${YELLOW}Adding minimal CLI packages...${NC}"
    
    # Start with base packages
    grep -v "^#" "$PACKAGES_FILE" | grep -v "^$" > "$PROFILE_DIR/packages.x86_64"
fi

# Remove desktop environment from skel
if [ -d "$PROFILE_DIR/airootfs/etc/skel/.config" ]; then
    rm -rf "$PROFILE_DIR/airootfs/etc/skel/.config"
fi

# Create a customizable boot menu
mkdir -p "$PROFILE_DIR/airootfs/etc/minimal-cli"
cat > "$PROFILE_DIR/airootfs/etc/minimal-cli/boot_config.conf" << EOF
# Minimal CLI Linux Boot Configuration
BOOT_TIMEOUT=5
DEFAULT_BOOT="minimal"
ENABLE_VM_TOOLS=true
EOF

# Create a boot detection script
mkdir -p "$PROFILE_DIR/airootfs/usr/local/bin"
cat > "$PROFILE_DIR/airootfs/usr/local/bin/minimal-boot-detect" << 'EOF'
#!/bin/bash
# Minimal CLI Linux Boot Environment Detection Script

# Detect if running in VM
detect_vm() {
    if grep -q "hypervisor" /proc/cpuinfo || \
       grep -q "VMware" /sys/class/dmi/id/product_name || \
       grep -q "VirtualBox" /sys/class/dmi/id/product_name || \
       grep -q "QEMU" /sys/class/dmi/id/product_name || \
       grep -q "Parallels" /sys/class/dmi/id/product_name; then
        echo "vm"
        return 0
    fi
    # Additional check for Parallels
    if [ -d "/sys/devices/virtual/dmi/id" ] && grep -q "Parallels" /sys/devices/virtual/dmi/id/product_name 2>/dev/null; then
        echo "parallels"
        return 0
    fi
    return 1
}

# Main detection
main() {
    if detect_vm; then
        environment="vm"
        # Check specific VM type
        if grep -q "Parallels" /sys/class/dmi/id/product_name 2>/dev/null; then
            environment="parallels"
        fi
    else
        environment="unknown"
    fi
    
    echo "BOOT_ENV=$environment" > /etc/minimal-cli/boot_environment
    
    # Configure system based on environment
    case "$environment" in
        vm)
            echo "Configuring for VM environment..."
            systemctl enable open-vm-tools virtualbox-guest-utils.service qemu-guest-agent
            ;;
        parallels)
            echo "Configuring for Parallels VM environment..."
            # Enable Parallels specific services if installed
            if [ -f "/usr/lib/systemd/system/prltoolsd.service" ]; then
                systemctl enable prltoolsd.service
            fi
            ;;
        *)
            echo "Unknown boot environment, using generic configuration"
            ;;
    esac
}

main
EOF

chmod +x "$PROFILE_DIR/airootfs/usr/local/bin/minimal-boot-detect"

# Add to startup
mkdir -p "$PROFILE_DIR/airootfs/etc/systemd/system"
cat > "$PROFILE_DIR/airootfs/etc/systemd/system/minimal-boot-detect.service" << EOF
[Unit]
Description=Minimal CLI Linux Boot Environment Detection
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/minimal-boot-detect
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

# Enable the service
mkdir -p "$PROFILE_DIR/airootfs/etc/systemd/system/multi-user.target.wants"
ln -sf "/etc/systemd/system/minimal-boot-detect.service" "$PROFILE_DIR/airootfs/etc/systemd/system/multi-user.target.wants/minimal-boot-detect.service"

# Enable NetworkManager
mkdir -p "$PROFILE_DIR/airootfs/etc/systemd/system/multi-user.target.wants"
ln -sf "/usr/lib/systemd/system/NetworkManager.service" "$PROFILE_DIR/airootfs/etc/systemd/system/multi-user.target.wants/NetworkManager.service"

# Create user configuration
cat > "$PROFILE_DIR/airootfs/etc/systemd/system/getty@tty1.service.d/autologin.conf" << EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o '-p -f -- \\u' --noclear --autologin root %I \$TERM
EOF

# Customize pacman configuration for speed
sed -i 's/^#ParallelDownloads = 5/ParallelDownloads = 10/' "$PROFILE_DIR/airootfs/etc/pacman.conf"

# Create a custom welcome message
cat > "$PROFILE_DIR/airootfs/etc/motd" << EOF
===========================================
 Minimal CLI Linux - Optimized for Parallels
===========================================

This minimal Linux distro includes:
- Package manager (pacman)
- Git
- SSH and basic networking tools
- Parallels Tools support

Run 'pacman -Syu' to update your system.
EOF

# Build ISO
echo -e "${YELLOW}Building ISO image (this may take a while)...${NC}"
mkarchiso -v -w "$WORK_DIR" -o "$OUT_DIR" "$PROFILE_DIR"

# Create a Parallels PVM creation script
cat > "$OUT_DIR/create_parallels_vm.sh" << 'EOF'
#!/bin/bash
# Script to create a Parallels VM for Minimal CLI Linux

ISO_PATH=$(ls -t *minimal*.iso | head -1)

if [ ! -f "$ISO_PATH" ]; then
    echo "Error: No minimal ISO file found in current directory"
    exit 1
fi

# Check if Parallels is installed
if ! command -v prlctl &> /dev/null; then
    echo "Error: Parallels Desktop not found. Please make sure it's installed."
    exit 1
fi

echo "Creating Parallels VM for Minimal CLI Linux using ISO: $ISO_PATH"

# Create a name for the VM
VM_NAME="MinimalCLI-$(date +%Y%m%d)"

# Create the VM
echo "Creating VM: $VM_NAME..."
prlctl create "$VM_NAME" --distribution linux --dst ~/Parallels/

# Set VM configuration
echo "Configuring VM..."
prlctl set "$VM_NAME" --cpus 2
prlctl set "$VM_NAME" --memsize 2048
prlctl set "$VM_NAME" --device-add cdrom --image "$ISO_PATH"
prlctl set "$VM_NAME" --device-bootorder "cdrom0 hdd0"
prlctl set "$VM_NAME" --device-set cdrom0 --connect

# Start the VM
echo "Starting VM..."
prlctl start "$VM_NAME"

echo "Parallels Virtual Machine '$VM_NAME' has been created and started."
echo "Connect to it using Parallels Desktop."
echo ""
echo "Notes:"
echo "1. After installation, use the Devices menu to install Parallels Tools"
echo "2. Run 'pacman -Syu' to update your system after installation"
EOF

chmod +x "$OUT_DIR/create_parallels_vm.sh"

echo -e "${GREEN}Minimal CLI ISO build complete!${NC}"
echo -e "ISO file should be available in: ${YELLOW}$OUT_DIR${NC}"
echo
echo -e "${YELLOW}Deployment Options:${NC}"
echo -e "1. For Parallels Desktop: Run ${YELLOW}$OUT_DIR/create_parallels_vm.sh${NC}"
echo
echo -e "${GREEN}Done!${NC}"