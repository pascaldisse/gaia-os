#!/bin/bash
#
# GAIA-OS Configuration Script
# This script installs AI libraries and configures system branding for GAIA-OS,
# a customized fork of Fedora Asahi Remix 42 Minimal for Apple Silicon Macs.

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

# Function to check if running on Fedora Asahi Remix
check_environment() {
    print_message "Checking environment..." "$BLUE"
    
    # Check if running on Fedora
    if [[ ! -f /etc/fedora-release ]]; then
        print_error "This script must be run on Fedora Asahi Remix."
    fi
    
    # Check if running on Apple Silicon
    if [[ "$(uname -m)" != "aarch64" ]]; then
        print_error "This script must be run on an Apple Silicon Mac (M1/M2/M3)."
    fi
    
    # Check if running as root
    if [[ "$(id -u)" -ne 0 ]]; then
        print_error "This script must be run as root (sudo)."
    fi
    
    print_message "Environment check passed." "$GREEN"
}

# Function to update the system
update_system() {
    print_message "Updating system packages..." "$BLUE"
    
    if ! dnf update -y; then
        print_error "Failed to update system packages."
    fi
    
    print_message "System packages updated successfully." "$GREEN"
}

# Function to install development tools and prerequisites
install_prerequisites() {
    print_message "Installing development tools and prerequisites..." "$BLUE"
    
    # Install development tools
    if ! dnf group install -y "Development Tools"; then
        print_error "Failed to install Development Tools."
    fi
    
    # Install Python and pip
    if ! dnf install -y python3 python3-pip python3-devel; then
        print_error "Failed to install Python and pip."
    fi
    
    # Install other dependencies
    if ! dnf install -y gcc gcc-c++ cmake git curl wget; then
        print_error "Failed to install additional dependencies."
    fi
    
    print_message "Prerequisites installed successfully." "$GREEN"
}

# Function to install TensorFlow
install_tensorflow() {
    print_message "Installing TensorFlow..." "$BLUE"
    
    # Install TensorFlow for ARM64
    if ! pip3 install --upgrade pip && pip3 install tensorflow; then
        print_error "Failed to install TensorFlow."
    fi
    
    print_message "TensorFlow installed successfully." "$GREEN"
}

# Function to install PyTorch
install_pytorch() {
    print_message "Installing PyTorch..." "$BLUE"
    
    # Install PyTorch for ARM64
    if ! pip3 install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu; then
        print_error "Failed to install PyTorch."
    fi
    
    print_message "PyTorch installed successfully." "$GREEN"
}

# Function to install OpenCV
install_opencv() {
    print_message "Installing OpenCV..." "$BLUE"
    
    # Install OpenCV from Fedora repositories
    if ! dnf install -y opencv python3-opencv; then
        print_error "Failed to install OpenCV from repositories."
    fi
    
    print_message "OpenCV installed successfully." "$GREEN"
}

# Function to install LocalAI
install_localai() {
    print_message "Installing LocalAI..." "$BLUE"
    
    # Install LocalAI using the official installer
    if ! curl -fsSL https://localai.io/install.sh | bash; then
        print_message "LocalAI installation from script failed, trying alternative method..." "$YELLOW"
        
        # Alternative method using Docker
        if ! dnf install -y podman; then
            print_error "Failed to install Podman for LocalAI."
        fi
        
        # Pull LocalAI image
        if ! podman pull quay.io/go-skynet/local-ai:latest; then
            print_error "Failed to pull LocalAI container image."
        fi
        
        # Create a script to run LocalAI
        cat > /usr/local/bin/run-localai << 'EOF'
#!/bin/bash
podman run -p 8080:8080 -v $HOME/.local/share/localai:/root/.local/share/localai quay.io/go-skynet/local-ai:latest
EOF
        chmod +x /usr/local/bin/run-localai
        
        print_message "LocalAI installed via Podman. Run 'run-localai' to start the service." "$GREEN"
    else
        print_message "LocalAI installed successfully." "$GREEN"
    fi
}

# Function to configure system branding
configure_branding() {
    print_message "Configuring system branding..." "$BLUE"
    
    # Set hostname to gaia-os
    if ! hostnamectl set-hostname gaia-os; then
        print_error "Failed to set hostname."
    fi
    
    # Update /etc/hosts
    if grep -q "gaia-os" /etc/hosts; then
        print_message "Hostname already in /etc/hosts, skipping." "$YELLOW"
    else
        # Backup the original file
        cp /etc/hosts /etc/hosts.bak
        # Add hostname entry
        sed -i '1s/localhost/localhost gaia-os/' /etc/hosts
        if ! grep -q "gaia-os" /etc/hosts; then
            echo "127.0.0.1 gaia-os" >> /etc/hosts
        fi
    fi
    
    # Add welcome message to /etc/motd
    cat > /etc/motd << 'EOF'
=======================================================================
  _____          _____            ____   _____ 
 / ____|   /\   |_   _|   /\     / __ \ / ____|
| |  __   /  \    | |    /  \   | |  | | (___  
| | |_ | / /\ \   | |   / /\ \  | |  | |\___ \ 
| |__| |/ ____ \ _| |_ / ____ \ | |__| |____) |
 \_____/_/    \_\_____/_/    \_\ \____/|_____/ 
                                               
Welcome to GAIA-OS: AI-Integrated Operating System for Apple Silicon

Based on Fedora Asahi Remix 42 Minimal
=======================================================================
EOF
    
    print_message "System branding configured successfully." "$GREEN"
}

