#!/usr/bin/env python3
"""Build the actual HakiOS diagnostic kernel; no device operations."""
import argparse
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location(
    "r39_complete_kernel_builder",ROOT/"scripts/build-hakios-r38-thermal.py")
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision",choices=("rate01","rate02","rate03","rate04","rate05","rate06","rate07","rate08","rate09","rate10"),default="rate01")
    args=parser.parse_args()
    builder.main("r39-"+args.revision,ROOT/"evidence/cpu-r39-20261121/main")
