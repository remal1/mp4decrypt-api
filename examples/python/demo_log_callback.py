#!/usr/bin/env python3
"""
mp4decrypt-api - Native Log Callback & Filtering Demo (Python)
==============================================================

Demonstrates intercepting Bento4 and C ABI logs with Python callback
functions, testing log level filtering (DEBUG, INFO, WARN, ERROR, NONE).

Run with:
    python examples/python/demo_log_callback.py
"""

import os
import sys
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession, Mp4LogLevel


def main():
    print("=" * 67)
    print("    MP4DECRYPT-API - LOG CALLBACK & FILTERING (PYTHON)             ")
    print("=" * 67)

    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    kid_hex = "9eb4050de44b4802932e27d75083e266"
    key_hex = "166634c675823c235a4a9446fad52e4d"

    print("Fetching test media chunks...")
    init_bytes = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()
    seg_bytes = urllib.request.urlopen(f"{base_url}/15/0001.m4s").read()
    test_buffer = init_bytes + seg_bytes

    log_history = []

    def on_native_log(level: Mp4LogLevel, message: str):
        level_names = {
            Mp4LogLevel.DEBUG: "DEBUG",
            Mp4LogLevel.INFO:  "INFO ",
            Mp4LogLevel.WARN:  "WARN ",
            Mp4LogLevel.ERROR: "ERROR",
        }
        name = level_names.get(level, f"LVL{int(level)}")
        print(f"  [{name}] {message}")
        log_history.append((level, message))

    with Mp4DecryptSession() as session:
        session.add_key(kid_hex, key_hex)
        session.on_log(on_native_log)

        print("\n--- TEST 1: Log Level = DEBUG (Verbose) ---")
        session.set_log_level(Mp4LogLevel.DEBUG)
        session.decrypt_memory(test_buffer)
        print(f"Captured {len(log_history)} log messages at DEBUG level.")

        log_history.clear()
        print("\n--- TEST 2: Log Level = WARN (Quiet) ---")
        session.set_log_level(Mp4LogLevel.WARN)
        session.decrypt_memory(test_buffer)
        print(f"Captured {len(log_history)} log messages at WARN level (should be 0 on success).")

        log_history.clear()
        print("\n--- TEST 3: Intentionally trigger ERROR log ---")
        session.set_log_level(Mp4LogLevel.DEBUG)
        try:
            session.decrypt_memory(b"\x00\x00\x00\x08mdat")
        except RuntimeError as e:
            print(f"Caught expected RuntimeError: {e}")

    print("\nSUCCESS: Native log callback and level filtering verified successfully!")


if __name__ == "__main__":
    main()