# Function to create a test script
create_test_script() {
    print_message "Creating AI libraries test script..." "$BLUE"
    
    # Create the test_ai_libraries.py script
    cat > /usr/local/bin/test_ai_libraries.py << 'EOF'
#!/usr/bin/env python3
"""
GAIA-OS AI Libraries Test Script
This script tests the functionality of TensorFlow, PyTorch, OpenCV, and LocalAI.
"""

import sys
import os
import subprocess
import importlib.util
from importlib.metadata import version

def check_library(name, import_name=None):
    """Check if a library is installed and import it."""
    if import_name is None:
        import_name = name.lower()
    
    try:
        lib_version = version(name)
        print(f"{name} version: {lib_version}")
        return importlib.import_module(import_name)
    except ImportError:
        print(f"Failed to import {name}. Make sure it's installed correctly.")
        return None
    except Exception as e:
        print(f"Error checking {name}: {e}")
        return None

def test_tensorflow():
    """Test TensorFlow functionality."""
    tf = check_library("tensorflow", "tensorflow")
    if tf is not None:
        try:
            # Simple TensorFlow test
            a = tf.constant(2)
            b = tf.constant(3)
            result = a + b
            print(f"TensorFlow test: 2 + 3 = {result.numpy()}")
            return True
        except Exception as e:
            print(f"TensorFlow test failed: {e}")
    return False

def test_pytorch():
    """Test PyTorch functionality."""
    torch = check_library("torch", "torch")
    if torch is not None:
        try:
            # Simple PyTorch test
            a = torch.tensor(2)
            b = torch.tensor(3)
            result = a + b
            print(f"PyTorch test: 2 + 3 = {result.item()}")
            return True
        except Exception as e:
            print(f"PyTorch test failed: {e}")
    return False

def test_opencv():
    """Test OpenCV functionality."""
    cv2 = check_library("opencv-python", "cv2")
    if cv2 is None:
        # Try alternative package name
        cv2 = check_library("opencv", "cv2")
    
    if cv2 is not None:
        try:
            # Simple OpenCV test
            img = cv2.imread("/dev/null")  # This should not crash
            print("OpenCV test: Library functional")
            return True
        except Exception as e:
            print(f"OpenCV test failed: {e}")
    return False

def test_localai():
    """Test LocalAI availability."""
    try:
        # Check if LocalAI is running as a service
        result = subprocess.run(
            ["systemctl", "is-active", "localai.service"],
            capture_output=True,
            text=True
        )
        if result.stdout.strip() == "active":
            print("LocalAI test: Service is running")
            return True
        
        # Check if LocalAI is available as a container
        result = subprocess.run(
            ["podman", "images", "--format", "{{.Repository}}"],
            capture_output=True,
            text=True
        )
        if "local-ai" in result.stdout or "go-skynet/local-ai" in result.stdout:
            print("LocalAI test: Container image available")
            return True
        
        print("LocalAI test: Not detected, but might be installed")
        return False
    except Exception as e:
        print(f"LocalAI test error: {e}")
        return False

def main():
    """Run all tests and summarize results."""
    print("\n===== GAIA-OS AI Libraries Test =====\n")
    
    results = {
        "TensorFlow": test_tensorflow(),
        "PyTorch": test_pytorch(),
        "OpenCV": test_opencv(),
        "LocalAI": test_localai()
    }
    
    print("\n===== Test Summary =====")
    all_passed = True
    for library, passed in results.items():
        status = "PASSED" if passed else "FAILED"
        if not passed:
            all_passed = False
        print(f"{library}: {status}")
    
    print("\nOverall status:", "PASSED" if all_passed else "FAILED")
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())
EOF
    
    # Make the script executable
    chmod +x /usr/local/bin/test_ai_libraries.py
    
    # Create a symbolic link for ease of use
    ln -sf /usr/local/bin/test_ai_libraries.py /usr/local/bin/gaia-test
    
    print_message "Test script created successfully." "$GREEN"
    print_message "You can run it with: gaia-test" "$BLUE"
}

# Main function
main() {
    print_message "GAIA-OS Configuration" "$GREEN"
    print_message "This script will install AI libraries and configure system branding for GAIA-OS." "$BLUE"
    
    check_environment
    update_system
    install_prerequisites
    install_tensorflow
    install_pytorch
    install_opencv
    install_localai
    configure_branding
    create_test_script
    
    print_message "\nGAIA-OS configuration completed successfully!" "$GREEN"
    print_message "Run the test script to verify the installation:" "$BLUE"
    echo -e "${BOLD}$ gaia-test${NC}"
    print_message "\nFor detailed instructions, refer to the instructions.md file." "$BLUE"
}

# Run the main function
main