#!/usr/bin/env python3
"""
mp4decrypt-api - Asynchronous Session Cancellation Demo (Python)
================================================================

Demonstrates interrupting a decryption operation mid-flight using
session.cancel() from a stream callback or external thread.

Run with:
    python examples/python/demo_cancellation.py
"""

import io
import os
import sys
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession, Mp4LogLevel


class CancellingInputStream(io.BytesIO):
    def __init__(self, data: bytes, session: Mp4DecryptSession):
        super().__init__(data)
        self.session = session
        self.cancelled = False

    def read(self, size=-1):
        # Once some bytes have been read, trigger cancellation
        pos = self.tell()
        if pos >= 16 and not self.cancelled:
            print(f"  [Custom Stream] Read {pos} bytes -> triggering session.cancel()!")
            self.cancelled = True
            self.session.cancel()
        return super().read(size)


def main():
    print("=" * 67)
    print("    MP4DECRYPT-API - ASYNCHRONOUS CANCELLATION (PYTHON)            ")
    print("=" * 67)

    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    kid_hex = "9eb4050de44b4802932e27d75083e266"
    key_hex = "166634c675823c235a4a9446fad52e4d"

    print("Fetching encrypted media chunks...")
    init_bytes = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()
    seg_bytes = urllib.request.urlopen(f"{base_url}/15/0001.m4s").read()
    test_buffer = init_bytes + seg_bytes

    print("\nStarting streaming decryption with planned mid-flight cancellation...")
    with Mp4DecryptSession() as session:
        session.add_key(kid_hex, key_hex)
        session.set_log_level(Mp4LogLevel.DEBUG)
        session.on_log(lambda lvl, msg: print(f"  [Native Log] {msg}"))

        in_stream = CancellingInputStream(test_buffer, session)
        out_stream = io.BytesIO()

        try:
            session.decrypt_custom(in_stream, out_stream)
            print("ERROR: Decryption was expected to fail with cancellation error!")
        except RuntimeError as e:
            print(f"\nExpected cancellation caught: {e}")
            if "cancelled" in str(e).lower() or in_stream.cancelled:
                print("\nSUCCESS: Session cancellation operated as intended!")


if __name__ == "__main__":
    main()
