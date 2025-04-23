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
            # Create a simple image
            import numpy as np
            img = np.zeros((100, 100, 3), dtype=np.uint8)
            # Draw a white rectangle
            cv2.rectangle(img, (25, 25), (75, 75), (255, 255, 255), -1)
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
        
        # Try running a basic LocalAI API check
        import requests
        try:
            response = requests.get("http://localhost:8080/v1/models", timeout=2)
            if response.status_code == 200:
                print("LocalAI test: API is responsive")
                return True
        except:
            pass
        
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
    
    if not all_passed:
        print("\n===== Troubleshooting Tips =====")
        print("- Make sure all libraries are installed correctly")
        print("- Check if you have the necessary GPU drivers (if applicable)")
        print("- For LocalAI, ensure the service is running")
        print("- Consult the documentation for more information")
    
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())