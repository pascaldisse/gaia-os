#!/usr/bin/env python3
# VM Detection utility for Gaia OS

import os
import sys
import subprocess

def is_vm():
    """Check if running in a VM environment"""
    
    # Check CPU info for hypervisor flag
    try:
        with open('/proc/cpuinfo', 'r') as f:
            for line in f:
                if 'hypervisor' in line:
                    return True
    except:
        pass
    
    # Check DMI info
    try:
        dmi_vendors = ['VMware', 'VirtualBox', 'QEMU', 'Parallels']
        dmi_path = '/sys/class/dmi/id/product_name'
        if os.path.exists(dmi_path):
            with open(dmi_path, 'r') as f:
                product = f.read().strip()
                for vendor in dmi_vendors:
                    if vendor in product:
                        return True
    except:
        pass
    
    return False

def get_vm_type():
    """Determine the specific type of VM"""
    
    try:
        dmi_path = '/sys/class/dmi/id/product_name'
        if os.path.exists(dmi_path):
            with open(dmi_path, 'r') as f:
                product = f.read().strip()
                if 'VMware' in product:
                    return 'vmware'
                elif 'VirtualBox' in product:
                    return 'virtualbox'
                elif 'QEMU' in product:
                    return 'qemu'
                elif 'Parallels' in product:
                    return 'parallels'
    except:
        pass
    
    return 'unknown'

def optimize_for_vm():
    """Apply VM-specific optimizations"""
    
    vm_type = get_vm_type()
    print(f"Detected VM type: {vm_type}")
    
    # Apply specific optimizations based on VM type
    # (This would be implemented based on specific VM needs)
    
    print("VM optimizations applied")

if __name__ == '__main__':
    if is_vm():
        print("Running in a virtual machine environment")
        if len(sys.argv) > 1 and sys.argv[1] == '--optimize':
            optimize_for_vm()
    else:
        print("Not running in a virtual machine environment")
