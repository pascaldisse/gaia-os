# Step-by-Step Testing Guide for GAIA-OS on Apple Silicon

This document provides detailed instructions for testing GAIA-OS on an Apple Silicon Mac. Follow these steps carefully to ensure a successful installation and testing process.

## Prerequisites Check

Before proceeding, verify:
- Your Mac has an Apple Silicon chip (M1, M2, or M3)
- macOS 13.5 or later is installed
- At least 15GB of free disk space is available
- You have administrator privileges
- Your data is backed up

## 1. Back Up Your Data

**Critical**: This process will partition your disk and install a new operating system. Back up all important data:

```bash
# Use Time Machine or your preferred backup solution
# Alternatively, copy important files to an external drive
```

## 2. Prepare Installation Environment

Make the scripts executable:

```bash
# Navigate to the gaia-os directory
cd /path/to/gaia-os

# Make scripts executable
chmod +x install_gaia_os.sh configure_gaia_os.sh test_ai_libraries.py
```

## 3. Run Installation Script

Execute the installation script from macOS:

```bash
./install_gaia_os.sh
```

## 4. Follow Installation Prompts

During the Fedora Asahi Remix installation:

1. When prompted for edition, select **Minimal**
2. Follow disk partitioning prompts
   - Asahi Linux installer will create a new partition for Linux
   - Your macOS partition will be resized (data preserved)
3. Create a user account when prompted
4. Set a password
5. Complete the installation process
6. Reboot when prompted

## 5. Boot into Fedora Asahi Remix

1. When your Mac restarts, hold the power button to access startup options
2. Select "Fedora Asahi Remix" from the boot menu
3. Log in with the credentials you created during installation

## 6. Configure GAIA-OS

After successfully booting into Fedora:

1. Open a terminal (press Ctrl+Alt+T)
2. Navigate to where you have the configuration script
   ```bash
   # If you downloaded the files again
   git clone https://github.com/pascaldisse/gaia-os.git
   cd gaia-os
   ```
3. Run the configuration script as root
   ```bash
   sudo bash configure_gaia_os.sh
   ```
4. Wait for the configuration to complete
   - This will install TensorFlow, PyTorch, OpenCV, and LocalAI
   - Set the hostname to `gaia-os`
   - Configure the welcome message
   - The process may take 30-60 minutes depending on your internet connection

5. Reboot the system
   ```bash
   sudo reboot
   ```

## 7. Test AI Libraries

After rebooting:

1. Open a terminal
2. Run the test script
   ```bash
   gaia-test
   ```
3. Verify the test results
   - All libraries should display their version numbers
   - Sample operations should execute successfully
   - Look for the "PASSED" status for each library
4. If any tests fail, refer to the troubleshooting section in the instructions.md file

## 8. Explore GAIA-OS Features

Try out different AI libraries:

### TensorFlow Example

Create a file named `tf_test.py` with the following content:

```python
import tensorflow as tf

# Create a simple neural network
model = tf.keras.Sequential([
    tf.keras.layers.Dense(128, activation='relu'),
    tf.keras.layers.Dense(10, activation='softmax')
])

# Print the model summary
model.build((None, 784))
model.summary()

print("TensorFlow is working!")
```

Run it:
```bash
python3 tf_test.py
```

### PyTorch Example

Create a file named `torch_test.py` with the following content:

```python
import torch

# Check if PyTorch is available
print(f"PyTorch version: {torch.__version__}")
print(f"CUDA available: {torch.cuda.is_available()}")

# Create a simple tensor
x = torch.rand(5, 3)
print(x)

print("PyTorch is working!")
```

Run it:
```bash
python3 torch_test.py
```

### OpenCV Example

Create a file named `cv_test.py` with the following content:

```python
import cv2
import numpy as np

# Create a blank image
img = np.zeros((300, 300, 3), dtype=np.uint8)

# Draw a blue circle
cv2.circle(img, (150, 150), 100, (255, 0, 0), 5)

# Draw a green rectangle
cv2.rectangle(img, (50, 50), (250, 250), (0, 255, 0), 3)

# Add text
cv2.putText(img, "GAIA-OS", (75, 150), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 2)

# Save the image
cv2.imwrite('gaia_test.png', img)

print("OpenCV is working! Image saved as gaia_test.png")
```

Run it:
```bash
python3 cv_test.py
```

### LocalAI Example

Start LocalAI:
```bash
run-localai
```

In another terminal, create a file named `localai_test.py` with the following content:

```python
import requests
import json

# Test LocalAI endpoint
try:
    response = requests.get("http://localhost:8080/v1/models", timeout=5)
    if response.status_code == 200:
        print("LocalAI is responsive!")
        print("Available models:", json.dumps(response.json(), indent=2))
    else:
        print(f"LocalAI returned status code: {response.status_code}")
        print(response.text)
except Exception as e:
    print(f"Error connecting to LocalAI: {e}")
```

Run it:
```bash
python3 localai_test.py
```

## 9. Document Your Results

Create a test report with:
- System information
  ```bash
  uname -a
  cat /etc/fedora-release
  ```
- Test results for each library
- Any issues encountered
- Performance observations

## 10. Dual-Boot Usage

You can switch between macOS and GAIA-OS:
- To boot into macOS: Hold the power button during startup and select macOS
- To boot into GAIA-OS: Hold the power button during startup and select Fedora

## Troubleshooting

If you encounter issues:

1. Check logs for errors:
   ```bash
   journalctl -xe
   ```

2. For library installation failures:
   ```bash
   # Check pip installation status
   pip list | grep tensorflow
   pip list | grep torch
   
   # Check dnf installation status
   dnf list installed | grep opencv
   ```

3. For network issues:
   ```bash
   # Test connectivity
   ping -c 3 google.com
   
   # Check DNS resolution
   nslookup python.org
   ```

4. For LocalAI issues:
   ```bash
   # Check if container is running
   podman ps
   
   # Check service status
   systemctl status localai
   ```

5. If all else fails, refer to:
   - [Fedora Asahi Remix Documentation](https://docs.fedoraproject.org/en-US/fedora-asahi-remix/)
   - [Asahi Linux Community](https://asahilinux.org/community/)

## Next Steps

After successful testing, consider:
1. Installing additional AI tools and libraries
2. Exploring Neural Engine optimizations
3. Contributing improvements back to the GAIA-OS project
4. Developing AI applications that leverage the capabilities of GAIA-OS