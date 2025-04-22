#!/bin/bash
#
# Asahi Linux Feature Check Script for Gaia OS
# This script verifies that essential Asahi Linux features are working properly
# for the Gaia OS project.

echo "Gaia OS - Asahi Linux Feature Check"
echo "===================================="
echo

# Check if running on Asahi Linux
if ! grep -q "Asahi" /etc/os-release 2>/dev/null; then
    echo "Error: This script must be run on Asahi Linux!"
    exit 1
fi

# Check if running on Apple Silicon
if ! grep -q "Apple" /proc/cpuinfo 2>/dev/null; then
    echo "Error: This system doesn't appear to be Apple Silicon hardware!"
    exit 1
fi

echo "✓ Running on Asahi Linux on Apple Silicon"
echo

# Check GPU acceleration
echo "Checking GPU acceleration..."
if command -v glxinfo >/dev/null 2>&1; then
    if glxinfo | grep -q "Apple"; then
        echo "✓ GPU acceleration appears to be working"
    else
        echo "✗ GPU acceleration not detected"
        echo "  Run: sudo pacman -S mesa-demos and try again"
    fi
else
    echo "? GPU check skipped (install mesa-demos package to check)"
fi

# Check network connectivity
echo "Checking network connectivity..."
if ping -c 1 8.8.8.8 >/dev/null 2>&1; then
    echo "✓ Network connectivity working"
else
    echo "✗ Network connectivity issues detected"
fi

# Check for neural engine support
echo "Checking for Neural Engine support..."
if [ -d "/sys/devices/platform/apple-nvme/ANE" ] || [ -e "/dev/apple-m1n1/ane" ]; then
    echo "✓ Neural Engine hardware detected"
else
    echo "? Neural Engine status unknown (may need additional drivers)"
fi

# Check for development tools
echo "Checking for essential development tools..."
MISSING_TOOLS=""
for tool in git make gcc; do
    if ! command -v $tool >/dev/null 2>&1; then
        MISSING_TOOLS="$MISSING_TOOLS $tool"
    fi
done

if [ -z "$MISSING_TOOLS" ]; then
    echo "✓ Essential development tools installed"
else
    echo "✗ Missing development tools:$MISSING_TOOLS"
    echo "  Run: sudo pacman -S base-devel git"
fi

echo
echo "Check complete. Address any issues before proceeding with Gaia OS development."