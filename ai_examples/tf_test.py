#!/usr/bin/env python3
"""
TensorFlow test script for GAIA-OS
This script demonstrates basic TensorFlow functionality.
"""

import tensorflow as tf
import numpy as np
import time

print("TensorFlow Version:", tf.__version__)
print("Testing TensorFlow functionality on GAIA-OS...\n")

# Create a simple neural network model
print("Creating a simple neural network...")
model = tf.keras.Sequential([
    tf.keras.layers.Dense(128, activation='relu', input_shape=(784,)),
    tf.keras.layers.Dropout(0.2),
    tf.keras.layers.Dense(64, activation='relu'),
    tf.keras.layers.Dropout(0.2),
    tf.keras.layers.Dense(10, activation='softmax')
])

# Compile the model
model.compile(optimizer='adam',
              loss='sparse_categorical_crossentropy',
              metrics=['accuracy'])

# Print model summary
print("\nModel Summary:")
model.summary()

# Generate random training data
print("\nGenerating sample data...")
x_train = np.random.random((1000, 784))
y_train = np.random.randint(0, 10, (1000,))
x_test = np.random.random((200, 784))
y_test = np.random.randint(0, 10, (200,))

# Train the model
print("\nTraining model (quick demonstration)...")
start_time = time.time()
model.fit(x_train, y_train, epochs=5, batch_size=32, verbose=2, 
          validation_data=(x_test, y_test))
training_time = time.time() - start_time

# Evaluate the model
print("\nEvaluating model...")
test_loss, test_acc = model.evaluate(x_test, y_test, verbose=2)
print(f"Test accuracy: {test_acc:.4f}")

# Make predictions
print("\nMaking predictions...")
predictions = model.predict(x_test[:3])
print("Prediction shape:", predictions.shape)
print("Predictions (first 3 samples):")
for i, pred in enumerate(predictions[:3]):
    predicted_class = np.argmax(pred)
    print(f"Sample {i}: Class {predicted_class} (Confidence: {pred[predicted_class]:.4f})")

# Performance information
print("\nPerformance Information:")
print(f"Training time for 5 epochs: {training_time:.2f} seconds")

# Hardware information
try:
    hardware_info = tf.config.list_physical_devices()
    print("\nAvailable TensorFlow Devices:")
    for device in hardware_info:
        print(f"- {device.device_type}: {device.name}")
except Exception as e:
    print(f"Could not retrieve hardware information: {e}")

print("\nTensorFlow test completed successfully on GAIA-OS!")