# Gaia OS Project Guidelines and Commands

## GaiaScript Translation and Encoding Table

### Encoding Table
#### Words
```
Code    Word
w₈₀     build
w₈₁     development
w₈₂     commands
w₈₃     GaiaScript
w₈₄     language
w₈₅     requirements
w₈₆     always
w₈₇     use
w₈₈     code
w₈₉     style
w₉₀     guidelines
w₉₁     imports
w₉₂     formatting
w₉₃     naming
w₉₄     state
w₉₅     declaration
w₉₆     functions
w₉₇     UI
w₉₈     components
w₉₉     styles
w₁₀₀    variable
w₁₀₁    interpolation
w₁₀₂    error
w₁₀₃    handling
w₁₀₄    standard
w₁₀₅    project
w₁₀₆    structure
w₁₀₇    testing
w₁₀₈    OS
w₁₀₉    systems
w₁₁₀    python
w₁₁₁    shell
w₁₁₂    scripts
w₁₁₃    AI
w₁₁₄    integration
w₁₁₅    preferences
```

#### Phrases
```
Code    Phrase
s₂₀     ./setup.sh - Initialize project directories and virtual environment
s₂₁     source ./venv/bin/activate - Activate Python virtual environment (macOS)
s₂₂     sudo pacman -S git make devtools archiso - Install core tools (Asahi/Arch Linux)
s₂₃     sudo mkarchroot /tmp/chroot base - Create chroot environment
s₂₄     sudo arch-chroot /tmp/chroot - Enter chroot environment
s₂₅     ./scripts/build_iso.sh - Build custom ISO (must be run as root in chroot)
s₂₆     Build GaiaScript: /Users/pascaldisse/gaia/.gaia/gaia/gaia build main.gaia --output=build/gaia-os.js --target=web
s₂₇     ./scripts/check_asahi.sh - Validate Asahi Linux environment
s₂₈     python -m pytest src/tests/ - Run all Python tests
s₂₉     python -m pytest src/tests/test_file.py::test_function - Run specific test
s₃₀     flake8 src/ - Run Python linter on source code
s₃₁     Test on separate partition or spare M1 Mac for hardware verification
s₃₂     OS imports: Group by stdlib, third-party, project-specific
s₃₃     OS formatting: Follow Arch Linux style for shell scripts, Python PEP 8
s₃₄     OS naming: Use descriptive names, snake_case for files, functions, and variables
s₃₅     OS error handling: Include proper error messages and exit codes in scripts
s₃₆     OS documentation: Docstrings for all Python classes and functions
s₃₇     OS AI integration: Document all AI models and their dependencies
s₃₈     ALWAYS USE GAIASCRIPT for UI and interactive elements
s₃₉     GaiaScript imports: Use N⟨UI, Utils, JsSystem⟩ namespace imports pattern
s₄₀     GaiaScript state declaration: Use S⟨variable1: value1, variable2: value2⟩
s₄₁     GaiaScript functions: Use F⟨functionName, param1, param2⟩...⟨/F⟩ pattern
s₄₂     GaiaScript UI components: Declare with UI⟨✱⟩...⟨/UI⟩ and proper styling
s₄₃     GaiaScript UI styles: Use □{styles}⟦Content⟧ for styled elements
s₄₄     GaiaScript variable interpolation: Use ${...} for dynamic content
s₄₅     GaiaScript error handling: Encapsulate error handling in dedicated functions
s₄₆     Shell scripts: POSIX-compliant when possible
s₄₇     Core tools: C/C++, Rust for performance-critical code
s₄₈     AI integration: Python with TensorFlow/PyTorch
s₄₉     UI Components: GaiaScript for all user interfaces
s₅₀     Prefer ARM/AArch64-optimized libraries for M1 compatibility
s₅₁     src/: Core system code and Python modules
s₅₂     scripts/: Build and utility scripts
s₅₃     boot/: Boot and initialization code
s₅₄     ui/: GaiaScript UI components and interfaces
s₅₅     ai_examples/: Example code for AI integration
```

#### Symbols
```
Symbol  Meaning
⊕       Concatenation
→       Flow
N       Network
S       State
F       Function
UI      UI Component
□       Styled Element
```

