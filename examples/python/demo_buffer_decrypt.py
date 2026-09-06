#!/usr/bin/env python3
"""
mp4decrypt-api - In-Memory RAM Buffer Decryption Demo (Python)
=============================================================

Demonstrates Paradigm 2: Decrypting media stored entirely in RAM memory
buffers using both the object-oriented Mp4DecryptSession and the one-shot
convenience method Mp4DecryptSession.decrypt_buffer().

Run with:
    python examples/python/demo_buffer_decrypt.py
"""

import os
import sys
import time
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession


def main():
    print("=" * 67)
    print("     MP4DECRYPT-API - IN-MEMORY BUFFER DECRYPTION (PYTHON)         ")
    print("=" * 67)

    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    kid_hex = "9eb4050de44b4802932e27d75083e266"
    key_hex = "166634c675823c235a4a9446fad52e4d"

    print("1. Downloading encrypted fragments into memory...")
    init_bytes = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()
    seg_bytes = urllib.request.urlopen(f"{base_url}/15/0001.m4s").read()
    encrypted_buffer = init_bytes + seg_bytes

    print(f"   Buffer size in RAM: {len(encrypted_buffer):,} bytes")

    # Method A: Object-oriented session
    print("\n2. Decrypting via Mp4DecryptSession context...")
    t0 = time.perf_counter()
    with Mp4DecryptSession() as session:
        session.add_key(kid_hex, key_hex)
        decrypted_a = session.decrypt_memory(encrypted_buffer)
        stats = session.get_stats()
    duration_a = (time.perf_counter() - t0) * 1000.0

    print(f"   Session output size: {len(decrypted_a):,} bytes")
    print(f"   Execution time:     {duration_a:.2f} ms (native pipeline: {stats.execution_duration_ms:.2f} ms)")

    # Method B: One-shot convenience method
    print("\n3. Decrypting via one-shot Mp4DecryptSession.decrypt_buffer()...")
    t1 = time.perf_counter()
    decrypted_b = Mp4DecryptSession.decrypt_buffer(encrypted_buffer, kid_hex, key_hex)
    duration_b = (time.perf_counter() - t1) * 1000.0

    print(f"   One-shot output size: {len(decrypted_b):,} bytes")
    print(f"   Total duration:       {duration_b:.2f} ms")

    # Verification
    print("\n4. Verifying media contents in-memory (zero disk I/O)...")
    info = Mp4DecryptSession.probe(decrypted_b)
    print(f"   Track count:         {info.track_count}")
    print(f"   Track #1 codec:      {info.tracks[0].codec}")
    print(f"   Track #1 encrypted:  {info.tracks[0].is_encrypted}")

    if not info.tracks[0].is_encrypted and decrypted_a == decrypted_b:
        print("\nSUCCESS: Both in-memory decryption paradigms produced identical plaintext!")
    else:
        raise RuntimeError("Decryption verification failed.")


if __name__ == "__main__":
    main()
