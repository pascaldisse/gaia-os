#!/usr/bin/env python3
"""
Symbolic Number Representation System
Efficiently represents large numbers with minimal characters
"""

import math
import sys

# Extended character set for compactness
SYMBOLS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!@#$%^&*()-_=+[]{};:,.<>/?|`~"

class SymbolicNumber:
    """
    A compact representation system for numbers that uses exponential notation
    and a custom symbol set to minimize character count.
    """
    
    def __init__(self):
        self.base = len(SYMBOLS)
    
    def encode(self, number):
        """Convert a number to symbolic representation"""
        if number == 0:
            return SYMBOLS[0]
        
        # For very large numbers, use exponential notation
        if number > 1000000:
            exponent = math.floor(math.log10(number))
            mantissa = number / (10 ** exponent)
            
            # Encode mantissa in our base system
            mantissa_str = self._encode_base(mantissa * 100)
            # Encode exponent in our base system
            exponent_str = self._encode_base(exponent)
            
            return f"{mantissa_str}ᴇ{exponent_str}"
            
        # For medium-sized numbers, use our base system directly
        return self._encode_base(number)
    
    def decode(self, symbolic):
        """Convert symbolic representation back to a number"""
        # Check for exponential notation
        if 'ᴇ' in symbolic:
            mantissa_str, exponent_str = symbolic.split('ᴇ')
            mantissa = self._decode_base(mantissa_str) / 100
            exponent = self._decode_base(exponent_str)
            return mantissa * (10 ** exponent)
        
        # Regular base conversion
        return self._decode_base(symbolic)
    
    def _encode_base(self, number):
        """Encode a number in our custom base system"""
        number = int(number)
        if number == 0:
            return SYMBOLS[0]
        
        result = ""
        while number > 0:
            result = SYMBOLS[number % self.base] + result
            number //= self.base
            
        return result
    
    def _decode_base(self, symbolic):
        """Decode from our custom base system"""
        result = 0
        for char in symbolic:
            result = result * self.base + SYMBOLS.index(char)
        return result

def format_number(number):
    """Format a number using our symbolic representation"""
    converter = SymbolicNumber()
    return converter.encode(number)

def parse_number(symbolic):
    """Parse a symbolic representation back to a number"""
    converter = SymbolicNumber()
    return converter.decode(symbolic)

if __name__ == "__main__":
    # Command-line interface
    if len(sys.argv) > 1:
        try:
            # If number, encode it
            number = float(sys.argv[1])
            print(f"Symbolic: {format_number(number)}")
        except ValueError:
            # If symbolic, decode it
            symbolic = sys.argv[1]
            print(f"Number: {parse_number(symbolic)}")
    else:
        print("Usage: number_system.py <number or symbolic>")
        print("Examples:")
        converter = SymbolicNumber()
        examples = [42, 800, 1000000, 9876543210, 10**15]
        for num in examples:
            sym = converter.encode(num)
            print(f"{num} → {sym} ({len(str(num))} chars → {len(sym)} chars)")