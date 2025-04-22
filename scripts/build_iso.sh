#!/bin/bash
#
# Gaia OS ISO Build Script
# This script builds a custom Arch Linux ISO with Gaia OS components
# for Apple Silicon Macs, external boot drives, and VM environments.

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ISO_DIR="$PROJECT_ROOT/iso"
WORK_DIR="$PROJECT_ROOT/work"
OUT_DIR="$PROJECT_ROOT/out"
PACKAGES_FILE="$ISO_DIR/packages/gaia-packages.txt"
BOOTLOADER_DIR="$PROJECT_ROOT/boot"

# Build types
BUILD_TYPE=${1:-"full"}  # Options: asahi, vm, external, full (default)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print header
echo -e "${GREEN}"
echo "====================================================="
echo "      Gaia OS - Custom Arch Linux ISO Builder        "
echo "====================================================="
echo -e "${NC}"
echo -e "${BLUE}Build type: ${BUILD_TYPE}${NC}"
echo

# Check if running with root privileges
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}Error: This script must be run as root.${NC}"
    echo "Please run: sudo $0 [build_type]"
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
PROFILE_DIR="$WORK_DIR/profile"
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
mkdir -p "$PROFILE_DIR/airootfs/boot/efi/EFI/gaia"

# Customize the profile
echo -e "${YELLOW}Customizing ISO profile...${NC}"

# Copy custom packages list and add boot/VM packages
if [ -f "$PACKAGES_FILE" ]; then
    echo -e "${YELLOW}Adding packages...${NC}"
    
    # Start with base packages
    grep -v "^#" "$PACKAGES_FILE" | grep -v "^$" > "$PROFILE_DIR/packages.x86_64"
    
    # Add VM and external boot packages based on build type
    if [[ "$BUILD_TYPE" == "vm" || "$BUILD_TYPE" == "full" ]]; then
        echo -e "${BLUE}Adding VM support packages...${NC}"
        cat >> "$PROFILE_DIR/packages.x86_64" << EOF
# VM Support Packages
open-vm-tools
virtualbox-guest-utils
qemu-guest-agent
spice-vdagent
EOF
    fi
    
    if [[ "$BUILD_TYPE" == "external" || "$BUILD_TYPE" == "full" ]]; then
        echo -e "${BLUE}Adding external boot packages...${NC}"
        cat >> "$PROFILE_DIR/packages.x86_64" << EOF
# External Boot Packages
refind
grub
efibootmgr
dosfstools
mtools
EOF
    fi
    
    if [[ "$BUILD_TYPE" == "asahi" || "$BUILD_TYPE" == "full" ]]; then
        echo -e "${BLUE}Keeping Asahi Linux packages...${NC}"
        # Asahi packages already in base list
    fi
fi

# Create a customizable boot menu
mkdir -p "$PROFILE_DIR/airootfs/etc/gaia"
cat > "$PROFILE_DIR/airootfs/etc/gaia/boot_config.conf" << EOF
# Gaia OS Boot Configuration
BOOT_TIMEOUT=10
DEFAULT_BOOT="gaia"
ENABLE_VM_TOOLS=true
ENABLE_EXTERNAL_BOOT=true
ENABLE_ASAHI_SUPPORT=true
EOF

# Create a boot detection script
mkdir -p "$PROFILE_DIR/airootfs/usr/local/bin"
cat > "$PROFILE_DIR/airootfs/usr/local/bin/gaia-boot-detect" << 'EOF'
#!/bin/bash
# Gaia OS Boot Environment Detection Script

# Detect if running in VM
detect_vm() {
    if grep -q "hypervisor" /proc/cpuinfo || \
       grep -q "VMware" /sys/class/dmi/id/product_name || \
       grep -q "VirtualBox" /sys/class/dmi/id/product_name || \
       grep -q "QEMU" /sys/class/dmi/id/product_name; then
        echo "vm"
        return 0
    fi
    return 1
}

# Detect if running on Apple Silicon
detect_asahi() {
    if [ -d "/sys/firmware/devicetree/base/compatible" ] && \
       grep -q "apple" /sys/firmware/devicetree/base/compatible; then
        echo "asahi"
        return 0
    fi
    return 1
}

