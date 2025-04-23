# GAIA-OS: AI-Integrated Operating System for Apple Silicon

GAIA-OS is a customized fork of Fedora Asahi Remix 42 Minimal, designed as an AI-integrated operating system for Apple Silicon Macs (M1, M2, M3). This document provides instructions for installing, configuring, and using GAIA-OS.

## Introduction

GAIA-OS enhances Fedora Asahi Remix 42 Minimal with a suite of AI libraries and tools, including:

- **TensorFlow**: Machine learning framework
- **PyTorch**: Deep learning framework
- **OpenCV**: Computer vision library
- **LocalAI**: Self-hosted AI platform

The system is optimized for Apple Silicon hardware, leveraging the ARM64 architecture for efficient AI workloads.

## Prerequisites

Before installing GAIA-OS, ensure you have:

- Apple Silicon Mac (M1, M2, or M3)
- macOS 13.5 or later
- At least 15GB of free disk space
- Basic knowledge of Linux and command-line usage
- Internet connection

## Installation

The installation process consists of two main steps:

1. Installing Fedora Asahi Remix 42 Minimal (base system)
2. Configuring GAIA-OS (AI libraries and system branding)

### Step 1: Install Fedora Asahi Remix 42 Minimal

1. **Prepare your Mac**:
   - Back up your data
   - Ensure you have at least 15GB of free disk space
   - Connect to a stable internet connection

2. **Run the installation script**:
   ```bash
   chmod +x install_gaia_os.sh
   ./install_gaia_os.sh
   ```

3. **Follow the on-screen instructions**:
   - When prompted for edition, select **Minimal**
   - Complete the installation process as directed by the installer
   - Reboot into Fedora Asahi Remix when prompted

### Step 2: Configure GAIA-OS

After booting into Fedora Asahi Remix, run the configuration script to set up GAIA-OS:

1. **Open a terminal** (press Ctrl+Alt+T)

2. **Run the configuration script**:
   ```bash
   sudo bash configure_gaia_os.sh
   ```

3. **Wait for the configuration to complete**:
   - The script will install AI libraries
   - Set the hostname to `gaia-os`
   - Configure the welcome message
   - Create a test script

4. **Reboot the system**:
   ```bash
   sudo reboot
   ```

## Testing

After installation and configuration, verify that all AI libraries are working correctly:

1. **Open a terminal**

2. **Run the test script**:
   ```bash
   gaia-test
   ```

3. **Review the test results**:
   - The script will test TensorFlow, PyTorch, OpenCV, and LocalAI
   - It will display version information for each library
   - It will perform simple operations to verify functionality

## Using GAIA-OS

### Running LocalAI

LocalAI provides a local alternative to OpenAI's API. To use it:

1. **Start the LocalAI service**:
   ```bash
   run-localai
   ```

2. **Access the API**:
   The API will be available at `http://localhost:8080/v1`

3. **Example usage with Python**:
   ```python
   import requests

   response = requests.post(
       "http://localhost:8080/v1/chat/completions",
       json={
           "model": "gpt-3.5-turbo",
           "messages": [{"role": "user", "content": "Hello from GAIA-OS!"}]
       }
   )
   print(response.json())
   ```

### TensorFlow and PyTorch

Both TensorFlow and PyTorch are installed and ready to use. You can import them in your Python scripts:

```python
import tensorflow as tf
import torch

# TensorFlow example
tensor_tf = tf.constant([[1, 2], [3, 4]])
print(tensor_tf)

# PyTorch example
tensor_torch = torch.tensor([[1, 2], [3, 4]])
print(tensor_torch)
```

### OpenCV

OpenCV is installed for computer vision tasks:

```python
import cv2
import numpy as np

# Create an image
img = np.zeros((300, 300, 3), dtype=np.uint8)
cv2.circle(img, (150, 150), 100, (0, 255, 0), 5)
cv2.imwrite('circle.png', img)
```

## Advanced Configuration

### Neural Engine Optimization (Optional)

For full Neural Engine support, consider the following advanced configuration:

1. **TensorFlow with Metal support**:
   ```bash
   pip install tensorflow-metal
   ```

2. **PyTorch with Metal support**:
   Follow the instructions at [PyTorch Metal Performance Shaders](https://pytorch.org/docs/stable/metal.html)

### System Customization

To further customize your GAIA-OS:

1. **Install additional packages**:
   ```bash
   sudo dnf install <package-name>
   ```

2. **Add a desktop environment** (if desired):
   ```bash
   sudo dnf group install "Basic Desktop" "GNOME Desktop Environment"
   ```

## Troubleshooting

### Common Issues

1. **Installation script fails**:
   - Ensure you're running on macOS 13.5 or later
   - Check your internet connection
   - Verify you have sufficient disk space

2. **Configuration script fails**:
   - Make sure you're running it as root (using sudo)
   - Check your internet connection
   - Look for specific error messages

3. **AI libraries not working**:
   - Run `gaia-test` to identify which library is failing
   - For TensorFlow or PyTorch issues, try reinstalling:
     ```bash
     pip uninstall tensorflow
     pip install tensorflow
     ```
   - For OpenCV issues, reinstall the package:
     ```bash
     sudo dnf reinstall opencv python3-opencv
     ```

4. **LocalAI issues**:
   - Check if the service is running:
     ```bash
     systemctl status localai
     ```
   - Try running the containerized version:
     ```bash
     run-localai
     ```

### Getting Help

If you encounter issues not covered in this document:

1. Check the Fedora Asahi Remix documentation:
   - [Fedora Asahi Remix Documentation](https://docs.fedoraproject.org/en-US/fedora-asahi-remix/)

2. Visit the community forums:
   - [Fedora Discussion](https://discussion.fedoraproject.org/)
   - [Asahi Linux Community](https://asahilinux.org/community/)

## References

- [Fedora Asahi Remix Installation Guide](https://docs.fedoraproject.org/en-US/fedora-asahi-remix/installation/)
- [TensorFlow Documentation](https://www.tensorflow.org/install)
- [PyTorch Documentation](https://pytorch.org/get-started/locally/)
- [OpenCV Documentation](https://docs.opencv.org/)
- [LocalAI Documentation](https://localai.io/basics/getting_started/)

## License

GAIA-OS is distributed under the MIT License.