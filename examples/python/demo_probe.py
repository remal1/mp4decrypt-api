#!/usr/bin/env python3
"""
mp4decrypt-api - Media Probing & Metadata Extraction Demo (Python)
==================================================================

Demonstrates inspecting MP4 / fMP4 containers to extract track details,
codecs, dimensions, sample rates, encryption schemes, and default KIDs
without decrypting the file.

Run with:
    python examples/python/demo_probe.py
"""

import os
import sys
import urllib.request

# Ensure sdk/python is importable
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession


def main():
    print("=" * 67)
    print("      MP4DECRYPT-API - MEDIA CONTAINER PROBING (PYTHON)            ")
    print("=" * 67)

    # 1. Query dynamic library version
    ver = Mp4DecryptSession.get_version()
    print(f"\nEngine Version:    {ver.version_string}")
    print(f"ABI Revision:      {ver.abi_revision}")
    print(f"Bento4 Core:       {ver.bento4_version}")
    print(f"Build Date:        {ver.build_date}")

    # 2. Fetch sample media
    base_url = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey"
    print("\nDownloading sample init fragment...")
    init_bytes = urllib.request.urlopen(f"{base_url}/15/init.mp4").read()

    # 3. Probe in-memory buffer
    print(f"\nProbing {len(init_bytes):,} bytes from memory buffer:")
    info = Mp4DecryptSession.probe(init_bytes)

    print(f"  Container Brands:   {info.container_brands}")
    print(f"  Container Duration: {info.duration_seconds:.2f} s")
    print(f"  Is Fragmented:      {info.is_fragmented}")
    print(f"  Total Tracks:       {info.track_count}")

    for idx, track in enumerate(info.tracks):
        print(f"\n  --- Track #{idx + 1} (ID: {track.track_id}) ---")
        print(f"    Stream Type:      {track.stream_type} (handler: '{track.handler_type}')")
        print(f"    Codec:            {track.codec}")
        print(f"    Duration:         {track.duration_seconds:.2f} s")
        print(f"    Encrypted:        {track.is_encrypted}")
        if track.is_encrypted:
            print(f"    Scheme Type:      {track.scheme_type}")
            print(f"    Default KID:      {track.default_kid_hex}")
        if track.stream_type == "Audio":
            print(f"    Audio Channels:   {track.audio_channels}")
            print(f"    Sample Rate:      {track.sample_rate} Hz")
        elif track.stream_type == "Video":
            print(f"    Resolution:       {track.width}x{track.height}")
            print(f"    Framerate:        {track.frame_rate:.2f} fps")

    print("\nSUCCESS: Media probed and detailed metadata retrieved!")


if __name__ == "__main__":
    main()
