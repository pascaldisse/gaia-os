#!/bin/bash
#
# Gaia OS Environment Setup Script
# This script detects the running environment (VM, external drive, or Asahi)
# and configures the system accordingly.

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check for root
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}Error: This script must be run as root.${NC}"
    echo "Please run: sudo $0"
    exit 1
fi

echo -e "${GREEN}"
echo "====================================================="
echo "      Gaia OS - Environment Detection & Setup        "
echo "====================================================="
echo -e "${NC}"

# Create configuration directory
mkdir -p /etc/gaia

# Detect environment
echo -e "${YELLOW}Detecting environment...${NC}"

# 1. Check for VM
if grep -q "hypervisor" /proc/cpuinfo || \
   grep -q "VMware" /sys/class/dmi/id/product_name 2>/dev/null || \
   grep -q "QEMU" /sys/class/dmi/id/product_name 2>/dev/null || \
   grep -q "VirtualBox" /sys/class/dmi/id/product_name 2>/dev/null || \
   grep -q "Parallels" /sys/class/dmi/id/product_name 2>/dev/null; then
    ENV_TYPE="vm"
    echo -e "${GREEN}✓ Detected VM environment${NC}"
fi

# 2. Check for Apple Silicon / Asahi
if [ -z "$ENV_TYPE" ] && (grep -q "Apple" /proc/cpuinfo 2>/dev/null || \
   [ -d "/sys/firmware/devicetree/base/compatible" ] && \
   grep -q "apple" /sys/firmware/devicetree/base/compatible 2>/dev/null); then
    ENV_TYPE="asahi"
    echo -e "${GREEN}✓ Detected Asahi Linux on Apple Silicon${NC}"
fi

# 3. If not VM or Asahi, assume external
if [ -z "$ENV_TYPE" ]; then
    ENV_TYPE="external"
    echo -e "${GREEN}✓ Detected external boot drive${NC}"
fi

# Store environment
echo "GAIA_BOOT_ENV=$ENV_TYPE" > /etc/gaia/boot_environment

# Configure based on environment
echo -e "${YELLOW}Configuring for $ENV_TYPE environment...${NC}"
case "$ENV_TYPE" in
    vm)
        # Configure VM tools
        if command -v systemctl >/dev/null 2>&1; then
            # Check which VM platform and enable appropriate services
            if grep -q "VMware" /sys/class/dmi/id/product_name 2>/dev/null; then
                echo "Enabling VMware tools..."
                systemctl enable --now open-vm-tools.service
            elif grep -q "VirtualBox" /sys/class/dmi/id/product_name 2>/dev/null; then
                echo "Enabling VirtualBox guest additions..."
                systemctl enable --now vboxservice.service
            elif grep -q "QEMU" /sys/class/dmi/id/product_name 2>/dev/null; then
                echo "Enabling QEMU guest agent..."
                systemctl enable --now qemu-guest-agent.service
            fi
            
            # Enable Spice agent for better graphics
            if command -v spice-vdagentd >/dev/null 2>&1; then
                systemctl enable --now spice-vdagentd.service
            fi
        fi
        
        # Set VM-optimized parameters
        echo 'vm.swappiness=10' > /etc/sysctl.d/99-gaia-vm.conf
        
        # Configure display for VM
        if [ -f "/etc/X11/xorg.conf.d/40-gaia-display.conf" ]; then
            echo "Optimizing display settings for VM..."
            cat > /etc/X11/xorg.conf.d/40-gaia-display.conf << EOF
Section "Device"
    Identifier "VMDisplay"
    Driver "modesetting"
    Option "AccelMethod" "glamor"
    Option "DRI" "3"