## Build and Development
- `./setup.sh` - Initialize project directories and virtual environment
- `source ./venv/bin/activate` - Activate Python virtual environment (macOS)
- `sudo pacman -S git make devtools archiso` - Install core tools (Asahi/Arch Linux)
- `sudo mkarchroot /tmp/chroot base` - Create chroot environment
- `sudo arch-chroot /tmp/chroot` - Enter chroot environment
- `./scripts/build_iso.sh` - Build custom ISO (must be run as root in chroot)
- Build GaiaScript: `/Users/pascaldisse/gaia/.gaia/gaia/gaia build main.gaia --output=build/gaia-os.js --target=web`

## Testing
- `./scripts/check_asahi.sh` - Validate Asahi Linux environment
- `python -m pytest src/tests/` - Run all Python tests
- `python -m pytest src/tests/test_file.py::test_function` - Run specific test
- `flake8 src/` - Run Python linter on source code
- Test on separate partition or spare M1 Mac for hardware verification

## Code Style Guidelines

### OS and Systems Code Style
- **Imports**: Group by stdlib, third-party, project-specific
- **Formatting**: Follow Arch Linux style for shell scripts, Python PEP 8 for Python
  * 4-space indentation for Python
  * Maximum line length of 88 characters
- **Naming**: Use descriptive names, snake_case for files, functions, and variables
- **Error Handling**: Include proper error messages and exit codes in scripts
- **Documentation**: Docstrings for all Python classes and functions
- **AI Integration**: Document all AI models and their dependencies

### GaiaScript UI Code Style
- **ALWAYS USE GAIASCRIPT** for UI and interactive elements
- **Imports**: Use `N⟨UI, Utils, JsSystem⟩` namespace imports pattern
- **State Declaration**: Use `S⟨variable1: value1, variable2: value2⟩`
- **Functions**: Use `F⟨functionName, param1, param2⟩...⟨/F⟩` pattern
- **UI Components**: Declare with `UI⟨✱⟩...⟨/UI⟩` and proper styling
- **UI Styles**: Use `□{styles}⟦Content⟧` for styled elements
- **Variable Interpolation**: Use `${...}` for dynamic content
- **Error Handling**: Encapsulate error handling in dedicated GaiaScript functions

## Language Preferences
- Shell scripts: POSIX-compliant when possible
- Core tools: C/C++, Rust for performance-critical code
- AI integration: Python with TensorFlow/PyTorch
- UI Components: GaiaScript for all user interfaces
- Prefer ARM/AArch64-optimized libraries for M1 compatibility

## Project Structure
- **src/**: Core system code and Python modules
- **scripts/**: Build and utility scripts
- **boot/**: Boot and initialization code
- **ui/**: GaiaScript UI components and interfaces
- **ai_examples/**: Example code for AI integration

## GaiaScript Encoding

D⟨O⟨
  title:T⟨Gaia OS Project Guidelines and Commands⟩,
  sections:L⟨
    O⟨name:T⟨w₈₀ and w₈₁⟩, content:L⟨s₂₀, s₂₁, s₂₂, s₂₃, s₂₄, s₂₅, s₂₆⟩⟩,
    O⟨name:T⟨w₁₀₇⟩, content:L⟨s₂₇, s₂₈, s₂₉, s₃₀, s₃₁⟩⟩,
    O⟨name:T⟨w₈₈ w₈₉ w₉₀⟩, content:L⟨
      O⟨name:T⟨w₁₀₈ and w₁₀₉ w₈₈ w₈₉⟩, content:L⟨s₃₂, s₃₃, s₃₄, s₃₅, s₃₆, s₃₇⟩⟩,
      O⟨name:T⟨w₈₃ w₉₇ w₈₈ w₈₉⟩, content:L⟨s₃₈, s₃₉, s₄₀, s₄₁, s₄₂, s₄₃, s₄₄, s₄₅⟩⟩
    ⟩⟩,
    O⟨name:T⟨w₈₄ w₁₁₅⟩, content:L⟨s₄₆, s₄₇, s₄₈, s₄₉, s₅₀⟩⟩,
    O⟨name:T⟨w₁₀₅ w₁₀₆⟩, content:L⟨s₅₁, s₅₂, s₅₃, s₅₄, s₅₅⟩⟩
  ⟩
⟩⟩