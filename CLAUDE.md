# Gaia OS Project Guidelines and Commands

## Build and Development
- `./setup.sh` - Initialize project directories and virtual environment
- `source ./venv/bin/activate` - Activate Python virtual environment (macOS)
- `sudo pacman -S git make devtools archiso` - Install core tools (Asahi/Arch Linux)
- `sudo mkarchroot /tmp/chroot base` - Create chroot environment
- `sudo arch-chroot /tmp/chroot` - Enter chroot environment
- `./scripts/build_iso.sh` - Build custom ISO (must be run as root in chroot)

## Testing
- `./scripts/check_asahi.sh` - Validate Asahi Linux environment
- `python -m pytest src/tests/` - Run all Python tests
- `python -m pytest src/tests/test_file.py::test_function` - Run specific test
- `flake8 src/` - Run Python linter on source code
- Test on separate partition or spare M1 Mac for hardware verification

## Code Style Guidelines
- **Imports**: Group by stdlib, third-party, project-specific
- **Formatting**: Follow Arch Linux style for shell scripts, Python PEP 8 for Python
  * 4-space indentation for Python
  * Maximum line length of 88 characters
- **Naming**: Use descriptive names, snake_case for files, functions, and variables
- **Error Handling**: Include proper error messages and exit codes in scripts
- **Documentation**: Docstrings for all Python classes and functions
- **AI Integration**: Document all AI models and their dependencies

## Language Preferences
- Shell scripts: POSIX-compliant when possible
- Core tools: C/C++, Rust for performance-critical code
- AI integration: Python with TensorFlow/PyTorch
- Prefer ARM/AArch64-optimized libraries for M1 compatibility