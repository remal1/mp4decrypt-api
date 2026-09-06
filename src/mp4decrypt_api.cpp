/**
 * mp4decrypt-api - C ABI Implementation
 * =====================================
 *
 * Implements the C ABI exported functions and Bento4 engine integration.
 */

#include "mp4decrypt_api.h"
#include "mp4decrypt_internal.h"
#include "mp4decrypt_callback_stream.h"
#include "Ap4Version.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Format fourCC to null-terminated ASCII
static void FormatFourCC(uint32_t code, char* out_str, size_t max_len) {
    if (max_len < 5) return;
    out_str[0] = static_cast<char>((code >> 24) & 0xFF);
    out_str[1] = static_cast<char>((code >> 16) & 0xFF);
    out_str[2] = static_cast<char>((code >> 8) & 0xFF);
    out_str[3] = static_cast<char>(code & 0xFF);
    out_str[4] = '\0';
    for (int i = 0; i < 4; ++i) {
        if (static_cast<unsigned char>(out_str[i]) < 32 || static_cast<unsigned char>(out_str[i]) > 126) {
            out_str[i] = '?';
        }
    }
}

// Format 16 bytes to 32-char hex string
static void FormatHex16(const uint8_t* data, char* out_hex) {
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        out_hex[i * 2]     = hex_digits[(data[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = hex_digits[data[i] & 0x0F];
    }
    out_hex[32] = '\0';
}

// ---------------------------------------------------------------------------
// Mp4DecryptProgressListener Implementation
// ---------------------------------------------------------------------------

AP4_Result Mp4DecryptProgressListener::OnProgress(unsigned int step, unsigned int total) {
    if (!session_) return AP4_SUCCESS;

    if (session_->IsCancelled()) {
        session_->Log(MP4DECRYPT_LOG_INFO, "Decryption interrupted by user request (cancel).");
        return AP4_FAILURE;
    }

    session_->InvokeProgress(step, total);
    return AP4_SUCCESS;
}

// ---------------------------------------------------------------------------
// Mp4DecryptSession Implementation
// ---------------------------------------------------------------------------

Mp4DecryptSession::Mp4DecryptSession()
    : key_map_(std::make_unique<AP4_ProtectionKeyMap>()),
      cancelled_(false),
      progress_cb_(nullptr),
      progress_user_data_(nullptr),
      log_cb_(nullptr),
      log_user_data_(nullptr),
      log_level_(MP4DECRYPT_LOG_INFO) {
    std::memset(&stats_, 0, sizeof(stats_));
}

void Mp4DecryptSession::SetError(const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    last_error_ = message;
    Log(MP4DECRYPT_LOG_ERROR, "%s", message.c_str());
}

const char* Mp4DecryptSession::GetLastError() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return last_error_.c_str();
}

void Mp4DecryptSession::Log(int level, const char* fmt, ...) {
    if (level < log_level_) return;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (log_cb_) {
        log_cb_(level, buffer, log_user_data_);
    }
}

void Mp4DecryptSession::SetLogLevel(int level) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    log_level_ = level;
}

void Mp4DecryptSession::SetLogCallback(Mp4DecryptLogCallback cb, void* user_data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    log_cb_ = cb;
    log_user_data_ = user_data;
}

void Mp4DecryptSession::SetProgressCallback(Mp4DecryptProgressCallback cb, void* user_data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    progress_cb_ = cb;
    progress_user_data_ = user_data;
}

void Mp4DecryptSession::InvokeProgress(uint32_t step, uint32_t total) {
    Mp4DecryptProgressCallback cb = nullptr;
    void* user_data = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        cb = progress_cb_;
        user_data = progress_user_data_;
    }
    if (cb) {
        cb(step, total, user_data);
    }
}