EndSection
EOF
        fi
        ;;

    asahi)
        # Enable Asahi-specific services and optimizations
        if command -v systemctl >/dev/null 2>&1; then
            # Check and enable Asahi-specific services
            echo "Enabling Asahi-specific services..."
            
            # Power management
            systemctl enable --now thermald.service
            systemctl enable --now power-profiles-daemon.service
            
            # Audio
            if command -v alsactl >/dev/null 2>&1; then
                alsactl restore
            fi
        fi
        
        # Check for Neural Engine
        if [ -d "/sys/devices/platform/apple-nvme/ANE" ] || [ -e "/dev/apple-m1n1/ane" ]; then
            echo "Configuring Neural Engine support..."
            
            # Create directories for AI workloads
            mkdir -p /opt/gaia/ai/models
            mkdir -p /opt/gaia/ai/data
            
            # Create Neural Engine configuration
            if [ -f "/opt/gaia/configs/neural_engine.conf" ]; then
                cp /opt/gaia/configs/neural_engine.conf /etc/gaia/
            fi
        fi
        
        # Optimize for Apple Silicon
        echo 'vm.swappiness=5' > /etc/sysctl.d/99-gaia-asahi.conf
        ;;

    external)
        # Configure for external boot setup
        echo "Configuring for external boot drive..."
        
        # Update fstab if needed
        if grep -q "GAIA_ROOT" /etc/fstab; then
            echo "Updating fstab for proper mounting..."
            # Already configured by installer
        else
            # Find the root device
            ROOT_DEV=$(findmnt -no SOURCE /)
            if [ -n "$ROOT_DEV" ]; then
                ROOT_UUID=$(blkid -s UUID -o value "$ROOT_DEV")
                if [ -n "$ROOT_UUID" ]; then
                    echo "Updating fstab with correct UUID..."
                    sed -i "s|ROOT_UUID|$ROOT_UUID|g" /etc/fstab
                fi
            fi
        fi
        
        # Make sure bootloader is properly configured
        if [ -d "/boot/efi/EFI/BOOT" ]; then
            echo "Verifying bootloader configuration..."
            if [ ! -f "/boot/efi/EFI/BOOT/BOOTX64.EFI" ]; then
                echo "Fixing bootloader..."
                if [ -f "/boot/efi/EFI/gaia/grubx64.efi" ]; then
                    mkdir -p /boot/efi/EFI/BOOT
                    cp /boot/efi/EFI/gaia/grubx64.efi /boot/efi/EFI/BOOT/BOOTX64.EFI
                elif [ -f "/boot/efi/EFI/gaia/shimx64.efi" ]; then
                    mkdir -p /boot/efi/EFI/BOOT
                    cp /boot/efi/EFI/gaia/shimx64.efi /boot/efi/EFI/BOOT/BOOTX64.EFI
                fi
            fi
        fi
        ;;

    *)
        echo "Unknown environment type. Using generic configuration."
        ;;
esac

# Set up the Gaia assistant for any environment
echo -e "${YELLOW}Setting up Gaia Assistant...${NC}"
if [ -d "/opt/gaia/assistant" ]; then
    # Create symlink for easy access
    ln -sf /opt/gaia/assistant/gaia_assistant.py /usr/local/bin/gaia-assistant
    chmod +x /usr/local/bin/gaia-assistant
    
    # Add to desktop environment if GNOME is installed
    if command -v gnome-shell >/dev/null 2>&1; then
        mkdir -p /usr/share/applications
        cat > /usr/share/applications/gaia-assistant.desktop << EOF
[Desktop Entry]
Type=Application
Name=Gaia Assistant
Comment=AI assistant for Gaia OS
Exec=/usr/local/bin/gaia-assistant --interactive
Icon=/opt/gaia/assistant/icon.png
Terminal=true
Categories=Utility;
EOF
    fi
fi

# Final environment-specific configurations
echo -e "${YELLOW}Finalizing environment-specific configurations...${NC}"

# Create a first-boot detection file
if [ ! -f "/etc/gaia/first_boot_complete" ]; then
    echo "First boot detected. Performing one-time setup..."
    
    # Run environment-specific first boot items
    case "$ENV_TYPE" in
        vm)
            # VM-specific first boot actions
            ;;
        asahi)
            # Asahi-specific first boot actions
            ;;
        external)
            # External boot first boot actions
            ;;
    esac
    
    # Mark first boot as complete
    date > /etc/gaia/first_boot_complete
    
    echo "First boot setup complete."
fi

echo -e "${GREEN}Gaia OS has been configured for $ENV_TYPE environment.${NC}"
echo "You may need to restart your system for all changes to take effect."