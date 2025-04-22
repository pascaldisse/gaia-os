#!/usr/bin/env python3
"""
Gaia Assistant - Simple AI assistant for Gaia OS
This script provides a basic command-line AI assistant interface
that can be expanded with more advanced capabilities.
"""

import os
import sys
import json
import argparse
import subprocess
from datetime import datetime

# In a real implementation, you would import ML libraries
# import tensorflow as tf
# import torch
# import numpy as np

CONFIG_DIR = os.path.expanduser("~/.config/gaia-assistant")
HISTORY_FILE = os.path.join(CONFIG_DIR, "history.json")

class GaiaAssistant:
    """
    Basic AI assistant for Gaia OS that can be extended with
    more sophisticated ML models and capabilities.
    """
    
    def __init__(self):
        """Initialize the assistant and load configuration."""
        os.makedirs(CONFIG_DIR, exist_ok=True)
        self.history = self._load_history()
        
        # In the future, load ML models here
        # self.model = tf.keras.models.load_model("/path/to/model")
        
        # Basic command mappings - will be replaced with ML inference
        self.commands = {
            "memory": self._check_memory,
            "cpu": self._check_cpu,
            "system": self._check_system,
            "help": self._show_help,
            "time": lambda: f"Current time: {datetime.now().strftime('%H:%M:%S')}",
            "date": lambda: f"Today's date: {datetime.now().strftime('%Y-%m-%d')}",
        }
    
    def respond_to(self, query):
        """Generate a response to the user's query."""
        # Log the query in history
        self._add_to_history(query)
        
        # Simple keyword matching (to be replaced with NLP)
        query_lower = query.lower()
        
        # Check for direct command matches
        for cmd, func in self.commands.items():
            if cmd in query_lower:
                return func()
        
        # In a real implementation, use ML model inference here
        # return self.model.predict(query)
        
        # Fallback response
        return "I'm a simple Gaia OS assistant prototype. In the full version, I'll use ML models to provide intelligent responses."
    
    def _check_memory(self):
        """Check system memory usage."""
        try:
            if sys.platform == 'darwin':  # macOS
                # Use vm_stat on macOS
                result = subprocess.run(["vm_stat"], capture_output=True, text=True)
                return f"Memory Info (macOS):\n{result.stdout}"
            else:  # Linux
                result = subprocess.run(["free", "-h"], capture_output=True, text=True)
                return result.stdout
        except Exception as e:
            return f"Error checking memory: {str(e)}"
    
    def _check_cpu(self):
        """Check CPU usage."""
        try:
            if sys.platform == 'darwin':  # macOS
                # Use top on macOS with different flags
                result = subprocess.run(["top", "-l", "1", "-n", "5"], capture_output=True, text=True)
                lines = result.stdout.split("\n")
                # Get header and first few processes
                header_lines = [l for l in lines[:15] if l.strip()]
                return "\n".join(header_lines)
            else:  # Linux
                result = subprocess.run(["top", "-bn1"], capture_output=True, text=True)
                return "\n".join(result.stdout.split("\n")[:12])
        except Exception as e:
            return f"Error checking CPU: {str(e)}"
    
    def _check_system(self):
        """Check general system information."""
        try:
            if sys.platform == 'darwin':  # macOS
                try:
                    # Try neofetch first
                    result = subprocess.run(["neofetch", "--stdout"], capture_output=True, text=True)
                    return result.stdout
                except:
                    # Fall back to system_profiler on macOS
                    result = subprocess.run(["system_profiler", "SPSoftwareDataType", "SPHardwareDataType"], 
                                          capture_output=True, text=True)
                    return result.stdout
            else:  # Linux
                result = subprocess.run(["neofetch", "--stdout"], capture_output=True, text=True)
                return result.stdout
        except Exception as e:
            return f"System information not available: {str(e)}"
    
    def _show_help(self):
        """Show available commands."""
        commands = ", ".join(self.commands.keys())
        return f"Available commands: {commands}"
    
    def _load_history(self):
        """Load query history from file."""
        if os.path.exists(HISTORY_FILE):
            try:
                with open(HISTORY_FILE, 'r') as f:
                    return json.load(f)
            except Exception:
                return []
        return []
    
    def _add_to_history(self, query):
        """Add a query to the history."""
        timestamp = datetime.now().isoformat()
        self.history.append({"query": query, "timestamp": timestamp})
        
        # Keep history limited to last 100 queries
        if len(self.history) > 100:
            self.history = self.history[-100:]
            
        # Save history
        try:
            with open(HISTORY_FILE, 'w') as f:
                json.dump(self.history, f, indent=2)
        except Exception:
            pass  # Fail silently for history saving

def main():
    """Main function to run the assistant."""
    parser = argparse.ArgumentParser(description="Gaia OS AI Assistant")
    parser.add_argument("query", nargs="*", help="Question or command for the assistant")
    parser.add_argument("--interactive", "-i", action="store_true", help="Run in interactive mode")
    args = parser.parse_args()
    
    assistant = GaiaAssistant()
    
    if args.interactive:
        print("Gaia Assistant - Interactive Mode (type 'exit' to quit)")
        print("------------------------------------------------------")
        while True:
            try:
                query = input("\nYou: ")
                if query.lower() in ("exit", "quit"):
                    break
                response = assistant.respond_to(query)
                print(f"\nGaia: {response}")
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Error: {str(e)}")
    elif args.query:
        query = " ".join(args.query)
        response = assistant.respond_to(query)
        print(response)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()