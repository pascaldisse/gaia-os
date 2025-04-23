#!/usr/bin/env python3
"""
OpenCV test script for GAIA-OS
This script demonstrates basic OpenCV functionality.
"""

import cv2
import numpy as np
import time
import os

print("OpenCV Version:", cv2.__version__)
print("Testing OpenCV functionality on GAIA-OS...\n")

# Create output directory if it doesn't exist
output_dir = "opencv_output"
if not os.path.exists(output_dir):
    os.makedirs(output_dir)

# Basic image creation
print("Creating a blank image...")
img = np.zeros((400, 600, 3), dtype=np.uint8)

# Draw shapes
print("Drawing shapes...")
# Draw a blue rectangle
cv2.rectangle(img, (50, 50), (550, 350), (255, 0, 0), 2)

# Draw a green circle
cv2.circle(img, (300, 200), 100, (0, 255, 0), 3)

# Draw a red line
cv2.line(img, (150, 150), (450, 250), (0, 0, 255), 4)

# Draw a yellow triangle
triangle_pts = np.array([[300, 100], [200, 300], [400, 300]], np.int32)
triangle_pts = triangle_pts.reshape((-1, 1, 2))
cv2.polylines(img, [triangle_pts], True, (0, 255, 255), 3)

# Add text
print("Adding text...")
cv2.putText(img, "GAIA-OS", (220, 380), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)

# Save the image
print(f"Saving image to {output_dir}/basic_shapes.png")
cv2.imwrite(f"{output_dir}/basic_shapes.png", img)

# Create a gradient image
print("Creating a gradient image...")
gradient = np.zeros((256, 256, 3), dtype=np.uint8)
for y in range(256):
    for x in range(256):
        gradient[y, x] = [y, 255-y, x]

print(f"Saving gradient to {output_dir}/gradient.png")
cv2.imwrite(f"{output_dir}/gradient.png", gradient)

# Image processing examples
print("\nDemonstrating image processing...")

# Create a sample image with noise
sample = np.zeros((300, 300, 3), dtype=np.uint8)
cv2.circle(sample, (150, 150), 100, (200, 0, 0), -1)
cv2.rectangle(sample, (100, 100), (200, 200), (0, 200, 0), -1)

# Add noise
noise = np.random.randint(0, 50, (300, 300, 3), dtype=np.uint8)
noisy_image = cv2.add(sample, noise)
print(f"Saving noisy image to {output_dir}/noisy.png")
cv2.imwrite(f"{output_dir}/noisy.png", noisy_image)

# Apply Gaussian blur
print("Applying Gaussian blur...")
blurred = cv2.GaussianBlur(noisy_image, (5, 5), 0)
print(f"Saving blurred image to {output_dir}/blurred.png")
cv2.imwrite(f"{output_dir}/blurred.png", blurred)

# Edge detection
print("Applying edge detection...")
edges = cv2.Canny(cv2.cvtColor(sample, cv2.COLOR_BGR2GRAY), 100, 200)
print(f"Saving edge detection to {output_dir}/edges.png")
cv2.imwrite(f"{output_dir}/edges.png", edges)

# Color space conversion
print("Converting between color spaces...")
hsv = cv2.cvtColor(sample, cv2.COLOR_BGR2HSV)
print(f"Saving HSV image to {output_dir}/hsv.png")
cv2.imwrite(f"{output_dir}/hsv.png", hsv)

# Create a GAIA-OS logo
print("\nCreating GAIA-OS logo...")
logo = np.zeros((300, 500, 3), dtype=np.uint8)

# Background gradient
for y in range(300):
    for x in range(500):
        # Create a dark blue to purple gradient
        blue = int(255 * (1 - y/300))
        purple = int(100 * (x/500))
        logo[y, x] = [purple, 0, blue]

# Add a planet-like circle
cv2.circle(logo, (150, 150), 80, (0, 0, 180), -1)
# Add glow effect
cv2.circle(logo, (150, 150), 90, (20, 0, 160), 10)
cv2.circle(logo, (150, 150), 100, (40, 0, 140), 5)
cv2.circle(logo, (150, 150), 110, (60, 0, 120), 3)

# Add an orbit ring
cv2.ellipse(logo, (150, 150), (120, 40), 30, 0, 360, (120, 120, 240), 2)

# Add "GAIA-OS" text
cv2.putText(logo, "GAIA-OS", (200, 150), cv2.FONT_HERSHEY_SIMPLEX, 1.5, (255, 255, 255), 3)
cv2.putText(logo, "AI-Integrated OS for Apple Silicon", (140, 190), 
            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (200, 200, 200), 1)

print(f"Saving GAIA-OS logo to {output_dir}/gaia_os_logo.png")
cv2.imwrite(f"{output_dir}/gaia_os_logo.png", logo)

# Performance test
print("\nRunning performance test...")
start_time = time.time()
iterations = 100
test_img = np.random.randint(0, 255, (1024, 1024, 3), dtype=np.uint8)

for i in range(iterations):
    # Apply various operations
    cv2.GaussianBlur(test_img, (5, 5), 0)
    cv2.Canny(cv2.cvtColor(test_img, cv2.COLOR_BGR2GRAY), 100, 200)
    cv2.resize(test_img, (512, 512))

performance_time = time.time() - start_time
print(f"Performed {iterations} iterations of image processing in {performance_time:.2f} seconds")
print(f"Average time per operation: {performance_time/iterations*1000:.2f} ms")

print("\nOpenCV test completed successfully on GAIA-OS!")
print(f"Output images are saved in the '{output_dir}' directory")