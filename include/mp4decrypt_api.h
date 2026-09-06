/**
 * mp4decrypt-api - C ABI FFI Header
 * =================================
 *
 * Thread-safe C ABI dynamic library interface for Bento4 mp4decrypt.
 * Compatible with Bun (bun:ffi), Python (ctypes/cffi), Rust, C#, Go, and C/C++.
 */

#ifndef MP4DECRYPT_API_H
#define MP4DECRYPT_API_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(MP4DECRYPT_BUILD_SHARED)
    #define MP4DECRYPT_API_EXPORT __declspec(dllexport)
  #else
    #define MP4DECRYPT_API_EXPORT __declspec(dllimport)
  #endif
#else
  #if defined(__GNUC__) && __GNUC__ >= 4
    #define MP4DECRYPT_API_EXPORT __attribute__((visibility("default")))
  #else
    #define MP4DECRYPT_API_EXPORT
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MP4DECRYPT_API_VERSION_MAJOR 1
#define MP4DECRYPT_API_VERSION_MINOR 0
#define MP4DECRYPT_API_VERSION_PATCH 0
#define MP4DECRYPT_API_ABI_REVISION  1

// Opaque context handle
typedef struct Mp4DecryptSession Mp4DecryptSession;

// Log levels
typedef enum Mp4LogLevel {
  MP4DECRYPT_LOG_DEBUG = 0,
  MP4DECRYPT_LOG_INFO  = 1,
  MP4DECRYPT_LOG_WARN  = 2,
  MP4DECRYPT_LOG_ERROR = 3,
  MP4DECRYPT_LOG_QUIET = 4
} Mp4LogLevel;

// Progress callback: step, total (percentage: (double)step / total * 100.0)
typedef void (*Mp4DecryptProgressCallback)(uint32_t step, uint32_t total, void* user_data);

// Log callback: level, message, user_data
typedef void (*Mp4DecryptLogCallback)(int level, const char* message, void* user_data);

// --- Custom Streaming / Callback I/O Function Pointers ---
// Return number of bytes read/written, or negative value on error.
typedef int64_t (*Mp4DecryptReadFunc)(void* user_data, uint8_t* buffer, uint64_t bytes_to_read);
typedef int64_t (*Mp4DecryptWriteFunc)(void* user_data, const uint8_t* buffer, uint64_t bytes_to_write);
typedef int64_t (*Mp4DecryptSeekFunc)(void* user_data, uint64_t offset);
typedef int64_t (*Mp4DecryptTellFunc)(void* user_data);
typedef uint64_t (*Mp4DecryptSizeFunc)(void* user_data);

typedef struct Mp4CustomStream {
  void* user_data;
  Mp4DecryptReadFunc  read_cb;   // Read callback (NULL if output-only)
  Mp4DecryptWriteFunc write_cb;  // Write callback (NULL if input-only)
  Mp4DecryptSeekFunc  seek_cb;   // Optional seek callback (NULL if non-seekable)
  Mp4DecryptTellFunc  tell_cb;   // Optional tell callback (NULL if position unknown)
  Mp4DecryptSizeFunc  size_cb;   // Optional total size callback (NULL if unknown)
} Mp4CustomStream;

// Version information
typedef struct Mp4DecryptVersionInfo {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t abi_revision;
  const char* bento4_version;
  const char* build_date;
} Mp4DecryptVersionInfo;

// Performance and throughput statistics
typedef struct Mp4DecryptStats {
  uint64_t total_bytes_read;
  uint64_t total_bytes_written;
  double execution_duration_ms;
  double throughput_mb_per_sec;
} Mp4DecryptStats;

// Track metadata retrieved during media inspection
typedef struct Mp4TrackMetadata {
  uint32_t track_id;
  int stream_type;              // 1=Audio, 2=Video, 3=Subtitles, 0=Unknown
  char handler_type[8];         // "vide", "soun", "subt"
  char codec[32];               // e.g. "avc1.640028", "hvc1.1.6.L93.90", "mp4a.40.2"
  int is_encrypted;             // 1 if protected, 0 if plaintext
  char scheme_type[8];          // "cenc", "cbcs", "cens", "cbc1", "odcf", etc.
  char default_kid_hex[33];     // 32-char hex string + null terminator (if encrypted)
  uint32_t width;               // Video width (0 for audio)
  uint32_t height;              // Video height (0 for audio)
  double frame_rate;            // Video framerate (0 for audio)
  uint32_t audio_channels;      // Audio channel count (0 for video)
  uint32_t sample_rate;         // Audio sample rate (0 for video)
  double duration_seconds;      // Track duration in seconds
} Mp4TrackMetadata;

// Aggregated media container information
typedef struct Mp4MediaInfo {
  uint32_t track_count;
  Mp4TrackMetadata tracks[32];
  char container_brands[64];
  double duration_seconds;
  int is_fragmented;            // 1 if fragmented MP4 (fMP4), 0 if standard
} Mp4MediaInfo;

// ---------------------------------------------------------------------------
// Lifecycle & Configuration
// ---------------------------------------------------------------------------

