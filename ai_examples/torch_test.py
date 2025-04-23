#!/usr/bin/env python3
"""
PyTorch test script for GAIA-OS
This script demonstrates basic PyTorch functionality.
"""

import torch
import torch.nn as nn
import torch.optim as optim
import numpy as np
import time

print("PyTorch Version:", torch.__version__)
print("Testing PyTorch functionality on GAIA-OS...\n")

# Check CUDA availability
print("CUDA is available:", torch.cuda.is_available())
if torch.cuda.is_available():
    print("CUDA device count:", torch.cuda.device_count())
    print("CUDA device name:", torch.cuda.get_device_name(0))
    device = torch.device("cuda")
else:
    print("Running on CPU")
    device = torch.device("cpu")

# Check MPS (Metal Performance Shaders) availability for Apple Silicon
has_mps = False
if hasattr(torch, 'has_mps') and torch.has_mps:
    has_mps = torch.has_mps
    if has_mps:
        print("MPS (Metal Performance Shaders) is available")
        device = torch.device("mps")
else:
    print("MPS (Metal Performance Shaders) is not available")

print(f"Using device: {device}")

# Define a simple neural network
class SimpleNN(nn.Module):
    def __init__(self):
        super(SimpleNN, self).__init__()
        self.layer1 = nn.Linear(784, 128)
        self.relu = nn.ReLU()
        self.dropout = nn.Dropout(0.2)
        self.layer2 = nn.Linear(128, 64)
        self.layer3 = nn.Linear(64, 10)
        self.softmax = nn.Softmax(dim=1)
    
    def forward(self, x):
        x = self.layer1(x)
        x = self.relu(x)
        x = self.dropout(x)
        x = self.layer2(x)
        x = self.relu(x)
        x = self.dropout(x)
        x = self.layer3(x)
        x = self.softmax(x)
        return x

# Create the model
print("\nCreating a simple neural network...")
model = SimpleNN().to(device)
print(model)

# Generate random data
print("\nGenerating sample data...")
x_train = torch.randn(1000, 784).to(device)
y_train = torch.randint(0, 10, (1000,)).to(device)
x_test = torch.randn(200, 784).to(device)
y_test = torch.randint(0, 10, (200,)).to(device)

# Define loss function and optimizer
criterion = nn.CrossEntropyLoss()
optimizer = optim.Adam(model.parameters(), lr=0.001)

# Train the model
print("\nTraining model (quick demonstration)...")
start_time = time.time()
epochs = 5
for epoch in range(epochs):
    # Forward pass
    outputs = model(x_train)
    loss = criterion(outputs, y_train)
    
    # Backward pass and optimize
    optimizer.zero_grad()
    loss.backward()
    optimizer.step()
    
    # Calculate accuracy
    _, predicted = torch.max(outputs.data, 1)
    correct = (predicted == y_train).sum().item()
    accuracy = correct / y_train.size(0)
    
    print(f"Epoch {epoch+1}/{epochs}, Loss: {loss.item():.4f}, Accuracy: {accuracy:.4f}")

training_time = time.time() - start_time

# Evaluate the model
print("\nEvaluating model...")
model.eval()
with torch.no_grad():
    outputs = model(x_test)
    _, predicted = torch.max(outputs.data, 1)
    correct = (predicted == y_test).sum().item()
    accuracy = correct / y_test.size(0)
    print(f"Test Accuracy: {accuracy:.4f}")

# Tensor operations
print("\nDemonstrating basic tensor operations:")
a = torch.tensor([1, 2, 3]).to(device)
b = torch.tensor([4, 5, 6]).to(device)
print("a =", a)
print("b =", b)
print("a + b =", a + b)
print("a * b =", a * b)
print("Matrix multiplication:", torch.matmul(a.unsqueeze(0), b.unsqueeze(1)))

# Performance information
print("\nPerformance Information:")
print(f"Training time for {epochs} epochs: {training_time:.2f} seconds")

# Memory usage
if torch.cuda.is_available():
    print(f"CUDA memory allocated: {torch.cuda.memory_allocated(0) / 1024**2:.2f} MB")
    print(f"CUDA memory cached: {torch.cuda.memory_reserved(0) / 1024**2:.2f} MB")

print("\nPyTorch test completed successfully on GAIA-OS!")