int Mp4DecryptSession::AddKey(const char* id_spec, const char* key_hex) {
    if (!id_spec || !key_hex) {
        SetError("Invalid arguments: id_spec and key_hex must not be null.");
        return -1;
    }

    if (std::strlen(key_hex) != 32) {
        SetError("Invalid key length: key_hex must be exactly 32 hex characters (128-bit).");
        return -1;
    }

    uint8_t key[16];
    if (AP4_ParseHex(key_hex, key, 16) != AP4_SUCCESS) {
        SetError("Failed to parse key_hex: invalid hexadecimal string.");
        return -1;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    size_t id_len = std::strlen(id_spec);
    if (id_len == 32) {
        uint8_t kid[16];
        if (AP4_ParseHex(id_spec, kid, 16) != AP4_SUCCESS) {
            last_error_ = "Failed to parse 128-bit KID hex string.";
            return -1;
        }
        key_map_->SetKeyForKid(kid, key, 16);
        Log(MP4DECRYPT_LOG_DEBUG, "Added decryption key for KID: %s", id_spec);
    } else {
        char* end_ptr = nullptr;
        unsigned long track_id = std::strtoul(id_spec, &end_ptr, 10);
        if (*end_ptr != '\0') {
            last_error_ = "Invalid track ID: expected decimal integer or 32-character hex KID.";
            return -1;
        }
        key_map_->SetKey(static_cast<AP4_UI32>(track_id), key, 16);
        Log(MP4DECRYPT_LOG_DEBUG, "Added decryption key for Track ID: %lu", track_id);
    }

    return 0;
}

int Mp4DecryptSession::ClearKeys() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    key_map_ = std::make_unique<AP4_ProtectionKeyMap>();
    Log(MP4DECRYPT_LOG_DEBUG, "Cleared all decryption keys.");
    return 0;
}

void Mp4DecryptSession::Cancel() {
    cancelled_.store(true, std::memory_order_release);
    Log(MP4DECRYPT_LOG_INFO, "Mp4DecryptSession cancellation requested.");
}

bool Mp4DecryptSession::IsCancelled() const {
    return cancelled_.load(std::memory_order_acquire);
}

void Mp4DecryptSession::ResetExecutionState() {
    cancelled_.store(false, std::memory_order_release);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    last_error_.clear();
    std::memset(&stats_, 0, sizeof(stats_));
}

int Mp4DecryptSession::GetStats(Mp4DecryptStats* out_stats) {
    if (!out_stats) return -1;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    *out_stats = stats_;
    return 0;
}

void Mp4DecryptSession::RecordStats(uint64_t bytes_read, uint64_t bytes_written, double duration_ms) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stats_.total_bytes_read = bytes_read;
    stats_.total_bytes_written = bytes_written;
    stats_.execution_duration_ms = duration_ms;
    if (duration_ms > 0.0) {
        stats_.throughput_mb_per_sec = (static_cast<double>(bytes_read) / (1024.0 * 1024.0)) / (duration_ms / 1000.0);
    } else {
        stats_.throughput_mb_per_sec = 0.0;
    }
}

// ---------------------------------------------------------------------------
// Core Stream Processing & Decrypting Processor Selection
// ---------------------------------------------------------------------------

