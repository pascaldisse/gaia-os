#!/usr/bin/env python3
"""
Gaia OS System Monitor with AI Predictions
This tool monitors system resources and uses simple ML to predict resource usage trends.
"""

import os
import sys
import time
import json
import argparse
import datetime
import subprocess
import signal
import threading
import numpy as np
from collections import deque

# Constants
DATA_DIR = os.path.expanduser("~/.local/share/gaia/system-monitor")
CONFIG_FILE = os.path.expanduser("~/.config/gaia/system-monitor.conf")
MAX_HISTORY = 1000  # Maximum number of data points to keep in memory

class GaiaSystemMonitor:
    """System monitor with AI prediction capabilities."""
    
    def __init__(self):
        """Initialize the system monitor."""
        # Create data directory if it doesn't exist
        os.makedirs(DATA_DIR, exist_ok=True)
        
        # Initialize data structures
        self.cpu_history = deque(maxlen=MAX_HISTORY)
        self.memory_history = deque(maxlen=MAX_HISTORY)
        self.disk_history = deque(maxlen=MAX_HISTORY)
        self.temperature_history = deque(maxlen=MAX_HISTORY)
        
        # Set up configuration
        self.interval = 5  # Default: collect data every 5 seconds
        self.prediction_window = 12  # Default: predict 1 minute ahead (12 * 5 seconds)
        self.running = False
        
        # Load existing data if available
        self._load_data()
    
    def start_monitoring(self, interval=None):
        """Start monitoring system resources."""
        if interval is not None:
            self.interval = interval
        
        self.running = True
        print(f"Starting Gaia System Monitor (interval: {self.interval}s)")
        print("Press Ctrl+C to stop monitoring")
        
        # Register signal handler for graceful shutdown
        signal.signal(signal.SIGINT, self._handle_interrupt)
        
        # Start the monitoring loop
        self._monitoring_loop()
    
    def _monitoring_loop(self):
        """Main monitoring loop."""
        while self.running:
            try:
                # Collect system data
                cpu_usage = self._get_cpu_usage()
                memory_usage = self._get_memory_usage()
                disk_usage = self._get_disk_usage()
                temperature = self._get_temperature()
                
                # Store data
                timestamp = datetime.datetime.now().isoformat()
                data_point = {
                    "timestamp": timestamp,
                    "cpu": cpu_usage,
                    "memory": memory_usage,
                    "disk": disk_usage,
                    "temperature": temperature
                }
                
                # Add to history
                self.cpu_history.append(cpu_usage)
                self.memory_history.append(memory_usage)
                self.disk_history.append(disk_usage)
                self.temperature_history.append(temperature)
                
                # Save data periodically (every 60 data points)
                if len(self.cpu_history) % 60 == 0:
                    self._save_data()
                
                # Print current status
                self._print_status(data_point)
                
                # Sleep until next collection
                time.sleep(self.interval)
            
            except Exception as e:
                print(f"Error in monitoring loop: {str(e)}")
                time.sleep(self.interval)
    
    def _print_status(self, data):
        """Print current system status with predictions."""
        # Clear screen (cross-platform)
        os.system('cls' if os.name=='nt' else 'clear')
        
        print("=== GAIA OS SYSTEM MONITOR ===")
        print(f"Time: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("")
        
        print("--- CURRENT STATUS ---")
        print(f"CPU Usage:     {data['cpu']:.1f}%")
        print(f"Memory Usage:  {data['memory']:.1f}%")
        print(f"Disk Usage:    {data['disk']:.1f}%")
        if data['temperature'] is not None:
            print(f"Temperature:   {data['temperature']:.1f}°C")
        else:
            print("Temperature:   Not available")
        
        # Only predict if we have enough data
        if len(self.cpu_history) >= 30:
            print("")
            print("--- AI PREDICTIONS (1 minute ahead) ---")
            cpu_pred = self._predict_resource(self.cpu_history)
            mem_pred = self._predict_resource(self.memory_history)
            disk_pred = self._predict_resource(self.disk_history)
            
            # Print predictions with trend arrows
            cpu_trend = "↑" if cpu_pred > data['cpu'] else "↓"
            mem_trend = "↑" if mem_pred > data['memory'] else "↓"
            disk_trend = "↑" if disk_pred > data['disk'] else "↓"
            
            print(f"CPU Usage:     {cpu_pred:.1f}% {cpu_trend}")
            print(f"Memory Usage:  {mem_pred:.1f}% {mem_trend}")
            print(f"Disk Usage:    {disk_pred:.1f}% {disk_trend}")
            
            # Print potential issues
            if cpu_pred > 90:
                print("\nWARNING: High CPU usage predicted. Consider closing intensive applications.")
            if mem_pred > 90:
                print("\nWARNING: High memory usage predicted. Check for memory leaks or close applications.")
    
    def _predict_resource(self, history):
        """
        Simple prediction using linear regression.
        In a real implementation, this would use more sophisticated ML models.
        """
        # Convert to numpy array for calculations
        data = np.array(list(history))
        
        # Use simple linear regression for prediction
        x = np.arange(len(data))
        if len(x) > 0:
            # Fit linear regression
            slope, intercept = np.polyfit(x, data, 1)
            
            # Predict next value (add prediction_window to the last index)
            next_x = len(data) - 1 + self.prediction_window
            prediction = slope * next_x + intercept
            
            # Constrain prediction to reasonable bounds
            prediction = max(0, min(100, prediction))
            
            return prediction
        
        return 0
    
    def _get_cpu_usage(self):
        """Get current CPU usage."""
        try:
            if sys.platform == 'darwin':  # macOS
                # Use macOS-specific top command
                output = subprocess.check_output(['top', '-l', '1', '-n', '0']).decode('utf-8')
                cpu_line = [line for line in output.split('\n') if 'CPU usage' in line][0]
                # Parse the CPU usage line which looks like "CPU usage: 10.64% user, 14.35% sys, 75.00% idle"
                idle = float(cpu_line.split('idle')[0].split()[-1].replace('%', ''))
                return 100 - idle
            else:  # Linux
                output = subprocess.check_output(['top', '-bn1']).decode('utf-8')
                cpu_line = [line for line in output.split('\n') if '%Cpu' in line][0]
                cpu_usage = 100 - float(cpu_line.split('id,')[1].split('ni,')[0].strip())
                return cpu_usage
        except Exception as e:
            try:
                # Fallback method
                total = 0
                count = 0
                for line in subprocess.check_output(['ps', '-A', '-o', '%cpu']).decode().split('\n')[1:]:
                    if line.strip():
                        total += float(line.strip())
                        count += 1
                return total if count == 0 else total / count
            except:
                return 0
    
    def _get_memory_usage(self):
        """Get current memory usage."""
        try:
            if sys.platform == 'darwin':  # macOS
                # Use vm_stat to get memory info on macOS
                output = subprocess.check_output(['vm_stat']).decode('utf-8')
                lines = output.split('\n')
                
                # Parse page sizes
                page_size = 4096  # Default page size, usually 4K in bytes
                if "page size of" in output:
                    page_size_line = [l for l in lines if "page size of" in l][0]
                    page_size = int(page_size_line.split("page size of")[1].strip().split()[0])
                
                # Get memory stats
                pages_free = int(lines[1].split(':')[1].strip().replace('.', ''))
                pages_active = int(lines[2].split(':')[1].strip().replace('.', ''))
                pages_inactive = int(lines[3].split(':')[1].strip().replace('.', ''))
                pages_speculative = int(lines[4].split(':')[1].strip().replace('.', ''))
                pages_wired = int([l for l in lines if "Pages wired down" in l][0].split(':')[1].strip().replace('.', ''))
                
                # Calculate memory usage
                free_mem = (pages_free + pages_inactive + pages_speculative) * page_size
                used_mem = (pages_active + pages_wired) * page_size
                total_mem = free_mem + used_mem
                
                return (used_mem / total_mem) * 100
            else:  # Linux
                output = subprocess.check_output(['free', '-m']).decode('utf-8')
                memory_line = output.split('\n')[1]
                total = float(memory_line.split()[1])
                used = float(memory_line.split()[2])
                return (used / total) * 100
        except Exception as e:
            return 0
    
    def _get_disk_usage(self):
        """Get current disk usage."""
        try:
            # Works on both macOS and Linux
            output = subprocess.check_output(['df', '-h', '/']).decode('utf-8')
            lines = output.split('\n')
            if len(lines) > 1:
                disk_line = lines[1]
                # macOS and Linux have different df output formats
                # Looking for the percentage field which usually has a % sign
                for field in disk_line.split():
                    if '%' in field:
                        return float(field.rstrip('%'))
                
                # If no % found, try common positions (5th field in Linux, 4th in some versions)
                fields = disk_line.split()
                if len(fields) >= 5:
                    # Try the usual positions
                    for pos in [4, 3]:
                        if pos < len(fields):
                            try:
                                return float(fields[pos].rstrip('%'))
                            except ValueError:
                                continue
            return 0
        except Exception as e:
            return 0
    
    def _get_temperature(self):
        """Get current CPU temperature if available."""
        try:
            if sys.platform == 'darwin':  # macOS
                try:
                    # Try using osx-cpu-temp if installed (brew install osx-cpu-temp)
                    output = subprocess.check_output(['osx-cpu-temp']).decode('utf-8')
                    # Output format: "CPU: 45.6°C"
                    temp = float(output.split('CPU:')[1].split('°C')[0].strip())
                    return temp
                except:
                    # Fallback to system_profiler on macOS
                    try:
                        output = subprocess.check_output(['system_profiler', 'SPPowerDataType']).decode('utf-8')
                        temp_lines = [line for line in output.split('\n') if 'Temperature' in line]
                        if temp_lines:
                            # Extract temperature from line like "CPU Die Temperature: 45.67 C"
                            temp = float(temp_lines[0].split(':')[1].split('C')[0].strip())
                            return temp
                    except:
                        pass
            else:  # Linux
                # Try getting temperature from thermal zones
                if os.path.exists('/sys/class/thermal/thermal_zone0/temp'):
                    with open('/sys/class/thermal/thermal_zone0/temp', 'r') as f:
                        temp = int(f.read().strip()) / 1000
                    return temp
            return None
        except:
            return None
    
    def _save_data(self):
        """Save collected data to disk."""
        timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
        filename = os.path.join(DATA_DIR, f'system_data_{timestamp}.json')
        
        data = {
            "timestamp": timestamp,
            "cpu_history": list(self.cpu_history),
            "memory_history": list(self.memory_history),
            "disk_history": list(self.disk_history),
            "temperature_history": list(self.temperature_history)
        }
        
        try:
            with open(filename, 'w') as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"Error saving data: {str(e)}")
    
    def _load_data(self):
        """Load previously saved data if available."""
        try:
            # Find the newest data file
            files = [f for f in os.listdir(DATA_DIR) if f.startswith('system_data_')]
            if not files:
                return
            
            newest_file = max(files)
            filepath = os.path.join(DATA_DIR, newest_file)
            
            with open(filepath, 'r') as f:
                data = json.load(f)
            
            # Load data into history
            self.cpu_history.extend(data.get('cpu_history', []))
            self.memory_history.extend(data.get('memory_history', []))
            self.disk_history.extend(data.get('disk_history', []))
            self.temperature_history.extend(data.get('temperature_history', []))
            
            print(f"Loaded {len(self.cpu_history)} historical data points")
        
        except Exception as e:
            print(f"Error loading data: {str(e)}")
    
    def _handle_interrupt(self, sig, frame):
        """Handle keyboard interrupt."""
        print("\nStopping system monitor...")
        self.running = False
        self._save_data()
        print("Data saved. Exiting.")
        sys.exit(0)

def main():
    """Main function to run the system monitor."""
    parser = argparse.ArgumentParser(description="Gaia OS System Monitor with AI Predictions")
    parser.add_argument("--interval", type=int, default=5,
                       help="Data collection interval in seconds (default: 5)")
    parser.add_argument("--graphical", action="store_true",
                       help="Show graphical output (requires matplotlib)")
    args = parser.parse_args()
    
    monitor = GaiaSystemMonitor()
    
    if args.graphical:
        try:
            import matplotlib.pyplot as plt
            # Graphical monitoring would be implemented here
            print("Graphical monitoring not implemented in this version")
            monitor.start_monitoring(args.interval)
        except ImportError:
            print("Matplotlib is required for graphical output")
            print("Install with: pip install matplotlib")
            monitor.start_monitoring(args.interval)
    else:
        monitor.start_monitoring(args.interval)

if __name__ == "__main__":
    main()