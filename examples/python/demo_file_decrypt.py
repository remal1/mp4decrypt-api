#!/usr/bin/env python3
"""
mp4decrypt-api - File-Based Decryption Demo (Python)
===================================================

Demonstrates Paradigm 1: Decrypting media files directly on the filesystem
with progress reporting, throughput metrics, and plaintext validation.

Run with:
    python examples/python/demo_file_decrypt.py
"""

import os
import sys
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession, Mp4LogLevel


def main():
    print("=" * 67)
    print("       MP4DECRYPT-API - FILE DECRYPTION DEMO (PYTHON)              ")
    print("=" * 67)

    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    kid_hex = "9eb4050de44b4802932e27d75083e266"
    key_hex = "166634c675823c235a4a9446fad52e4d"

    temp_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "build", "test_tmp"))
    os.makedirs(temp_dir, exist_ok=True)

    in_file = os.path.join(temp_dir, "python_demo_encrypted.mp4")
    out_file = os.path.join(temp_dir, "python_demo_decrypted.mp4")

    print("\n1. Fetching test vector from remote CDN...")
    init_data = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()
    seg_data = urllib.request.urlopen(f"{base_url}/15/0001.m4s").read()

    with open(in_file, "wb") as f:
        f.write(init_data)
        f.write(seg_data)

    file_size_kb = os.path.getsize(in_file) / 1024.0
    print(f"   Created input file: {in_file} ({file_size_kb:.1f} KB)")

    # Probe encrypted file
    info_before = Mp4DecryptSession.probe(in_file)
    print(f"\n2. Probing media before decryption:")
    print(f"   Tracks: {info_before.track_count}")
    print(f"   Duration: {info_before.duration_seconds:.2f}s, Fragmented: {info_before.is_fragmented}")
    print(f"   Track #1 encrypted: {info_before.tracks[0].is_encrypted} (scheme: {info_before.tracks[0].scheme_type})")

    # Decrypt with session
    print("\n3. Decrypting file with progress callback and metrics...")
    with Mp4DecryptSession() as session:
        session.add_key(kid_hex, key_hex)
        session.set_log_level(Mp4LogLevel.INFO)

        session.on_log(lambda lvl, msg: print(f"   [Native Log] {msg}"))
        session.on_progress(lambda step, total: print(f"   [Progress] {step}/{total} ({step/total*100:.1f}%)"))

        session.decrypt_file(in_file, out_file)
        stats = session.get_stats()

    print(f"\n4. Decryption Performance:")
    print(f"   Bytes processed: {stats.total_bytes_read:,} read, {stats.total_bytes_written:,} written")
    print(f"   Execution time:  {stats.execution_duration_ms:.2f} ms")
    print(f"   Throughput:      {stats.throughput_mb_per_sec:.2f} MB/s")

    # Verify plaintext
    info_after = Mp4DecryptSession.probe(out_file)
    print(f"\n5. Verification after decryption:")
    print(f"   Track #1 encrypted: {info_after.tracks[0].is_encrypted}")
    print(f"   Codec: {info_after.tracks[0].codec}")

    if not info_after.tracks[0].is_encrypted:
        print("\nSUCCESS: Media decrypted and verified plaintext successfully!")
    else:
        raise RuntimeError("Decryption verification failed.")

    # Cleanup
    if os.path.exists(in_file):
        os.remove(in_file)
    if os.path.exists(out_file):
        os.remove(out_file)


if __name__ == "__main__":
    main()
