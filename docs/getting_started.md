# Getting Started with Gaia OS

This guide will help you get started with Gaia OS development and usage across multiple platforms.

## Prerequisites

Before developing or using Gaia OS, you need one of the following:

- An Apple Silicon Mac (M1, M2, or M3 series) for native installation
- A modern x86_64 system for VM installation
- A USB drive (8GB+) for external boot
- Basic knowledge of Linux command line

## Installation Options

Gaia OS can be installed in three different ways:

### 1. Native Installation on Apple Silicon

For the best performance and full Neural Engine support:

1. Download the Gaia OS ISO from the releases page
2. Install Asahi Linux first: https://asahilinux.org/
3. Follow the Asahi Linux installer and choose "Custom OS" option
4. Point the installer to your Gaia OS ISO
5. Complete the installation and reboot

### 2. Virtual Machine Installation

For testing and development:

1. Download the Gaia OS VM-compatible ISO
2. Create a new VM in your preferred hypervisor (QEMU, VirtualBox, VMware)
   - Use 4+ GB RAM, 20+ GB disk
   - Enable EFI boot if available
3. Boot from the ISO and follow the installation prompts
4. Alternatively, use the provided script:
   ```
   ./out/run_in_vm.sh
   ```

### 3. External Drive Installation

For portable usage without modifying your main system:

1. Download the Gaia OS ISO
2. Prepare a USB drive (8GB+ recommended):
   ```
   sudo ./out/make_bootable_usb.sh /dev/sdX
   ```
   (Replace /dev/sdX with your USB drive)
3. Boot from the USB drive on your target system

## For Developers

If you're developing Gaia OS:

1. Clone the Gaia OS repository:
   ```
   git clone https://github.com/your-username/gaia-os.git
   cd gaia-os
   ```

2. Run the setup script with your target environment:
   ```
   ./setup.sh [asahi|vm|external|all]
   ```

3. Build the ISO for your target platform:
   ```
   sudo ./scripts/build_iso.sh [asahi|vm|external|full]
   ```

4. To build in a chroot environment (recommended):
   ```
   sudo mkarchroot /tmp/chroot base
   sudo arch-chroot /tmp/chroot
   cd /path/to/gaia-os
   ./scripts/build_iso.sh full
   ```

## Environment Detection and Optimization

Gaia OS automatically detects its running environment:

1. After first boot, the system identifies if it's running:
   - On Apple Silicon with Asahi Linux
   - In a virtual machine
   - From an external drive

2. The system configures itself accordingly:
   - Enabling Neural Engine support on Apple Silicon
   - Loading VM guest tools when in a virtual machine
   - Optimizing for portable usage on external drives

3. You can manually configure the environment with:
   ```
   sudo ./scripts/environment_setup.sh
   ```

## Using the Gaia Assistant

Gaia OS comes with an AI assistant that adapts to your environment:

1. Open a terminal
2. Start the assistant in interactive mode:
   ```
   gaia-assistant --interactive
   ```
   
3. Or ask a specific question:
   ```
   gaia-assistant "How can I check my system performance?"
   ```

## Building Custom Images

To build your own custom Gaia OS image:

1. Modify the package list in `iso/packages/gaia-packages.txt`
2. Add custom files to `iso/airootfs/` if needed
3. Run the build script:
   ```
   sudo ./scripts/build_iso.sh full  # For all platforms
   ```
   Or for a specific platform:
   ```
   sudo ./scripts/build_iso.sh vm    # For VM compatibility
   sudo ./scripts/build_iso.sh asahi # For Apple Silicon
   sudo ./scripts/build_iso.sh external # For external boot
   ```
4. The finished ISO and helper scripts will be in the `out` directory

## Getting Help

- Report issues on GitHub
- Join our community on Discord/Matrix
- Check the [Asahi Linux wiki](https://github.com/AsahiLinux/docs/wiki/) for Apple Silicon-specific issues

## Next Steps

- Try running the `local-ai` package to experiment with local AI models
- Explore platform-specific features (Neural Engine on Asahi)
- Contribute improvements back to the Gaia OS project