# Detect if booting from external media
detect_external() {
    boot_dev=$(mount | grep " / " | cut -d' ' -f1)
    if [[ "$boot_dev" == "/dev/sd"* ]] || [[ "$boot_dev" == "/dev/nvme"* ]]; then
        echo "external"
        return 0
    fi
    return 1
}

# Main detection
main() {
    if detect_vm; then
        environment="vm"
    elif detect_asahi; then
        environment="asahi"
    elif detect_external; then
        environment="external"
    else
        environment="unknown"
    fi
    
    echo "GAIA_BOOT_ENV=$environment" > /etc/gaia/boot_environment
    
    # Configure system based on environment
    case "$environment" in
        vm)
            echo "Configuring for VM environment..."
            systemctl enable open-vm-tools virtualbox-guest-utils.service qemu-guest-agent
            ;;
        asahi)
            echo "Configuring for Asahi Linux on Apple Silicon..."
            # Enable Asahi-specific services
            ;;
        external)
            echo "Configuring for external boot..."
            # Enable external boot services
            ;;
        *)
            echo "Unknown boot environment, using generic configuration"
            ;;
    esac
}

main
EOF

chmod +x "$PROFILE_DIR/airootfs/usr/local/bin/gaia-boot-detect"

# Add to startup
mkdir -p "$PROFILE_DIR/airootfs/etc/systemd/system"
cat > "$PROFILE_DIR/airootfs/etc/systemd/system/gaia-boot-detect.service" << EOF
[Unit]
Description=Gaia OS Boot Environment Detection
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/gaia-boot-detect
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

# Create GRUB configuration for external boot and VM
mkdir -p "$PROFILE_DIR/airootfs/etc/grub.d"
cat > "$PROFILE_DIR/airootfs/etc/grub.d/40_gaia" << 'EOF'
#!/bin/sh
cat << GRUBCONFIG
menuentry "Gaia OS" {
    linux /boot/vmlinuz-linux-asahi root=LABEL=GAIA_ROOT rw quiet splash
    initrd /boot/initramfs-linux-asahi.img
}
GRUBCONFIG
EOF
chmod +x "$PROFILE_DIR/airootfs/etc/grub.d/40_gaia"

# Copy custom airootfs overlay
if [ -d "$ISO_DIR/airootfs" ]; then
    echo -e "${YELLOW}Adding custom airootfs overlay...${NC}"
    cp -r "$ISO_DIR/airootfs/"* "$PROFILE_DIR/airootfs/"
fi

# Copy Gaia assistant and tools
echo -e "${YELLOW}Adding Gaia OS components...${NC}"
mkdir -p "$PROFILE_DIR/airootfs/opt/gaia"
cp -r "$PROJECT_ROOT/src/"* "$PROFILE_DIR/airootfs/opt/gaia/"

# Make sure scripts are executable
find "$PROFILE_DIR/airootfs/opt/gaia" -type f -name "*.py" -o -name "*.sh" | xargs chmod +x

# Build ISO
echo -e "${YELLOW}Building ISO image (this may take a while)...${NC}"
mkarchiso -v -w "$WORK_DIR" -o "$OUT_DIR" "$PROFILE_DIR"

# Create a script for making the ISO bootable on external media
cat > "$OUT_DIR/make_bootable_usb.sh" << 'EOF'
#!/bin/bash
# Script to make Gaia OS bootable on external media