static AP4_Processor* CreateDecryptingProcessor(AP4_ProtectionKeyMap& key_map,
                                                AP4_ByteStream& input,
                                                AP4_ByteStream* fragments_info,
                                                AP4_AtomFactory& atom_factory) {
    AP4_Processor* processor = nullptr;

    // Use a temporary AP4_File instance to parse the container headers
    AP4_ByteStream& probe_stream = fragments_info ? *fragments_info : input;
    probe_stream.Seek(0);
    AP4_File input_file(probe_stream, atom_factory, false);

    AP4_FtypAtom* ftyp = input_file.GetFileType();
    if (ftyp) {
        if (ftyp->GetMajorBrand() == AP4_OMA_DCF_BRAND_ODCF || ftyp->HasCompatibleBrand(AP4_OMA_DCF_BRAND_ODCF)) {
            processor = new AP4_OmaDcfDecryptingProcessor(&key_map);
        } else if (ftyp->GetMajorBrand() == AP4_MARLIN_BRAND_MGSV || ftyp->HasCompatibleBrand(AP4_MARLIN_BRAND_MGSV)) {
            processor = new AP4_MarlinIpmpDecryptingProcessor(&key_map);
        } else if (ftyp->GetMajorBrand() == AP4_PIFF_BRAND || ftyp->HasCompatibleBrand(AP4_PIFF_BRAND)) {
            processor = new AP4_CencDecryptingProcessor(&key_map);
        }
    }

    if (processor == nullptr) {
        AP4_Movie* movie = input_file.GetMovie();
        if (movie) {
            AP4_List<AP4_Track>& tracks = movie->GetTracks();
            for (unsigned int i = 0; i < tracks.ItemCount(); ++i) {
                AP4_Track* track = nullptr;
                tracks.Get(i, track);
                if (!track) continue;

                AP4_SampleDescription* sdesc = track->GetSampleDescription(0);
                if (sdesc && sdesc->GetType() == AP4_SampleDescription::TYPE_PROTECTED) {
                    auto* psdesc = AP4_DYNAMIC_CAST(AP4_ProtectedSampleDescription, sdesc);
                    if (psdesc) {
                        AP4_UI32 scheme = psdesc->GetSchemeType();
                        if (scheme == AP4_PROTECTION_SCHEME_TYPE_CENC ||
                            scheme == AP4_PROTECTION_SCHEME_TYPE_CBC1 ||
                            scheme == AP4_PROTECTION_SCHEME_TYPE_CENS ||
                            scheme == AP4_PROTECTION_SCHEME_TYPE_CBCS) {
                            processor = new AP4_CencDecryptingProcessor(&key_map);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (processor == nullptr) {
        processor = new AP4_StandardDecryptingProcessor(&key_map);
    }

    return processor;
}

class Ap4CancellableByteStream : public AP4_ByteStream {
public:
    Ap4CancellableByteStream(AP4_ByteStream& inner, Mp4DecryptSession* session)
        : inner_(inner), session_(session) {}

    AP4_Result ReadPartial(void* buffer, AP4_Size bytes_to_read, AP4_Size& bytes_read) override {
        if (session_ && session_->IsCancelled()) {
            bytes_read = 0;
            return AP4_ERROR_READ_FAILED;
        }
        return inner_.ReadPartial(buffer, bytes_to_read, bytes_read);
    }

    AP4_Result WritePartial(const void* buffer, AP4_Size bytes_to_write, AP4_Size& bytes_written) override {
        if (session_ && session_->IsCancelled()) {
            bytes_written = 0;
            return AP4_ERROR_WRITE_FAILED;
        }
        return inner_.WritePartial(buffer, bytes_to_write, bytes_written);
    }

    AP4_Result Seek(AP4_Position position) override { return inner_.Seek(position); }
    AP4_Result Tell(AP4_Position& position) override { return inner_.Tell(position); }
    AP4_Result GetSize(AP4_LargeSize& size) override { return inner_.GetSize(size); }
    AP4_Result Flush() override { return inner_.Flush(); }

    void AddReference() override {}
    void Release() override {}

private:
    AP4_ByteStream& inner_;
    Mp4DecryptSession* session_;
};

int Mp4DecryptSession::ProcessByteStreams(AP4_ByteStream& input,
                                         AP4_ByteStream& output,
                                         AP4_ByteStream* fragments_info) {
    ResetExecutionState();

    auto start_time = std::chrono::high_resolution_clock::now();
    Log(MP4DECRYPT_LOG_INFO, "Starting MP4 decryption pipeline...");

    // Create appropriate processor using this session's isolated atom factory
    AP4_Processor* processor = CreateDecryptingProcessor(*key_map_, input, fragments_info, atom_factory_);
    if (!processor) {
        SetError("Failed to instantiate Bento4 decrypting processor.");
        return -1;
    }

    // Rewind streams before main processing loop
    if (fragments_info) {
        fragments_info->Seek(0);
    } else {
        input.Seek(0);
    }

    Mp4DecryptProgressListener listener(this);
    AP4_Result result = AP4_SUCCESS;

    Ap4CancellableByteStream cancellable_in(input, this);
    Ap4CancellableByteStream cancellable_out(output, this);

    if (fragments_info) {
        Ap4CancellableByteStream cancellable_frag(*fragments_info, this);
        result = processor->Process(cancellable_in, cancellable_out, cancellable_frag, &listener, atom_factory_);
    } else {
        result = processor->Process(cancellable_in, cancellable_out, &listener, atom_factory_);
    }

    delete processor;

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    // Query stream sizes for metrics
    AP4_LargeSize input_size = 0;
    input.GetSize(input_size);
    AP4_LargeSize output_size = 0;
    output.GetSize(output_size);

    RecordStats(input_size, output_size, duration_ms);

    if (IsCancelled()) {
        SetError("Decryption operation cancelled.");
        return -2; // Cancelled
    }

    if (AP4_FAILED(result)) {
        std::ostringstream ss;
        ss << "Bento4 decryption failed: " << AP4_ResultText(result) << " (code " << result << ")";
        SetError(ss.str());
        return -1;
    }

    Log(MP4DECRYPT_LOG_INFO, "Decryption completed successfully in %.2f ms (%.2f MB/s).",
        duration_ms, stats_.throughput_mb_per_sec);
    return 0;
}

// ---------------------------------------------------------------------------
// Probing Implementation
// ---------------------------------------------------------------------------

int Mp4DecryptSession::ProbeStream(AP4_ByteStream& stream, Mp4MediaInfo* out_info, Mp4DecryptSession* session) {
    if (!out_info) return -1;
    std::memset(out_info, 0, sizeof(*out_info));

    AP4_DefaultAtomFactory local_factory;
    stream.Seek(0);
    AP4_File file(stream, local_factory, false);

    AP4_FtypAtom* ftyp = file.GetFileType();
    if (ftyp) {
        char brand[5];
        FormatFourCC(ftyp->GetMajorBrand(), brand, sizeof(brand));
        std::string brands = brand;
        for (unsigned int i = 0; i < ftyp->GetCompatibleBrands().ItemCount(); ++i) {
            FormatFourCC(ftyp->GetCompatibleBrands()[i], brand, sizeof(brand));
            brands += " ";
            brands += brand;
        }
        std::strncpy(out_info->container_brands, brands.c_str(), sizeof(out_info->container_brands) - 1);
    }

    AP4_Movie* movie = file.GetMovie();
    if (movie) {
        if (movie->GetTimeScale() > 0) {
            out_info->duration_seconds = static_cast<double>(movie->GetDuration()) / movie->GetTimeScale();
        }

        // Check for mvex atom indicating fragmented movie
        if (movie->GetMoovAtom() && movie->GetMoovAtom()->GetChild(AP4_ATOM_TYPE_MVEX) != nullptr) {
            out_info->is_fragmented = 1;
        }

        AP4_List<AP4_Track>& tracks = movie->GetTracks();
        uint32_t count = std::min(static_cast<uint32_t>(tracks.ItemCount()), 32u);
        out_info->track_count = count;

        for (uint32_t i = 0; i < count; ++i) {
            AP4_Track* track = nullptr;
            tracks.Get(i, track);
            if (!track) continue;

            Mp4TrackMetadata& meta = out_info->tracks[i];
            meta.track_id = track->GetId();

            if (track->GetMediaTimeScale() > 0) {
                meta.duration_seconds = static_cast<double>(track->GetDuration()) / track->GetMediaTimeScale();
            }

            FormatFourCC(track->GetHandlerType(), meta.handler_type, sizeof(meta.handler_type));

            switch (track->GetType()) {
                case AP4_Track::TYPE_AUDIO:
                    meta.stream_type = 1;
                    break;
                case AP4_Track::TYPE_VIDEO:
                    meta.stream_type = 2;
                    break;
                case AP4_Track::TYPE_SUBTITLES:
                case AP4_Track::TYPE_TEXT:
                    meta.stream_type = 3;
                    break;
                default:
                    meta.stream_type = 0;
                    break;
            }

            AP4_SampleDescription* sdesc = track->GetSampleDescription(0);
            if (sdesc) {
                FormatFourCC(sdesc->GetFormat(), meta.codec, sizeof(meta.codec));

                if (meta.stream_type == 2) {
                    meta.width = track->GetWidth() >> 16;
                    meta.height = track->GetHeight() >> 16;
                }

                auto* audio_desc = AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, sdesc);
                if (audio_desc) {
                    meta.audio_channels = audio_desc->GetChannelCount();
                    meta.sample_rate = audio_desc->GetSampleRate();
                }

                if (sdesc->GetType() == AP4_SampleDescription::TYPE_PROTECTED) {
                    meta.is_encrypted = 1;
                    auto* psdesc = AP4_DYNAMIC_CAST(AP4_ProtectedSampleDescription, sdesc);
                    if (psdesc) {
                        FormatFourCC(psdesc->GetSchemeType(), meta.scheme_type, sizeof(meta.scheme_type));
                        FormatFourCC(psdesc->GetOriginalFormat(), meta.codec, sizeof(meta.codec));

                        AP4_ProtectionSchemeInfo* scheme_info = psdesc->GetSchemeInfo();
                        if (scheme_info && scheme_info->GetSchiAtom()) {
                            AP4_TencAtom* tenc = AP4_DYNAMIC_CAST(AP4_TencAtom,
                                scheme_info->GetSchiAtom()->GetChild(AP4_ATOM_TYPE_TENC));
                            if (tenc && tenc->GetDefaultKid()) {
                                FormatHex16(tenc->GetDefaultKid(), meta.default_kid_hex);
                            }
                        }
                    }
                }
            }
        }
    } else {
        // May be a fragmented file with only moof atoms
        out_info->is_fragmented = 1;
    }

    if (session) {
        session->Log(MP4DECRYPT_LOG_DEBUG, "Probed media: %u tracks found, duration: %.2fs.",
                     out_info->track_count, out_info->duration_seconds);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Exported C ABI Functions
// ---------------------------------------------------------------------------

extern "C" {

MP4DECRYPT_API_EXPORT Mp4DecryptSession* Mp4Decrypt_Create(void) {
    try {
        return new Mp4DecryptSession();
    } catch (...) {
        return nullptr;
    }
}

MP4DECRYPT_API_EXPORT void Mp4Decrypt_Destroy(Mp4DecryptSession* ctx) {
    delete ctx;
}

MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetLastError(Mp4DecryptSession* ctx) {
    if (!ctx) return "Invalid session handle (null pointer).";
    return ctx->GetLastError();
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_AddKey(Mp4DecryptSession* ctx, const char* id_spec, const char* key_hex) {
    if (!ctx) return -1;
    return ctx->AddKey(id_spec, key_hex);
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_ClearKeys(Mp4DecryptSession* ctx) {
    if (!ctx) return -1;
    return ctx->ClearKeys();
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_SetProgressCallback(Mp4DecryptSession* ctx, Mp4DecryptProgressCallback cb, void* user_data) {
    if (!ctx) return -1;
    ctx->SetProgressCallback(cb, user_data);
    return 0;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_SetLogCallback(Mp4DecryptSession* ctx, Mp4DecryptLogCallback cb, void* user_data) {
    if (!ctx) return -1;
    ctx->SetLogCallback(cb, user_data);
    return 0;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_SetLogLevel(Mp4DecryptSession* ctx, int level) {
    if (!ctx) return -1;
    ctx->SetLogLevel(level);
    return 0;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptFile(
    Mp4DecryptSession* ctx,
    const char* input_path,
    const char* output_path,
    const char* fragments_info_path) {
    if (!ctx || !input_path || !output_path) {
        if (ctx) ctx->SetError("Invalid arguments: input_path and output_path cannot be null.");
        return -1;
    }

    AP4_ByteStream* input = nullptr;
    AP4_Result res = AP4_FileByteStream::Create(input_path, AP4_FileByteStream::STREAM_MODE_READ, input);
    if (AP4_FAILED(res)) {
        ctx->SetError(std::string("Cannot open input file: ") + input_path + " (" + AP4_ResultText(res) + ")");
        return -1;
    }

    AP4_ByteStream* output = nullptr;
    res = AP4_FileByteStream::Create(output_path, AP4_FileByteStream::STREAM_MODE_WRITE, output);
    if (AP4_FAILED(res)) {
        input->Release();
        ctx->SetError(std::string("Cannot open output file for writing: ") + output_path + " (" + AP4_ResultText(res) + ")");
        return -1;
    }

    AP4_ByteStream* fragments_info = nullptr;
    if (fragments_info_path && std::strlen(fragments_info_path) > 0) {
        res = AP4_FileByteStream::Create(fragments_info_path, AP4_FileByteStream::STREAM_MODE_READ, fragments_info);
        if (AP4_FAILED(res)) {
            input->Release();
            output->Release();
            ctx->SetError(std::string("Cannot open fragments info file: ") + fragments_info_path + " (" + AP4_ResultText(res) + ")");
            return -1;
        }
    }

    int status = ctx->ProcessByteStreams(*input, *output, fragments_info);

    input->Release();
    output->Release();
    if (fragments_info) fragments_info->Release();

    return status;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptMemory(
    Mp4DecryptSession* ctx,
    const uint8_t* input_data,
    uint64_t input_size,
    const uint8_t* fragments_info_data,
    uint64_t fragments_info_size,
    uint8_t** out_data,
    uint64_t* out_size) {
    if (!ctx || !input_data || !out_data || !out_size || input_size == 0) {
        if (ctx) ctx->SetError("Invalid arguments: input_data, out_data, and out_size must be valid.");
        return -1;
    }
    *out_data = nullptr;
    *out_size = 0;

    auto* input = new AP4_MemoryByteStream(input_data, static_cast<AP4_Size>(input_size));
    auto* output = new AP4_MemoryByteStream();
    AP4_MemoryByteStream* fragments_info = nullptr;

    if (fragments_info_data && fragments_info_size > 0) {
        fragments_info = new AP4_MemoryByteStream(fragments_info_data, static_cast<AP4_Size>(fragments_info_size));
    }

    int status = ctx->ProcessByteStreams(*input, *output, fragments_info);

    input->Release();
    if (fragments_info) fragments_info->Release();

    if (status != 0) {
        output->Release();
        return status;
    }

    AP4_Size res_size = output->GetDataSize();
    if (res_size == 0) {
        output->Release();
        *out_data = nullptr;
        *out_size = 0;
        return 0;
    }

    auto* buffer = static_cast<uint8_t*>(std::malloc(res_size));
    if (!buffer) {
        output->Release();
        ctx->SetError("Failed to allocate memory buffer for decrypted output.");
        return -1;
    }

    std::memcpy(buffer, output->GetData(), res_size);
    output->Release();
    *out_data = buffer;
    *out_size = res_size;
    return 0;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptBuffer(
    const uint8_t* in_data,
    uint64_t in_size,
    const char* kid_or_track_hex,
    const char* key_hex,
    uint8_t** out_data,
    uint64_t* out_size) {
    if (!in_data || in_size == 0 || !kid_or_track_hex || !key_hex || !out_data || !out_size) {
        return -1;
    }

    Mp4DecryptSession session;
    if (session.AddKey(kid_or_track_hex, key_hex) != 0) {
        return -1;
    }

    return Mp4Decrypt_DecryptMemory(&session, in_data, in_size, nullptr, 0, out_data, out_size);
}

MP4DECRYPT_API_EXPORT void Mp4Decrypt_FreeBuffer(uint8_t* buffer) {
    std::free(buffer);
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_DecryptCustom(
    Mp4DecryptSession* ctx,
    const Mp4CustomStream* input_stream,
    const Mp4CustomStream* output_stream,
    const Mp4CustomStream* fragments_info_stream) {
    if (!ctx || !input_stream || !output_stream) {
        if (ctx) ctx->SetError("Invalid arguments: input_stream and output_stream are required.");
        return -1;
    }

    auto* input = new Ap4CallbackByteStream(*input_stream);
    auto* output = new Ap4CallbackByteStream(*output_stream);
    Ap4CallbackByteStream* fragments_info = nullptr;
    if (fragments_info_stream) {
        fragments_info = new Ap4CallbackByteStream(*fragments_info_stream);
    }

    int status = ctx->ProcessByteStreams(*input, *output, fragments_info);

    input->Release();
    output->Release();
    if (fragments_info) fragments_info->Release();

    return status;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_Cancel(Mp4DecryptSession* ctx) {
    if (!ctx) return -1;
    ctx->Cancel();
    return 0;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_GetStats(Mp4DecryptSession* ctx, Mp4DecryptStats* out_stats) {
    if (!ctx || !out_stats) return -1;
    return ctx->GetStats(out_stats);
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_ProbeFile(const char* file_path, Mp4MediaInfo* out_info) {
    if (!file_path || !out_info) return -1;

    AP4_ByteStream* stream = nullptr;
    AP4_Result res = AP4_FileByteStream::Create(file_path, AP4_FileByteStream::STREAM_MODE_READ, stream);
    if (AP4_FAILED(res)) return -1;

    int status = Mp4DecryptSession::ProbeStream(*stream, out_info, nullptr);
    stream->Release();
    return status;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_ProbeMemory(const uint8_t* data, uint64_t size, Mp4MediaInfo* out_info) {
    if (!data || size == 0 || !out_info) return -1;
    auto* stream = new AP4_MemoryByteStream(data, static_cast<AP4_Size>(size));
    int status = Mp4DecryptSession::ProbeStream(*stream, out_info, nullptr);
    stream->Release();
    return status;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_GetApiVersion(void) {
    return MP4DECRYPT_API_VERSION_MAJOR;
}

MP4DECRYPT_API_EXPORT int Mp4Decrypt_GetVersion(Mp4DecryptVersionInfo* out_info) {
    if (!out_info) return -1;
    out_info->major = MP4DECRYPT_API_VERSION_MAJOR;
    out_info->minor = MP4DECRYPT_API_VERSION_MINOR;
    out_info->patch = MP4DECRYPT_API_VERSION_PATCH;
    out_info->abi_revision = MP4DECRYPT_API_ABI_REVISION;
    out_info->bento4_version = AP4_VERSION_STRING;
    out_info->build_date = __DATE__ " " __TIME__;
    return 0;
}

MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetVersionString(void) {
    static char version_str[256];
    static bool initialized = false;
    if (!initialized) {
        std::snprintf(version_str, sizeof(version_str),
                      "mp4decrypt-api v%u.%u.%u (ABI rev %u, Bento4 v%s, built %s)",
                      MP4DECRYPT_API_VERSION_MAJOR,
                      MP4DECRYPT_API_VERSION_MINOR,
                      MP4DECRYPT_API_VERSION_PATCH,
                      MP4DECRYPT_API_ABI_REVISION,
                      AP4_VERSION_STRING,
                      __DATE__ " " __TIME__);
        initialized = true;
    }
    return version_str;
}

MP4DECRYPT_API_EXPORT const char* Mp4Decrypt_GetVersionJson(void) {
    static std::string json_str;
    static std::mutex json_mutex;
    std::lock_guard<std::mutex> lock(json_mutex);
    if (json_str.empty()) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"name\": \"mp4decrypt-api\",\n"
           << "  \"version\": \"" << MP4DECRYPT_API_VERSION_MAJOR << "."
           << MP4DECRYPT_API_VERSION_MINOR << "." << MP4DECRYPT_API_VERSION_PATCH << "\",\n"
           << "  \"major\": " << MP4DECRYPT_API_VERSION_MAJOR << ",\n"
           << "  \"minor\": " << MP4DECRYPT_API_VERSION_MINOR << ",\n"
           << "  \"patch\": " << MP4DECRYPT_API_VERSION_PATCH << ",\n"
           << "  \"abiRevision\": " << MP4DECRYPT_API_ABI_REVISION << ",\n"
           << "  \"bento4Version\": \"" << AP4_VERSION_STRING << "\",\n"
           << "  \"buildDate\": \"" << __DATE__ << " " << __TIME__ << "\"\n"
           << "}";
        json_str = ss.str();
    }
    return json_str.c_str();
}

} // extern "C"
