/**
 * mp4decrypt-api - Custom Streaming / Callback Adapter
 * ===================================================
 *
 * Implements Bento4's AP4_ByteStream interface by forwarding I/O
 * requests to user-defined C callbacks (read, write, seek, tell, size).
 */

#ifndef MP4DECRYPT_CALLBACK_STREAM_H
#define MP4DECRYPT_CALLBACK_STREAM_H

#include "Ap4ByteStream.h"
#include "mp4decrypt_api.h"
#include <atomic>
#include <cstdio>

class Ap4CallbackByteStream : public AP4_ByteStream {
public:
    explicit Ap4CallbackByteStream(const Mp4CustomStream& stream_cb)
        : cb_(stream_cb), ref_count_(1), position_(0) {}

    AP4_Result ReadPartial(void* buffer, AP4_Size bytes_to_read, AP4_Size& bytes_read) override {
        if (!cb_.read_cb) {
            bytes_read = 0;
            return AP4_ERROR_NOT_SUPPORTED;
        }
        int64_t n = cb_.read_cb(cb_.user_data, static_cast<uint8_t*>(buffer), bytes_to_read);
        if (n < 0) {
            bytes_read = 0;
            return AP4_ERROR_READ_FAILED;
        }
        bytes_read = static_cast<AP4_Size>(n);
        position_ += bytes_read;
        return (bytes_read == 0 && bytes_to_read > 0) ? AP4_ERROR_EOS : AP4_SUCCESS;
    }

    AP4_Result WritePartial(const void* buffer, AP4_Size bytes_to_write, AP4_Size& bytes_written) override {
        if (!cb_.write_cb) {
            bytes_written = 0;
            return AP4_ERROR_NOT_SUPPORTED;
        }
        int64_t n = cb_.write_cb(cb_.user_data, static_cast<const uint8_t*>(buffer), bytes_to_write);
        if (n < 0) {
            bytes_written = 0;
            return AP4_ERROR_WRITE_FAILED;
        }
        bytes_written = static_cast<AP4_Size>(n);
        position_ += bytes_written;
        return AP4_SUCCESS;
    }

    AP4_Result Seek(AP4_Position position) override {
        if (cb_.seek_cb) {
            int64_t res = cb_.seek_cb(cb_.user_data, position);
            if (res < 0) return AP4_ERROR_NOT_SUPPORTED;
            position_ = position;
            return AP4_SUCCESS;
        }
        if (position == position_) return AP4_SUCCESS;
        return AP4_ERROR_NOT_SUPPORTED;
    }

    AP4_Result Tell(AP4_Position& position) override {
        if (cb_.tell_cb) {
            int64_t pos = cb_.tell_cb(cb_.user_data);
            if (pos >= 0) {
                position_ = static_cast<AP4_Position>(pos);
                position = position_;
                return AP4_SUCCESS;
            }
        }
        position = position_;
        return AP4_SUCCESS;
    }

    AP4_Result GetSize(AP4_LargeSize& size) override {
        if (cb_.size_cb) {
            size = cb_.size_cb(cb_.user_data);
            return AP4_SUCCESS;
        }
        return AP4_ERROR_NOT_SUPPORTED;
    }

    void AddReference() override {
        ++ref_count_;
    }

    void Release() override {
        if (--ref_count_ == 0) {
            delete this;
        }
    }

protected:
    ~Ap4CallbackByteStream() override = default;

private:
    Mp4CustomStream cb_;
    std::atomic<uint32_t> ref_count_;
    AP4_Position position_;
};

#endif // MP4DECRYPT_CALLBACK_STREAM_H
