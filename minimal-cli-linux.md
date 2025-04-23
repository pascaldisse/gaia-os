# Minimal CLI Linux for Parallels

A minimal Linux distribution optimized for running in Parallels Desktop, with only essential CLI tools.

## Features

- Minimal footprint: Small ISO size and low resource usage
- CLI-only: No desktop environment or GUI applications
- Package management: Full pacman functionality (Arch-based)
- Developer tools: Git, SSH, and essential networking utilities
- Parallels support: Automatic VM detection and optimization

## Included Tools

- Package manager: pacman (Arch Linux)
- Text editors: vim, nano
- Version control: git
- Networking: ssh, wget, rsync, dhcpcd, net-tools
- System monitoring: htop, neofetch
- VM integration: Parallels Tools

## Building the ISO

To build the minimal CLI Linux ISO:

```bash
# On an Arch-based system
sudo ./scripts/build_minimal.sh
```

## Installation with Parallels

After building the ISO, you can create a Parallels VM automatically:

```bash
# Navigate to the output directory
cd out

# Run the Parallels VM creation script
./create_parallels_vm.sh
```

## User Guide

1. After booting, you'll be automatically logged in as root
2. Update the system: `pacman -Syu`
3. Install additional packages: `pacman -S package-name`
4. Configure networking with NetworkManager: `nmcli`
5. Git is pre-installed for your development needs

## Customization

To customize the included packages:

1. Edit the package list: `iso/packages/minimal-cli.txt`
2. Rebuild the ISO: `sudo ./scripts/build_minimal.sh`

## License

This project is licensed under the MIT License - see the LICENSE file for details.