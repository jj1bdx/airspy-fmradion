#!/usr/bin/env python3
"""Interleave two mono 32-bit raw files into one stereo raw file.

Usage: interleave32.py LEFT RIGHT OUT
"""
import sys
from array import array

left = array("i")
right = array("i")
with open(sys.argv[1], "rb") as f:
    left.frombytes(f.read())
with open(sys.argv[2], "rb") as f:
    right.frombytes(f.read())
n = min(len(left), len(right))
out = array("i", bytes(8 * n))
out[0::2] = left[:n]
out[1::2] = right[:n]
with open(sys.argv[3], "wb") as f:
    out.tofile(f)
