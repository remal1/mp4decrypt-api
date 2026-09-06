#!/usr/bin/env python3
"""
mp4decrypt-api - Parallel Multithreaded Decryption Demo (Python)
================================================================

Demonstrates thread-safety by running multiple concurrent decryption sessions
simultaneously across worker threads using concurrent.futures.ThreadPoolExecutor.

Run with:
    python examples/python/demo_async.py
"""

import concurrent.futures
import os
import sys
import time
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession


def decrypt_worker(task_id: int, data: bytes, kid: str, key: str):
    with Mp4DecryptSession() as session:
        session.add_key(kid, key)
        output = session.decrypt_memory(data)
        stats = session.get_stats()
        info = Mp4DecryptSession.probe(output)
        is_plaintext = not info.tracks[0].is_encrypted
        return task_id, len(output), stats.execution_duration_ms, is_plaintext


def main():
    print("=" * 67)
    print("  MP4DECRYPT-API - MULTITHREADED CONCURRENT SESSIONS (PYTHON)      ")
    print("=" * 67)

    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    kid_hex = "9eb4050de44b4802932e27d75083e266"
    key_hex = "166634c675823c235a4a9446fad52e4d"

    print("Fetching test media chunks...")
    init_bytes = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()
    seg_bytes = urllib.request.urlopen(f"{base_url}/15/0001.m4s").read()
    test_buffer = init_bytes + seg_bytes

    NUM_WORKERS = 8
    print(f"\nLaunching {NUM_WORKERS} parallel decryption sessions in ThreadPoolExecutor...")

    start_time = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=NUM_WORKERS) as executor:
        futures = [
            executor.submit(decrypt_worker, i, test_buffer, kid_hex, key_hex)
            for i in range(NUM_WORKERS)
        ]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]

    total_time_ms = (time.perf_counter() - start_time) * 1000.0

    print("\nResults:")
    all_ok = True
    for task_id, out_size, duration_ms, is_plaintext in sorted(results, key=lambda r: r[0]):
        print(f"  Task #{task_id}: {out_size:,} bytes decrypted in {duration_ms:.2f} ms (plaintext: {is_plaintext})")
        if not is_plaintext:
            all_ok = False

    print(f"\nTotal wall-clock duration for {NUM_WORKERS} concurrent tasks: {total_time_ms:.2f} ms")
    if all_ok:
        print("SUCCESS: In-process multithreaded thread-safety fully verified!")
    else:
        raise RuntimeError("One or more parallel sessions failed.")


if __name__ == "__main__":
    main()
