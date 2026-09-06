#!/usr/bin/env python3
"""
mp4decrypt-api - Custom Streaming / Callback I/O Demo (Python)
==============================================================

Demonstrates Paradigm 3: Decrypting directly between Python BinaryIO streams
(such as io.BytesIO, network sockets, or custom file wrappers) via C callbacks.

Run with:
    python examples/python/demo_streaming.py
"""

import io
import os
import sys
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession, Mp4LogLevel


class TracingStream(io.BytesIO):
    """A stream wrapper that tracks read/write activity for demonstration."""

    def __init__(self, initial_bytes=b"", name="Stream"):
        super().__init__(initial_bytes)
        self.name = name
        self.total_reads = 0
        self.total_writes = 0

    def read(self, size=-1):
        data = super().read(size)
        self.total_reads += len(data)
        return data

    def write(self, b):
        n = super().write(b)
        self.total_writes += n
        return n


def main():
    print("=" * 67)
    print("   MP4DECRYPT-API - CUSTOM STREAMING CALLBACK I/O (PYTHON)         ")
    print("=" * 67)

    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    kid_hex = "9eb4050de44b4802932e27d75083e266"
    key_hex = "166634c675823c235a4a9446fad52e4d"

    print("1. Fetching media chunks...")
    init_bytes = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()
    seg_bytes = urllib.request.urlopen(f"{base_url}/15/0001.m4s").read()
    input_data = init_bytes + seg_bytes

    input_stream = TracingStream(input_data, name="InputPipe")
    output_stream = TracingStream(name="OutputPipe")

    print("\n2. Decrypting through custom Python stream interfaces...")
    with Mp4DecryptSession() as session:
        session.add_key(kid_hex, key_hex)
        session.set_log_level(Mp4LogLevel.DEBUG)
        session.on_log(lambda lvl, msg: print(f"   [Stream Log] {msg}"))

        session.decrypt_custom(input_stream, output_stream)
        stats = session.get_stats()

    decrypted_bytes = output_stream.getvalue()

    print(f"\n3. Stream Statistics:")
    print(f"   Input stream bytes read:   {input_stream.total_reads:,}")
    print(f"   Output stream bytes written: {output_stream.total_writes:,}")
    print(f"   Decrypted stream size:      {len(decrypted_bytes):,} bytes")
    print(f"   Execution duration:        {stats.execution_duration_ms:.2f} ms")
    print(f"   Throughput:                {stats.throughput_mb_per_sec:.2f} MB/s")

    # Verification
    info = Mp4DecryptSession.probe(decrypted_bytes)
    print(f"\n4. Verification:")
    print(f"   Track #1 encrypted: {info.tracks[0].is_encrypted}")

    if not info.tracks[0].is_encrypted:
        print("\nSUCCESS: Custom callback streaming decrypted media successfully!")
    else:
        raise RuntimeError("Decryption verification failed.")


if __name__ == "__main__":
    main()
