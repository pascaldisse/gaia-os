#!/bin/bash
#
# GAIA-OS Installation Script
# This script automates the installation of Fedora Asahi Remix 42 Minimal
# on Apple Silicon Macs (M1/M2/M3).

set -e

# Text formatting
BOLD='\033[1m'
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored messages
print_message() {
    echo -e "${BOLD}${2}${1}${NC}"
}

# Function to print error messages and exit
print_error() {
    echo -e "${BOLD}${RED}ERROR: ${1}${NC}" >&2
    exit 1
}

# Function to check if running on macOS on Apple Silicon
check_environment() {
    print_message "Checking environment..." "$BLUE"
    
    # Check if running on macOS
    if [[ "$(uname)" != "Darwin" ]]; then
        print_error "This script must be run on macOS."
    fi
    
    # Check if running on Apple Silicon
    if [[ "$(uname -m)" != "arm64" ]]; then
        print_error "This script must be run on an Apple Silicon Mac (M1/M2/M3)."
    fi
    
    # Check macOS version (13.5 or higher)
    os_version=$(sw_vers -productVersion)
    major_version=$(echo "$os_version" | cut -d. -f1)
    minor_version=$(echo "$os_version" | cut -d. -f2)
    
    if [[ "$major_version" -lt 13 || ("$major_version" -eq 13 && "$minor_version" -lt 5) ]]; then
        print_error "macOS 13.5 or later is required. Current version: $os_version"
    fi
    
    # Check available disk space (at least 15GB)
    available_space_kb=$(df -k / | awk 'NR==2 {print $4}')
    available_space_gb=$((available_space_kb / 1024 / 1024))
    if [[ "$available_space_gb" -lt 15 ]]; then
        print_error "At least 15GB of free disk space is required. Available: ${available_space_gb}GB"
    fi
    
    print_message "Environment check passed." "$GREEN"
}

# Function to download the Fedora Asahi Remix installer
download_installer() {
    print_message "Downloading Fedora Asahi Remix installer..." "$BLUE"
    
    # Create a temporary directory
    temp_dir=$(mktemp -d)
    cd "$temp_dir"
    
    # Download the installer script
    if ! curl -fsSL https://fedora-asahi-remix.org/install | sh; then
        print_error "Failed to download the Fedora Asahi Remix installer. Check your internet connection."
    fi
    
    print_message "Installer downloaded successfully." "$GREEN"
}

# Function to guide the user through the Fedora Asahi Remix installation
install_fedora_asahi() {
    print_message "Starting Fedora Asahi Remix installation process..." "$BLUE"
    print_message "\nIMPORTANT INSTALLATION INSTRUCTIONS:" "$YELLOW"
    echo -e "${BOLD}1. When prompted for edition, select: ${GREEN}Minimal${NC}"
    echo -e "${BOLD}2. Follow the on-screen instructions to complete the installation.${NC}"
    echo -e "${BOLD}3. After installation, reboot into Fedora Asahi Remix and run the configuration script.${NC}"
    echo ""
    print_message "Press Enter to continue or Ctrl+C to cancel..." "$YELLOW"
    read -r
    
    # The actual installation is handled by the downloaded script
    # The script from fedora-asahi-remix.org/install will take over here
}

# Main function
main() {
    print_message "GAIA-OS Installation" "$GREEN"
    print_message "This script will install Fedora Asahi Remix 42 Minimal, the base for GAIA-OS." "$BLUE"
    
    check_environment
    download_installer
    install_fedora_asahi
    
    print_message "\nInstallation process started. Follow the on-screen instructions." "$GREEN"
    print_message "After installation completes and you boot into Fedora Asahi Remix," "$BLUE"
    print_message "run the configuration script to set up GAIA-OS:" "$BLUE"
    echo -e "${BOLD}$ sudo bash configure_gaia_os.sh${NC}"
    print_message "\nFor detailed instructions, refer to the instructions.md file." "$BLUE"
}

# Run the main function
main