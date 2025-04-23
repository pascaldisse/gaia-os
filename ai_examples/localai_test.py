#!/usr/bin/env python3
"""
LocalAI test script for GAIA-OS
This script demonstrates basic LocalAI functionality.
"""

import requests
import json
import time
import sys
import os
from datetime import datetime

# Configuration
LOCALAI_URL = "http://localhost:8080"
TIMEOUT = 5  # seconds

def print_header(text):
    """Print a formatted header."""
    print("\n" + "=" * 60)
    print(f"  {text}")
    print("=" * 60)

def check_localai_status():
    """Check if LocalAI is running and return its status."""
    try:
        response = requests.get(f"{LOCALAI_URL}/v1/models", timeout=TIMEOUT)
        if response.status_code == 200:
            return True, response.json()
        else:
            return False, f"LocalAI returned status code: {response.status_code}\n{response.text}"
    except requests.exceptions.ConnectionError:
        return False, "Connection error: LocalAI is not running or the connection was refused"
    except requests.exceptions.Timeout:
        return False, "Connection timeout: LocalAI is taking too long to respond"
    except Exception as e:
        return False, f"Error connecting to LocalAI: {e}"

def get_available_models():
    """Get a list of available models from LocalAI."""
    try:
        response = requests.get(f"{LOCALAI_URL}/v1/models", timeout=TIMEOUT)
        if response.status_code == 200:
            return response.json()
        return []
    except Exception:
        return []

def test_text_completion(model="gpt-3.5-turbo"):
    """Test text completion API."""
    print_header(f"Testing Text Completion with {model}")
    
    try:
        response = requests.post(
            f"{LOCALAI_URL}/v1/chat/completions",
            json={
                "model": model,
                "messages": [{"role": "user", "content": "What is GAIA-OS?"}],
                "temperature": 0.7,
                "max_tokens": 100
            },
            timeout=30  # Longer timeout for generation
        )
        
        if response.status_code == 200:
            result = response.json()
            print("Success! Response received:")
            print(f"Model: {model}")
            content = result.get('choices', [{}])[0].get('message', {}).get('content', '')
            print(f"Response: {content}")
            return True
        else:
            print(f"Failed with status code: {response.status_code}")
            print(response.text)
            return False
    except Exception as e:
        print(f"Error during text completion: {e}")
        return False

def test_embeddings(model="text-embedding-ada-002"):
    """Test embeddings API."""
    print_header(f"Testing Embeddings with {model}")
    
    try:
        response = requests.post(
            f"{LOCALAI_URL}/v1/embeddings",
            json={
                "model": model,
                "input": "GAIA-OS is an AI-integrated operating system."
            },
            timeout=10
        )
        
        if response.status_code == 200:
            result = response.json()
            print("Success! Embeddings received:")
            print(f"Model: {model}")
            embedding = result.get('data', [{}])[0].get('embedding', [])
            print(f"Embedding dimensions: {len(embedding)}")
            print(f"First 5 values: {embedding[:5]}")
            return True
        else:
            print(f"Failed with status code: {response.status_code}")
            print(response.text)
            return False
    except Exception as e:
        print(f"Error during embeddings request: {e}")
        return False

def test_health():
    """Test health endpoint."""
    print_header("Testing Health Endpoint")
    
    try:
        response = requests.get(f"{LOCALAI_URL}/health", timeout=TIMEOUT)
        print(f"Status code: {response.status_code}")
        print(f"Response: {response.text}")
        return response.status_code == 200
    except Exception as e:
        print(f"Error checking health: {e}")
        return False

def main():
    """Main function to run LocalAI tests."""
    print("LocalAI Test Script for GAIA-OS")
    print(f"Testing LocalAI at: {LOCALAI_URL}")
    print(f"Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    
    # Check if LocalAI is running
    print("Checking if LocalAI is running...")
    status, result = check_localai_status()
    
    if status:
        print("✅ LocalAI is running!")
        models = result
        print(f"Available models: {json.dumps(models, indent=2)}")
    else:
        print(f"❌ LocalAI is not running properly: {result}")
        print("\nPlease make sure LocalAI is running with:")
        print("  run-localai")
        sys.exit(1)
    
    # Run tests
    test_results = {
        "health": test_health(),
        "completion": False,
        "embeddings": False
    }
    
    # Try to find an appropriate model for completion
    available_models = get_available_models()
    completion_models = [
        model["id"] for model in available_models 
        if any(name in model["id"].lower() for name in ["gpt", "llama", "mistral", "falcon"])
    ]
    
    if completion_models:
        test_results["completion"] = test_text_completion(completion_models[0])
    else:
        print_header("Testing Text Completion")
        print("No suitable completion models found. Trying with default 'gpt-3.5-turbo'...")
        test_results["completion"] = test_text_completion()
    
    # Try to find an embedding model
    embedding_models = [
        model["id"] for model in available_models 
        if "embed" in model["id"].lower()
    ]
    
    if embedding_models:
        test_results["embeddings"] = test_embeddings(embedding_models[0])
    else:
        print_header("Testing Embeddings")
        print("No embedding models found. Trying with default 'text-embedding-ada-002'...")
        test_results["embeddings"] = test_embeddings()
    
    # Summary
    print_header("Test Summary")
    print("LocalAI Test Results:")
    for test, result in test_results.items():
        status = "✅ PASSED" if result else "❌ FAILED"
        print(f"{test.title()}: {status}")
    
    all_passed = all(test_results.values())
    print("\nOverall Status:", "✅ PASSED" if all_passed else "❌ FAILED")
    
    if not all_passed:
        print("\nTroubleshooting Tips:")
        print("1. Make sure LocalAI is running with 'run-localai'")
        print("2. Check if the models are properly loaded")
        print("3. Verify you have downloaded at least one LLM model")
        print("4. Check LocalAI logs for errors")
    else:
        print("\nLocalAI is working correctly on GAIA-OS!")
        print("You can now use it for AI applications and development.")
    
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())