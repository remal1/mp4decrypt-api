# mp4decrypt-api

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-informational)](#building-from-source)
[![Bento4 Core](https://img.shields.io/badge/Bento4-v1.6.0.0-green)](https://github.com/axiomatic-systems/Bento4)
[![Bun FFI](https://img.shields.io/badge/Bun-bun%3Affi-black?logo=bun)](sdk/bun)
[![Python SDK](https://img.shields.io/badge/Python-ctypes%20(zero--dep)-yellow?logo=python)](sdk/python)

A high-performance, **100% in-process thread-safe** C ABI dynamic library and native SDKs (Bun & Python) for Axiomatic Systems' **Bento4 MP4 Decryptor** (`mp4decrypt`).

Designed following the battle-tested architectural patterns of [`shaka-decryptor`](https://github.com/remal1/shaka-decryptor) and [`mkvtoolnix-api`](https://github.com/remal1/mkvtoolnix-api), `mp4decrypt-api` eliminates the subprocess overhead of spawning CLI executables while enabling direct RAM buffer manipulation, custom streaming callbacks, progress notifications, and dynamic log routing.

---

## Key Features

- **True In-Process Concurrency**: Unlike the CLI or isolated wrapper libraries, `mp4decrypt-api` isolates Bento4's `AP4_AtomFactory` per session and utilizes recursive mutex guards, allowing dozens of concurrent worker threads to decrypt media simultaneously in the same process without state corruption.
- **3 Universal I/O Paradigms**:
  1. **File-Based I/O**: Direct file-to-file processing with progress callbacks and throughput metrics.
  2. **In-Memory RAM Buffer I/O**: Zero-disk decryption directly between memory buffers for ultra-low latency.
  3. **Custom Streaming Callback I/O**: Pluggable user-defined C callbacks (`read`, `write`, `seek`, `tell`, `size`) enabling streaming directly between pipes, network sockets, or virtual streams.
- **Pre-Decryption Media Probing**: Extract full container metadata (codecs, tracks, dimensions, sample rates, duration, fragmentation status, encryption schemes `cenc`/`cbcs`/`piff`, and default KIDs) without decrypting.
- **Mid-Flight Cancellation**: Asynchronously interrupt long-running decryptions safely with clean memory unwinding.
- **Native Log Routing & Filtering**: Capture internal Bento4 and library logs with configurable log levels (`DEBUG`, `INFO`, `WARN`, `ERROR`, `NONE`).
- **Zero Third-Party Dependencies in Python**: Implemented entirely with Python's standard library `ctypes`.
- **High-Performance Bun FFI**: Direct native bindings using `bun:ffi` with typed memory buffers.

---

## Architectural Comparison

| Capability | Official Bento4 CLI (`mp4decrypt`) | `mp4decrypt-api` (This Project) |
| :--- | :---: | :---: |
| **Execution Model** | Subprocess CLI (`std::process`) | **Native In-Process C ABI Dynamic Library** |
| **File I/O** | Yes | **Yes** |
| **In-Memory Buffer I/O** | No (requires temp files) | **Yes (zero disk access)** |
| **Custom Stream Callbacks** | No | **Yes (`read`/`write`/`seek`/`tell`/`size`)** |
| **In-Process Thread Safety** | N/A (separate processes) | **Full In-Process (Isolated Atom Factories)** |
| **Log Callbacks** | Stderr only | **Native C callback with filter levels** |
| **Progress Interception** | Terminal progress text | **Synchronous step/total callback** |
| **Asynchronous Cancellation** | Process SIGINT | **Thread-safe `cancel()` method** |
| **Container Inspection** | Separate `mp4dump`/`mp4info` CLI | **Built-in `probe()` API** |

---

## Quick Start

### 1. Bun SDK (`bun:ffi`)

```typescript
import { Mp4DecryptSession, Mp4LogLevel } from "./sdk/bun/mp4decrypt_sdk";

// Paradigm 1: In-Memory Buffer Decryption
const kid = "11111111111111111111111111111111";
const key = "22222222222222222222222222222222";
const encryptedBytes = new Uint8Array(...);

// One-shot
const plaintext = Mp4DecryptSession.decryptBuffer(encryptedBytes, kid, key);

// Or via Session:
const session = new Mp4DecryptSession();
session.addKey(kid, key);
session.onProgress((step, total) => console.log(`Progress: ${step}/${total}`));
session.onLog((lvl, msg) => console.log(`[Log] ${msg}`));
const result = session.decryptMemory(encryptedBytes);
session.destroy();
```

### 2. Python SDK (Zero Dependencies)

```python
from sdk.python.mp4decrypt_sdk import Mp4DecryptSession, Mp4LogLevel

kid = "11111111111111111111111111111111"
key = "22222222222222222222222222222222"

# One-shot RAM buffer decryption
decrypted_bytes = Mp4DecryptSession.decrypt_buffer(encrypted_bytes, kid, key)

# Context Manager Session
with Mp4DecryptSession() as session:
    session.add_key(kid, key)
    session.set_log_level(Mp4LogLevel.INFO)
    session.on_log(lambda lvl, msg: print(f"Log: {msg}"))
    session.on_progress(lambda step, total: print(f"Progress: {step}/{total}"))
    session.decrypt_file("input_encrypted.mp4", "output_decrypted.mp4")
    stats = session.get_stats()
    print(f"Decrypted in {stats.execution_duration_ms:.2f} ms at {stats.throughput_mb_per_sec:.2f} MB/s")
```

---

## Supported I/O Paradigms

### Paradigm 1: File-Based I/O
Ideal for processing complete video files on disk. Streams data in chunks without loading the entire movie into memory.

- **C ABI**: `Mp4Decrypt_DecryptFile(ctx, in_path, out_path, fragments_info_path)`
- **Bun**: `session.decryptFile(inPath, outPath, fragmentsInfoPath)`
- **Python**: `session.decrypt_file(in_path, out_path, fragments_info_path)`

### Paradigm 2: In-Memory RAM Buffer I/O
Ideal for microservices, HLS/DASH segment proxies, and serverless lambdas. Eliminates filesystem bottlenecks.

- **C ABI**: `Mp4Decrypt_DecryptMemory(ctx, in_data, in_size, frag_data, frag_size, out_data, out_size)`
- **Bun**: `session.decryptMemory(inputBytes)` / `Mp4DecryptSession.decryptBuffer(...)`
- **Python**: `session.decrypt_memory(input_bytes)` / `Mp4DecryptSession.decrypt_buffer(...)`

### Paradigm 3: Custom Streaming / Callback I/O
Ideal for non-file sources, cloud object streams (S3/GCS), encryption on the fly, or memory-constrained virtual streams.

- **C ABI**: `Mp4Decrypt_DecryptCustom(ctx, &in_stream, &out_stream, &frag_stream)`
- **Bun**: `session.decryptCustom({ input: { read, seek, tell, size }, output: { write, seek, tell, size } })`
- **Python**: `session.decrypt_custom(input_stream, output_stream)` (accepts any `io.BytesIO` or `BinaryIO`)

---

## Media Probing API

Inspect container structure before deciding which decryption keys to acquire:

```python
info = Mp4DecryptSession.probe(media_data_or_path)

print(f"Container Brands: {info.container_brands}")
print(f"Duration:         {info.duration_seconds:.2f} s")
print(f"Fragmented MP4:   {info.is_fragmented}")

for track in info.tracks:
    print(f"Track #{track.track_id}: {track.stream_type} ({track.codec})")
    if track.is_encrypted:
        print(f"  Scheme:      {track.scheme_type}")
        print(f"  Default KID: {track.default_kid_hex}")
```

---

## Building from Source

### Prerequisites
- **CMake** >= 3.20
- **C++17 Compiler**: MSVC (VS 2019+), GCC 9+, or Clang 10+
- **Git**

### Clone & Compile

```bash
# Clone with Bento4 submodule
git clone --recurse-submodules https://github.com/remal1/mp4decrypt-api.git
cd mp4decrypt-api

# Configure and compile Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The compiled binary will be located in:
- Windows: `build/Release/mp4decrypt_api.dll`
- Linux: `build/libmp4decrypt_api.so`
- macOS: `build/libmp4decrypt_api.dylib`

---

## Running Examples & Tests

### Bun Examples

```bash
# File decryption with throughput stats
bun run demo:bun:file

# High-speed RAM buffer decryption
bun run demo:bun:buffer

# Custom virtual streaming callbacks
bun run demo:bun:streaming

# Media container probing
bun run demo:bun:probe

# Log callback interception & level filtering
bun run demo:bun:log

# Mid-flight session cancellation
bun run demo:bun:cancel

# Concurrent parallel sessions (thread-safety verification)
bun run demo:bun:parallel
```

### Python Examples

```bash
# File decryption
python examples/python/demo_file_decrypt.py

# In-memory RAM buffer decryption
python examples/python/demo_buffer_decrypt.py

# Custom streaming I/O with io.BytesIO
python examples/python/demo_streaming.py

# Media container probing
python examples/python/demo_probe.py

# Native log callbacks
python examples/python/demo_log_callback.py

# Mid-flight cancellation
python examples/python/demo_cancellation.py

# Multithreaded concurrent sessions
python examples/python/demo_async.py
```

---

## C ABI Public Header

The primary C interface is defined in [`include/mp4decrypt_api.h`](include/mp4decrypt_api.h).

```c
// Core lifecycle
MP4DECRYPT_API_EXPORT Mp4DecryptSession* Mp4Decrypt_Create(void);
MP4DECRYPT_API_EXPORT void Mp4Decrypt_Destroy(Mp4DecryptSession* ctx);
MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetLastError(Mp4DecryptSession* ctx);

// Key configuration
MP4DECRYPT_API_EXPORT int Mp4Decrypt_AddKey(Mp4DecryptSession* ctx, const char* id_spec, const char* key_hex);
MP4DECRYPT_API_EXPORT int Mp4Decrypt_ClearKeys(Mp4DecryptSession* ctx);

// Callbacks & control
MP4DECRYPT_API_EXPORT void Mp4Decrypt_SetProgressCallback(Mp4DecryptSession* ctx, Mp4DecryptProgressCallback cb, void* user_data);
MP4DECRYPT_API_EXPORT void Mp4Decrypt_SetLogCallback(Mp4DecryptSession* ctx, Mp4DecryptLogCallback cb, void* user_data);
MP4DECRYPT_API_EXPORT void Mp4Decrypt_SetLogLevel(Mp4DecryptSession* ctx, int level);
MP4DECRYPT_API_EXPORT void Mp4Decrypt_Cancel(Mp4DecryptSession* ctx);

// The 3 I/O modes
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptFile(Mp4DecryptSession* ctx, const char* in_path, const char* out_path, const char* frag_path);
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptMemory(Mp4DecryptSession* ctx, const uint8_t* in_data, uint64_t in_size, const uint8_t* frag_data, uint64_t frag_size, uint8_t** out_data, uint64_t* out_size);
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptCustom(Mp4DecryptSession* ctx, const Mp4CustomStream* in_stream, const Mp4CustomStream* out_stream, const Mp4CustomStream* frag_stream);

// Container probing & versioning
MP4DECRYPT_API_EXPORT int Mp4Decrypt_ProbeMemory(const uint8_t* in_data, uint64_t in_size, Mp4MediaInfo* out_info);
MP4DECRYPT_API_EXPORT int Mp4Decrypt_ProbeFile(const char* file_path, Mp4MediaInfo* out_info);
MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetVersionJson(void);
```

---

## License

This project is licensed under the MIT License. Bento4 is included under its original GPL / Commercial dual license (see `third_party/bento4/LICENSE.txt`).