// Creates a new decryption session. Each session operates with its own isolated
// atom factory, ensuring 100% thread safety across concurrent sessions.
MP4DECRYPT_API_EXPORT Mp4DecryptSession* Mp4Decrypt_Create(void);

// Destroys a decryption session and frees all allocated session resources.
MP4DECRYPT_API_EXPORT void Mp4Decrypt_Destroy(Mp4DecryptSession* ctx);

// Retrieves the last error message recorded on this session. Thread-safe.
MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetLastError(Mp4DecryptSession* ctx);

// Adds a decryption key to the session.
// id_spec: Either a decimal track ID ("1", "2") or a 128-bit KID hex string (32 hex characters).
// key_hex: 128-bit key as a 32-character hex string.
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_AddKey(Mp4DecryptSession* ctx, const char* id_spec, const char* key_hex);

// Clears all previously registered keys from the session.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_ClearKeys(Mp4DecryptSession* ctx);

// Registers a progress reporting callback on the session.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_SetProgressCallback(Mp4DecryptSession* ctx, Mp4DecryptProgressCallback cb, void* user_data);

// Registers a log interception callback on the session.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_SetLogCallback(Mp4DecryptSession* ctx, Mp4DecryptLogCallback cb, void* user_data);

// Sets the log verbosity filter for the session.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_SetLogLevel(Mp4DecryptSession* ctx, int level);

// ---------------------------------------------------------------------------
// Execution Paradigms (Thread-Safe)
// ---------------------------------------------------------------------------

// Paradigm 1: File-based I/O
// Decrypts input_path into output_path.
// fragments_info_path: Path to separate fragments info file (optional, pass NULL if not used).
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptFile(
    Mp4DecryptSession* ctx,
    const char* input_path,
    const char* output_path,
    const char* fragments_info_path);

// Paradigm 2: Full In-Memory Buffer I/O
// Decrypts an input memory buffer and allocates output_data in RAM.
// The allocated output buffer MUST be freed by caller using Mp4Decrypt_FreeBuffer().
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptMemory(
    Mp4DecryptSession* ctx,
    const uint8_t* input_data,
    uint64_t input_size,
    const uint8_t* fragments_info_data,
    uint64_t fragments_info_size,
    uint8_t** out_data,
    uint64_t* out_size);

// One-Shot In-Memory Buffer Decryption (Zero-setup convenience API)
// Automatically allocates an internal session, configures the key, decrypts, and cleans up.
// The allocated out_data buffer MUST be freed by caller using Mp4Decrypt_FreeBuffer().
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptBuffer(
    const uint8_t* in_data,
    uint64_t in_size,
    const char* kid_or_track_hex,
    const char* key_hex,
    uint8_t** out_data,
    uint64_t* out_size);

// Frees memory buffer allocated by Mp4Decrypt_DecryptMemory or Mp4Decrypt_DecryptBuffer.
MP4DECRYPT_API_EXPORT void Mp4Decrypt_FreeBuffer(uint8_t* buffer);

// Paradigm 3: Custom Streaming / Callback I/O
// Decrypts using user-provided streaming callbacks.
// Allows piping live network chunks (HLS/DASH), sliding window jitter buffers, or RAM streams.
// input_stream: Pointer to input Mp4CustomStream (must have read_cb).
// output_stream: Pointer to output Mp4CustomStream (must have write_cb).
// fragments_info_stream: Optional pointer to fragments info Mp4CustomStream (or NULL).
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptCustom(
    Mp4DecryptSession* ctx,
    const Mp4CustomStream* input_stream,
    const Mp4CustomStream* output_stream,
    const Mp4CustomStream* fragments_info_stream);

// ---------------------------------------------------------------------------
// Execution Control & Statistics
// ---------------------------------------------------------------------------

// Thread-safe cancellation: Can be called from ANY thread or timer.
// Signals the active decryption to abort immediately and return with an error code.
// Returns 0 on success.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_Cancel(Mp4DecryptSession* ctx);

// Retrieves performance metrics and throughput from the last decryption run.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_GetStats(Mp4DecryptSession* ctx, Mp4DecryptStats* out_stats);

// ---------------------------------------------------------------------------
// Media Probing (No Decryption Keys Required)
// ---------------------------------------------------------------------------

// Probes an MP4 media file to inspect its tracks, codecs, protection schemes, and KIDs.
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_ProbeFile(const char* file_path, Mp4MediaInfo* out_info);

// Probes an in-memory MP4 buffer.
// Returns 0 on success, non-zero on error.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_ProbeMemory(const uint8_t* data, uint64_t size, Mp4MediaInfo* out_info);

// ---------------------------------------------------------------------------
// Versioning & Information
// ---------------------------------------------------------------------------

// Returns the ABI major version.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_GetApiVersion(void);

// Fills out_info with semantic version details and build info.
MP4DECRYPT_API_EXPORT int Mp4Decrypt_GetVersion(Mp4DecryptVersionInfo* out_info);

// Returns a human-readable formatted version string.
MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetVersionString(void);

// Returns a structured JSON string with API, Bento4 version, platform, compiler, and build date.
MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetVersionJson(void);

#ifdef __cplusplus
}
#endif

#endif // MP4DECRYPT_API_H