if [ $# -lt 1 ]; then
    echo "Usage: $0 /dev/sdX"
    echo "Warning: This will erase all data on the specified device!"
    exit 1
fi

DEVICE=$1
ISO_PATH=$(ls -t *.iso | head -1)

if [ ! -f "$ISO_PATH" ]; then
    echo "Error: No ISO file found in current directory"
    exit 1
fi

echo "This will make $DEVICE bootable with Gaia OS using ISO: $ISO_PATH"
echo "WARNING: ALL DATA ON $DEVICE WILL BE ERASED!"
read -p "Are you sure you want to continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Operation canceled"
    exit 1
fi

# Create partition scheme
echo "Creating partitions..."
parted -s $DEVICE mklabel gpt
parted -s $DEVICE mkpart "EFI" fat32 1MiB 513MiB
parted -s $DEVICE set 1 boot on
parted -s $DEVICE set 1 esp on
parted -s $DEVICE mkpart "GAIA_ROOT" ext4 513MiB 100%

# Format partitions
echo "Formatting partitions..."
mkfs.fat -F32 -n GAIA_EFI ${DEVICE}1
mkfs.ext4 -L GAIA_ROOT ${DEVICE}2

# Mount partitions
echo "Mounting partitions..."
mkdir -p /mnt/gaiaboot/efi
mkdir -p /mnt/gaiaiso
mount ${DEVICE}2 /mnt/gaiaboot
mount ${DEVICE}1 /mnt/gaiaboot/efi
mount -o loop $ISO_PATH /mnt/gaiaiso

# Copy files
echo "Copying files from ISO to USB drive..."
cp -a /mnt/gaiaiso/* /mnt/gaiaboot/
mkdir -p /mnt/gaiaboot/efi/EFI/BOOT
cp /mnt/gaiaboot/boot/vmlinuz-linux-asahi /mnt/gaiaboot/efi/
cp /mnt/gaiaboot/boot/initramfs-linux-asahi.img /mnt/gaiaboot/efi/

# Install bootloader
echo "Installing bootloader..."
mkdir -p /mnt/gaiaboot/efi/EFI/gaia
cat > /mnt/gaiaboot/efi/EFI/BOOT/refind.conf << REFINDCONF
timeout 10
default_selection "Gaia OS"

menuentry "Gaia OS" {
    icon /EFI/BOOT/icons/os_linux.png
    volume GAIA_ROOT
    loader /efi/vmlinuz-linux-asahi
    initrd /efi/initramfs-linux-asahi.img
    options "root=LABEL=GAIA_ROOT rw quiet splash"
}
REFINDCONF

# Copy refind binaries and configuration
cp -r /usr/share/refind/refind /mnt/gaiaboot/efi/EFI/BOOT/
cp /usr/share/refind/refind.conf-sample /mnt/gaiaboot/efi/EFI/BOOT/refind.conf
cp /mnt/gaiaboot/efi/EFI/BOOT/refind_x64.efi /mnt/gaiaboot/efi/EFI/BOOT/BOOTX64.EFI

# Clean up
echo "Cleaning up..."
umount /mnt/gaiaiso
umount /mnt/gaiaboot/efi
umount /mnt/gaiaboot
rmdir /mnt/gaiaiso
rmdir /mnt/gaiaboot/efi
rmdir /mnt/gaiaboot

echo "Done! Your USB drive should now be bootable with Gaia OS."
EOF

chmod +x "$OUT_DIR/make_bootable_usb.sh"

# Create a VM boot script
cat > "$OUT_DIR/run_in_vm.sh" << 'EOF'
#!/bin/bash
# Script to run Gaia OS in a QEMU VM

ISO_PATH=$(ls -t *.iso | head -1)

if [ ! -f "$ISO_PATH" ]; then
    echo "Error: No ISO file found in current directory"
    exit 1
fi

echo "Running Gaia OS in QEMU using ISO: $ISO_PATH"

# Create a virtual disk if it doesn't exist
if [ ! -f "gaia_vm_disk.qcow2" ]; then
    echo "Creating virtual disk..."
    qemu-img create -f qcow2 gaia_vm_disk.qcow2 20G
fi

# Run the VM
qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -cpu host \
    -m 4G \
    -smp cores=2 \
    -drive file=gaia_vm_disk.qcow2,if=virtio \
    -cdrom "$ISO_PATH" \
    -boot d \
    -display gtk \
    -device virtio-net,netdev=net0 \
    -netdev user,id=net0 \
    -device virtio-vga \
    -device virtio-balloon \
    -device virtio-mouse \
    -device virtio-keyboard \
    -enable-kvm

echo "VM has been shut down."
EOF

chmod +x "$OUT_DIR/run_in_vm.sh"

echo -e "${GREEN}ISO build complete!${NC}"
echo -e "ISO file should be available in: ${YELLOW}$OUT_DIR${NC}"
echo
echo -e "${YELLOW}Deployment Options:${NC}"
echo -e "1. For Apple Silicon hardware: Use the Asahi Linux installer to deploy this ISO"
echo -e "2. For external drive boot: Run ${YELLOW}$OUT_DIR/make_bootable_usb.sh /dev/sdX${NC}"
echo -e "3. For VM testing: Run ${YELLOW}$OUT_DIR/run_in_vm.sh${NC}"
echo
echo -e "${GREEN}Done!${NC}"