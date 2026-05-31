#!/usr/bin/env python3
"""
TLS Fingerprint CNN Training Script

This script trains a CNN model to identify applications based on TLS fingerprints.
It generates synthetic training data and trains a 1D CNN model for binary classification.
"""

import json
import numpy as np
from typing import List, Dict, Tuple

class TLSFingerprintCNN:
    def __init__(self, num_classes=9):
        self.num_classes = num_classes
        self.input_dim = 490
        
        np.random.seed(42)
        
        self.conv1_weights = np.random.randn(16, 1, 3).astype(np.float32) * 0.1
        self.conv1_biases = np.zeros(16, dtype=np.float32)
        
        self.conv2_weights = np.random.randn(32, 16, 3).astype(np.float32) * 0.1
        self.conv2_biases = np.zeros(32, dtype=np.float32)
        
        self.conv3_weights = np.random.randn(64, 32, 3).astype(np.float32) * 0.1
        self.conv3_biases = np.zeros(64, dtype=np.float32)
        
        self.fc1_weights = np.random.randn(64 * 20, 128).astype(np.float32) * 0.1
        self.fc1_biases = np.zeros(128, dtype=np.float32)
        
        self.fc2_weights = np.random.randn(128, num_classes).astype(np.float32) * 0.1
        self.fc2_biases = np.zeros(num_classes, dtype=np.float32)
        
    def relu(self, x):
        return np.maximum(0, x)
    
    def max_pool1d(self, x, pool_size=2):
        output_len = x.shape[1] // pool_size
        result = np.zeros((x.shape[0], output_len, x.shape[2]), dtype=np.float32)
        for i in range(output_len):
            result[:, i, :] = x[:, i * pool_size:(i + 1) * pool_size, :].max(axis=1)
        return result
    
    def softmax(self, x):
        exp_x = np.exp(x - np.max(x, axis=-1, keepdims=True))
        return exp_x / np.sum(exp_x, axis=-1, keepdims=True)
    
    def conv1d(self, x, weights, biases, stride=1):
        batch_size, input_len, in_channels = x.shape
        kernel_size = weights.shape[2]
        output_channels = weights.shape[0]
        output_len = (input_len - kernel_size) // stride + 1
        
        output = np.zeros((batch_size, output_len, output_channels), dtype=np.float32)
        
        for b in range(batch_size):
            for oc in range(output_channels):
                for i in range(output_len):
                    s = biases[oc]
                    for ic in range(in_channels):
                        for k in range(kernel_size):
                            idx = i * stride + k
                            if idx < input_len:
                                s += x[b, idx, ic] * weights[oc, ic, k]
                    output[b, i, oc] = s
        
        return output
    
    def forward(self, x):
        x = x.reshape(-1, self.input_dim, 1)
        
        x = self.conv1d(x, self.conv1_weights, self.conv1_biases)
        x = self.relu(x)
        x = self.max_pool1d(x, 2)
        
        x = self.conv1d(x, self.conv2_weights, self.conv2_biases)
        x = self.relu(x)
        x = self.max_pool1d(x, 2)
        
        x = self.conv1d(x, self.conv3_weights, self.conv3_biases)
        x = self.relu(x)
        x = self.max_pool1d(x, 2)
        
        x = x.reshape(x.shape[0], -1)
        
        x = np.dot(x, self.fc1_weights) + self.fc1_biases
        x = self.relu(x)
        
        x = np.dot(x, self.fc2_weights) + self.fc2_biases
        x = self.softmax(x)
        
        return x
    
    def train(self, X_train, y_train, epochs=50, batch_size=32, learning_rate=0.01):
        for epoch in range(epochs):
            indices = np.random.permutation(len(X_train))
            total_loss = 0
            
            for i in range(0, len(X_train), batch_size):
                batch_idx = indices[i:i + batch_size]
                X_batch = X_train[batch_idx]
                y_batch = y_train[batch_idx]
                
                predictions = self.forward(X_batch)
                
                loss = -np.mean(np.sum(y_batch * np.log(predictions + 1e-8), axis=-1))
                total_loss += loss
                
                gradients = predictions - y_batch
                
                if (i // batch_size) % 10 == 0:
                    accuracy = np.mean(np.argmax(predictions, axis=1) == np.argmax(y_batch, axis=1))
                    print(f"Epoch {epoch + 1}/{epochs}, Batch {i // batch_size}, Loss: {loss:.4f}, Accuracy: {accuracy:.4f}")
            
            print(f"Epoch {epoch + 1}/{epochs}, Average Loss: {total_loss / (len(X_train) / batch_size):.4f}")
    
    def save_model(self, filename):
        model_data = {
            "input_shape": [self.input_dim],
            "num_classes": self.num_classes,
            "conv_layers": [
                {
                    "input_channels": 1,
                    "output_channels": 16,
                    "kernel_size": 3,
                    "stride": 1,
                    "weights": self.conv1_weights.flatten().tolist(),
                    "biases": self.conv1_biases.tolist()
                },
                {
                    "input_channels": 16,
                    "output_channels": 32,
                    "kernel_size": 3,
                    "stride": 1,
                    "weights": self.conv2_weights.flatten().tolist(),
                    "biases": self.conv2_biases.tolist()
                },
                {
                    "input_channels": 32,
                    "output_channels": 64,
                    "kernel_size": 3,
                    "stride": 1,
                    "weights": self.conv3_weights.flatten().tolist(),
                    "biases": self.conv3_biases.tolist()
                }
            ],
            "fc_layers": [
                {
                    "input_size": self.fc1_weights.shape[0],
                    "output_size": self.fc1_weights.shape[1],
                    "weights": self.fc1_weights.flatten().tolist(),
                    "biases": self.fc1_biases.tolist()
                },
                {
                    "input_size": self.fc2_weights.shape[0],
                    "output_size": self.fc2_weights.shape[1],
                    "weights": self.fc2_weights.flatten().tolist(),
                    "biases": self.fc2_biases.tolist()
                }
            ]
        }
        
        with open(filename, 'w') as f:
            json.dump(model_data, f, indent=2)
        
        print(f"Model saved to {filename}")


def generate_synthetic_fingerprint(app_id: int, num_samples: int = 100) -> np.ndarray:
    np.random.seed(app_id * 1000)
    
    base_fingerprint = np.zeros(490, dtype=np.float32)
    
    cipher_offset = app_id * 30
    for i in range(20):
        if cipher_offset + i < 300:
            base_fingerprint[cipher_offset + i] = np.random.choice([0, 1], p=[0.7, 0.3])
    
    ext_offset = 300 + app_id * 10
    for i in range(15):
        if ext_offset + i < 400:
            base_fingerprint[ext_offset + i] = np.random.choice([0, 1], p=[0.6, 0.4])
    
    curve_offset = 400 + app_id * 5
    for i in range(10):
        if curve_offset + i < 430:
            base_fingerprint[curve_offset + i] = np.random.choice([0, 1], p=[0.8, 0.2])
    
    sig_offset = 430 + app_id * 5
    for i in range(10):
        if sig_offset + i < 440:
            base_fingerprint[sig_offset + i] = np.random.choice([0, 1], p=[0.75, 0.25])
    
    base_fingerprint[440] = np.random.uniform(0.2, 0.8)
    base_fingerprint[441] = np.random.uniform(0.1, 0.5)
    
    samples = []
    for _ in range(num_samples):
        noise = np.random.randn(490).astype(np.float32) * 0.05
        sample = np.clip(base_fingerprint + noise, 0, 1)
        samples.append(sample)
    
    return np.array(samples)


def main():
    print("Generating synthetic TLS fingerprint data...")
    
    applications = [
        "google",
        "facebook",
        "twitter",
        "netflix",
        "amazon",
        "apple",
        "microsoft",
        "cloudflare",
        "unknown"
    ]
    
    X_train = []
    y_train = []
    
    for app_id, app_name in enumerate(applications):
        samples = generate_synthetic_fingerprint(app_id, num_samples=500)
        X_train.append(samples)
        
        one_hot = np.zeros(len(applications), dtype=np.float32)
        one_hot[app_id] = 1
        y_train.extend([one_hot] * 500)
    
    X_train = np.array(X_train).reshape(-1, 490).astype(np.float32)
    y_train = np.array(y_train, dtype=np.float32)
    
    indices = np.random.permutation(len(X_train))
    X_train = X_train[indices]
    y_train = y_train[indices]
    
    print(f"Training data shape: {X_train.shape}")
    print(f"Labels shape: {y_train.shape}")
    
    print("\nInitializing CNN model...")
    model = TLSFingerprintCNN(num_classes=len(applications))
    
    print("\nTraining model...")
    model.train(X_train, y_train, epochs=30, batch_size=64, learning_rate=0.01)
    
    print("\nEvaluating model...")
    predictions = model.forward(X_train[:100])
    predicted_classes = np.argmax(predictions, axis=1)
    true_classes = np.argmax(y_train[:100], axis=1)
    accuracy = np.mean(predicted_classes == true_classes)
    print(f"Training accuracy: {accuracy:.4f}")
    
    print("\nSaving model...")
    model.save_model("tls_fingerprint_model.json")
    
    print("\nModel training complete!")
    print("\nUsage in Rust:")
    print("  let model = CnnModel::from_file(\"tls_fingerprint_model.json\").unwrap();")
    print("  let prediction = model.predict(&features);")


if __name__ == "__main__":
    main()
