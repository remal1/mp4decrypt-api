/**
 * mp4decrypt-api - Internal Header
 * =================================
 *
 * Defines Mp4DecryptSession, local atom factories for thread-safety,
 * listener bridges, and internal utility functions.
 */

#ifndef MP4DECRYPT_INTERNAL_H
#define MP4DECRYPT_INTERNAL_H

#include "mp4decrypt_api.h"
#include "mp4decrypt_callback_stream.h"

#include "Ap4.h"
#include "Ap4Processor.h"
#include "Ap4Protection.h"
#include "Ap4CommonEncryption.h"
#include "Ap4Marlin.h"
#include "Ap4OmaDcf.h"
#include "Ap4FileByteStream.h"
#include "Ap4TencAtom.h"
#include "Ap4PsshAtom.h"
#include "Ap4Utils.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#include <memory>

class Mp4DecryptProgressListener : public AP4_Processor::ProgressListener {
public:
    explicit Mp4DecryptProgressListener(Mp4DecryptSession* session)
        : session_(session) {}

    AP4_Result OnProgress(unsigned int step, unsigned int total) override;

private:
    Mp4DecryptSession* session_;
};

struct Mp4DecryptSession {
public:
    Mp4DecryptSession();
    ~Mp4DecryptSession() = default;

    // Disallow copy/move
    Mp4DecryptSession(const Mp4DecryptSession&) = delete;
    Mp4DecryptSession& operator=(const Mp4DecryptSession&) = delete;

    void SetError(const std::string& message);
    const char* GetLastError();

    void Log(int level, const char* fmt, ...);
    void SetLogLevel(int level);
    void SetLogCallback(Mp4DecryptLogCallback cb, void* user_data);

    void SetProgressCallback(Mp4DecryptProgressCallback cb, void* user_data);
    void InvokeProgress(uint32_t step, uint32_t total);

    int AddKey(const char* id_spec, const char* key_hex);
    int ClearKeys();

    void Cancel();
    bool IsCancelled() const;
    void ResetExecutionState();

    int GetStats(Mp4DecryptStats* out_stats);
    void RecordStats(uint64_t bytes_read, uint64_t bytes_written, double duration_ms);

    // Core processing using an input, output, and optional fragments stream
    int ProcessByteStreams(AP4_ByteStream& input,
                           AP4_ByteStream& output,
                           AP4_ByteStream* fragments_info);

    // Probing helper
    static int ProbeStream(AP4_ByteStream& stream, Mp4MediaInfo* out_info, Mp4DecryptSession* session);

private:
    friend class Mp4DecryptProgressListener;

    mutable std::recursive_mutex mutex_;
    std::string last_error_;

    std::unique_ptr<AP4_ProtectionKeyMap> key_map_;
    // Each session owns its own isolated AP4_DefaultAtomFactory to guarantee in-process thread safety
    AP4_DefaultAtomFactory atom_factory_;

    std::atomic<bool> cancelled_{false};

    Mp4DecryptProgressCallback progress_cb_{nullptr};
    void* progress_user_data_{nullptr};

    Mp4DecryptLogCallback log_cb_{nullptr};
    void* log_user_data_{nullptr};
    int log_level_{MP4DECRYPT_LOG_INFO};

    Mp4DecryptStats stats_{0, 0, 0.0, 0.0};
};

#endif // MP4DECRYPT_INTERNAL_H
