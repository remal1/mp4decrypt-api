"""
mp4decrypt-api - High-Level Python SDK
=======================================

Thread-safe, high-performance Python bindings for Bento4 mp4decrypt C ABI
using zero-dependency ctypes. Supports file decryption, in-memory buffers,
streaming custom I/O, real-time progress callbacks, and media probing.
"""

from __future__ import annotations

import ctypes
import json
import os
from dataclasses import dataclass
from enum import IntEnum
from typing import Callable, List, Optional, Union, BinaryIO


class Mp4LogLevel(IntEnum):
    DEBUG = 0
    INFO = 1
    WARN = 2
    ERROR = 3
    QUIET = 4


# ---------------------------------------------------------------------------
# C ABI Struct Definitions
# ---------------------------------------------------------------------------

class Mp4DecryptStatsStruct(ctypes.Structure):
    _fields_ = [
        ("total_bytes_read", ctypes.c_uint64),
        ("total_bytes_written", ctypes.c_uint64),
        ("execution_duration_ms", ctypes.c_double),
        ("throughput_mb_per_sec", ctypes.c_double),
    ]


class Mp4TrackMetadataStruct(ctypes.Structure):
    _fields_ = [
        ("track_id", ctypes.c_uint32),
        ("stream_type", ctypes.c_int),
        ("handler_type", ctypes.c_char * 8),
        ("codec", ctypes.c_char * 32),
        ("is_encrypted", ctypes.c_int),
        ("scheme_type", ctypes.c_char * 8),
        ("default_kid_hex", ctypes.c_char * 33),
        ("_pad1", ctypes.c_uint8 * 3),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("frame_rate", ctypes.c_double),
        ("audio_channels", ctypes.c_uint32),
        ("sample_rate", ctypes.c_uint32),
        ("duration_seconds", ctypes.c_double),
    ]


class Mp4MediaInfoStruct(ctypes.Structure):
    _fields_ = [
        ("track_count", ctypes.c_uint32),
        ("_pad1", ctypes.c_uint32),
        ("tracks", Mp4TrackMetadataStruct * 32),
        ("container_brands", ctypes.c_char * 64),
        ("duration_seconds", ctypes.c_double),
        ("is_fragmented", ctypes.c_int),
        ("_pad2", ctypes.c_int),
    ]


# Callback Function Signatures
Mp4ProgressCbType = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p)
Mp4LogCbType = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)

Mp4ReadFuncType = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint64)
Mp4WriteFuncType = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint64)
Mp4SeekFuncType = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_void_p, ctypes.c_uint64)
Mp4TellFuncType = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_void_p)
Mp4SizeFuncType = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_void_p)


class Mp4CustomStreamStruct(ctypes.Structure):
    _fields_ = [
        ("user_data", ctypes.c_void_p),
        ("read_cb", Mp4ReadFuncType),
        ("write_cb", Mp4WriteFuncType),
        ("seek_cb", Mp4SeekFuncType),
        ("tell_cb", Mp4TellFuncType),
        ("size_cb", Mp4SizeFuncType),
    ]


# ---------------------------------------------------------------------------
# High-Level Dataclasses
# ---------------------------------------------------------------------------

@dataclass
class DecryptStats:
    total_bytes_read: int
    total_bytes_written: int
    execution_duration_ms: float
    throughput_mb_per_sec: float


@dataclass
class TrackMetadata:
    track_id: int
    stream_type: str
    handler_type: str
    codec: str
    is_encrypted: bool
    scheme_type: str
    default_kid_hex: str
    width: int
    height: int
    frame_rate: float
    audio_channels: int
    sample_rate: int
    duration_seconds: float


@dataclass
class MediaInfo:
    track_count: int
    tracks: List[TrackMetadata]
    container_brands: str
    duration_seconds: float
    is_fragmented: bool


@dataclass
class VersionInfo:
    major: int
    minor: int
    patch: int
    abi_revision: int
    bento4_version: str
    build_date: str
    version_string: str


# ---------------------------------------------------------------------------
# Library Loader
# ---------------------------------------------------------------------------

def _find_library(custom_path: Optional[str] = None) -> str:
    if custom_path and os.path.exists(custom_path):
        return os.path.abspath(custom_path)

    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    candidates = [
        os.path.join(repo_root, "build", "Release", "mp4decrypt_api.dll"),
        os.path.join(repo_root, "build", "mp4decrypt_api.dll"),
        os.path.join(repo_root, "build", "libmp4decrypt_api.so"),
        os.path.join(repo_root, "build", "libmp4decrypt_api.dylib"),
        os.path.join(os.getcwd(), "build", "Release", "mp4decrypt_api.dll"),
        os.path.join(os.getcwd(), "build", "mp4decrypt_api.dll"),
        os.path.join(os.getcwd(), "build", "libmp4decrypt_api.so"),
        os.path.join(os.getcwd(), "build", "libmp4decrypt_api.dylib"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    raise FileNotFoundError(f"Could not find mp4decrypt_api shared library in candidates: {candidates}")


_lib_instance = None


def _get_lib(custom_path: Optional[str] = None):
    global _lib_instance
    if _lib_instance is None:
        lib_path = _find_library(custom_path)
        lib = ctypes.CDLL(lib_path)

        # Lifecycle
        lib.Mp4Decrypt_Create.restype = ctypes.c_void_p
        lib.Mp4Decrypt_Create.argtypes = []

        lib.Mp4Decrypt_Destroy.restype = None
        lib.Mp4Decrypt_Destroy.argtypes = [ctypes.c_void_p]

        lib.Mp4Decrypt_GetLastError.restype = ctypes.c_char_p
        lib.Mp4Decrypt_GetLastError.argtypes = [ctypes.c_void_p]

        lib.Mp4Decrypt_AddKey.restype = ctypes.c_int
        lib.Mp4Decrypt_AddKey.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]

        lib.Mp4Decrypt_ClearKeys.restype = ctypes.c_int
        lib.Mp4Decrypt_ClearKeys.argtypes = [ctypes.c_void_p]

        lib.Mp4Decrypt_SetProgressCallback.restype = ctypes.c_int
        lib.Mp4Decrypt_SetProgressCallback.argtypes = [ctypes.c_void_p, Mp4ProgressCbType, ctypes.c_void_p]

        lib.Mp4Decrypt_SetLogCallback.restype = ctypes.c_int
        lib.Mp4Decrypt_SetLogCallback.argtypes = [ctypes.c_void_p, Mp4LogCbType, ctypes.c_void_p]

        lib.Mp4Decrypt_SetLogLevel.restype = ctypes.c_int
        lib.Mp4Decrypt_SetLogLevel.argtypes = [ctypes.c_void_p, ctypes.c_int]

        # Paradigms
        lib.Mp4Decrypt_DecryptFile.restype = ctypes.c_int
        lib.Mp4Decrypt_DecryptFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]

        lib.Mp4Decrypt_DecryptMemory.restype = ctypes.c_int
        lib.Mp4Decrypt_DecryptMemory.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_uint64),
        ]

        lib.Mp4Decrypt_DecryptBuffer.restype = ctypes.c_int
        lib.Mp4Decrypt_DecryptBuffer.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_uint64),
        ]

        lib.Mp4Decrypt_FreeBuffer.restype = None
        lib.Mp4Decrypt_FreeBuffer.argtypes = [ctypes.POINTER(ctypes.c_uint8)]

        lib.Mp4Decrypt_DecryptCustom.restype = ctypes.c_int
        lib.Mp4Decrypt_DecryptCustom.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(Mp4CustomStreamStruct),
            ctypes.POINTER(Mp4CustomStreamStruct),
            ctypes.POINTER(Mp4CustomStreamStruct),
        ]

        lib.Mp4Decrypt_Cancel.restype = ctypes.c_int
        lib.Mp4Decrypt_Cancel.argtypes = [ctypes.c_void_p]

        lib.Mp4Decrypt_GetStats.restype = ctypes.c_int
        lib.Mp4Decrypt_GetStats.argtypes = [ctypes.c_void_p, ctypes.POINTER(Mp4DecryptStatsStruct)]

        lib.Mp4Decrypt_ProbeFile.restype = ctypes.c_int
        lib.Mp4Decrypt_ProbeFile.argtypes = [ctypes.c_char_p, ctypes.POINTER(Mp4MediaInfoStruct)]

        lib.Mp4Decrypt_ProbeMemory.restype = ctypes.c_int
        lib.Mp4Decrypt_ProbeMemory.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint64, ctypes.POINTER(Mp4MediaInfoStruct)]

        lib.Mp4Decrypt_GetApiVersion.restype = ctypes.c_int
        lib.Mp4Decrypt_GetApiVersion.argtypes = []

        lib.Mp4Decrypt_GetVersionString.restype = ctypes.c_char_p
        lib.Mp4Decrypt_GetVersionString.argtypes = []

        lib.Mp4Decrypt_GetVersionJson.restype = ctypes.c_char_p
        lib.Mp4Decrypt_GetVersionJson.argtypes = []

        _lib_instance = lib

    return _lib_instance


def _parse_media_info(raw: Mp4MediaInfoStruct) -> MediaInfo:
    tracks: List[TrackMetadata] = []
    type_map = {1: "AUDIO", 2: "VIDEO", 3: "SUBTITLES"}

    for i in range(raw.track_count):
        t = raw.tracks[i]
        tracks.append(
            TrackMetadata(
                track_id=t.track_id,
                stream_type=type_map.get(t.stream_type, "UNKNOWN"),
                handler_type=t.handler_type.decode("utf-8", errors="replace"),
                codec=t.codec.decode("utf-8", errors="replace"),
                is_encrypted=bool(t.is_encrypted),
                scheme_type=t.scheme_type.decode("utf-8", errors="replace"),
                default_kid_hex=t.default_kid_hex.decode("utf-8", errors="replace"),
                width=t.width,
                height=t.height,
                frame_rate=t.frame_rate,
                audio_channels=t.audio_channels,
                sample_rate=t.sample_rate,
                duration_seconds=t.duration_seconds,
            )
        )

    return MediaInfo(
        track_count=raw.track_count,
        tracks=tracks,
        container_brands=raw.container_brands.decode("utf-8", errors="replace"),
        duration_seconds=raw.duration_seconds,
        is_fragmented=bool(raw.is_fragmented),
    )


# ---------------------------------------------------------------------------
# Object-Oriented Session
# ---------------------------------------------------------------------------

class Mp4DecryptSession:
    def __init__(self, custom_lib_path: Optional[str] = None):
        self._lib = _get_lib(custom_lib_path)
        self._handle = self._lib.Mp4Decrypt_Create()
        if not self._handle:
            raise RuntimeError("Failed to create Mp4DecryptSession native context.")

        self._c_progress_cb: Optional[Mp4ProgressCbType] = None
        self._c_log_cb: Optional[Mp4LogCbType] = None

    def __enter__(self) -> "Mp4DecryptSession":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()

    def destroy(self) -> None:
        if self._handle:
            self._lib.Mp4Decrypt_Destroy(self._handle)
            self._handle = None
        self._c_progress_cb = None
        self._c_log_cb = None

    def _check_handle(self) -> None:
        if not self._handle:
            raise RuntimeError("Mp4DecryptSession has already been destroyed.")

    def get_last_error(self) -> str:
        self._check_handle()
        err = self._lib.Mp4Decrypt_GetLastError(self._handle)
        return err.decode("utf-8", errors="replace") if err else ""

    def add_key(self, id_spec: Union[str, int], key_hex: str) -> "Mp4DecryptSession":
        self._check_handle()
        id_bytes = str(id_spec).encode("utf-8")
        key_bytes = key_hex.encode("utf-8")
        ret = self._lib.Mp4Decrypt_AddKey(self._handle, id_bytes, key_bytes)
        if ret != 0:
            raise ValueError(f"Failed to add key: {self.get_last_error()}")
        return self

    def clear_keys(self) -> "Mp4DecryptSession":
        self._check_handle()
        self._lib.Mp4Decrypt_ClearKeys(self._handle)
        return self

    def on_progress(self, callback: Callable[[int, int], None]) -> "Mp4DecryptSession":
        self._check_handle()

        def _native_cb(step: int, total: int, user_data):
            callback(step, total)

        self._c_progress_cb = Mp4ProgressCbType(_native_cb)
        self._lib.Mp4Decrypt_SetProgressCallback(self._handle, self._c_progress_cb, None)
        return self

    def on_log(self, callback: Callable[[Mp4LogLevel, str], None]) -> "Mp4DecryptSession":
        self._check_handle()

        def _native_cb(level: int, msg_ptr, user_data):
            msg = msg_ptr.decode("utf-8", errors="replace") if msg_ptr else ""
            callback(Mp4LogLevel(level), msg)

        self._c_log_cb = Mp4LogCbType(_native_cb)
        self._lib.Mp4Decrypt_SetLogCallback(self._handle, self._c_log_cb, None)
        return self

    def set_log_level(self, level: Mp4LogLevel) -> "Mp4DecryptSession":
        self._check_handle()
        self._lib.Mp4Decrypt_SetLogLevel(self._handle, int(level))
        return self

    def cancel(self) -> None:
        self._check_handle()
        self._lib.Mp4Decrypt_Cancel(self._handle)

    def decrypt_file(self, input_path: str, output_path: str, fragments_info_path: Optional[str] = None) -> None:
        self._check_handle()
        in_bytes = os.path.abspath(input_path).encode("utf-8")
        out_bytes = os.path.abspath(output_path).encode("utf-8")
        frag_bytes = os.path.abspath(fragments_info_path).encode("utf-8") if fragments_info_path else None

        ret = self._lib.Mp4Decrypt_DecryptFile(self._handle, in_bytes, out_bytes, frag_bytes)
        if ret != 0:
            raise RuntimeError(f"File decryption failed ({ret}): {self.get_last_error()}")

    def decrypt_memory(self, input_bytes: bytes, fragments_info: Optional[bytes] = None) -> bytes:
        self._check_handle()
        in_len = len(input_bytes)
        in_buf = (ctypes.c_uint8 * in_len).from_buffer_copy(input_bytes)

        frag_buf = None
        frag_len = 0
        if fragments_info:
            frag_len = len(fragments_info)
            frag_buf = (ctypes.c_uint8 * frag_len).from_buffer_copy(fragments_info)

        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_size = ctypes.c_uint64(0)

        ret = self._lib.Mp4Decrypt_DecryptMemory(
            self._handle,
            in_buf,
            in_len,
            frag_buf if frag_buf else None,
            frag_len,
            ctypes.byref(out_ptr),
            ctypes.byref(out_size),
        )

        if ret != 0:
            raise RuntimeError(f"Memory decryption failed ({ret}): {self.get_last_error()}")

        size = out_size.value
        if not out_ptr or size == 0:
            return b""

        try:
            return bytes(ctypes.string_at(out_ptr, size))
        finally:
            self._lib.Mp4Decrypt_FreeBuffer(out_ptr)

    def decrypt_custom(self, input_stream: BinaryIO, output_stream: BinaryIO, fragments_stream: Optional[BinaryIO] = None) -> None:
        """
        Decrypts media using Python file-like objects or custom streaming buffers.
        """
        self._check_handle()

        # Hold references to prevent GC of callbacks during execution
        c_callbacks = []

        def make_stream_struct(s: Optional[BinaryIO], is_input: bool) -> Optional[Mp4CustomStreamStruct]:
            if s is None:
                return None
            st = Mp4CustomStreamStruct()

            if is_input:
                def read_fn(user_data, buf_ptr, count):
                    try:
                        data = s.read(count)
                        if not data:
                            return 0
                        ctypes.memmove(buf_ptr, data, len(data))
                        return len(data)
                    except Exception:
                        return -1

                c_read = Mp4ReadFuncType(read_fn)
                c_callbacks.append(c_read)
                st.read_cb = c_read
            else:
                def write_fn(user_data, buf_ptr, count):
                    try:
                        raw = bytes(ctypes.string_at(buf_ptr, count))
                        s.write(raw)
                        return count
                    except Exception:
                        return -1

                c_write = Mp4WriteFuncType(write_fn)
                c_callbacks.append(c_write)
                st.write_cb = c_write

            if s.seekable():
                def seek_fn(user_data, offset):
                    try:
                        s.seek(offset)
                        return 0
                    except Exception:
                        return -1

                def tell_fn(user_data):
                    try:
                        return s.tell()
                    except Exception:
                        return -1

                c_seek = Mp4SeekFuncType(seek_fn)
                c_tell = Mp4TellFuncType(tell_fn)
                c_callbacks.extend([c_seek, c_tell])
                st.seek_cb = c_seek
                st.tell_cb = c_tell

            return st

        in_st = make_stream_struct(input_stream, True)
        out_st = make_stream_struct(output_stream, False)
        frag_st = make_stream_struct(fragments_stream, True)

        ret = self._lib.Mp4Decrypt_DecryptCustom(
            self._handle,
            ctypes.byref(in_st) if in_st else None,
            ctypes.byref(out_st) if out_st else None,
            ctypes.byref(frag_st) if frag_st else None,
        )

        if ret != 0:
            raise RuntimeError(f"Custom streaming decryption failed ({ret}): {self.get_last_error()}")

    def get_stats(self) -> DecryptStats:
        self._check_handle()
        raw = Mp4DecryptStatsStruct()
        ret = self._lib.Mp4Decrypt_GetStats(self._handle, ctypes.byref(raw))
        if ret != 0:
            raise RuntimeError(f"Failed to get stats: {self.get_last_error()}")
        return DecryptStats(
            total_bytes_read=raw.total_bytes_read,
            total_bytes_written=raw.total_bytes_written,
            execution_duration_ms=raw.execution_duration_ms,
            throughput_mb_per_sec=raw.throughput_mb_per_sec,
        )

    # -------------------------------------------------------------------------
    # Static Convenience Methods
    # -------------------------------------------------------------------------

    @classmethod
    def decrypt_buffer(cls, data: bytes, kid_or_track: Union[str, int], key_hex: str, custom_lib_path: Optional[str] = None) -> bytes:
        lib = _get_lib(custom_lib_path)
        in_len = len(data)
        in_buf = (ctypes.c_uint8 * in_len).from_buffer_copy(data)

        id_bytes = str(kid_or_track).encode("utf-8")
        key_bytes = key_hex.encode("utf-8")

        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_size = ctypes.c_uint64(0)

        ret = lib.Mp4Decrypt_DecryptBuffer(
            in_buf,
            in_len,
            id_bytes,
            key_bytes,
            ctypes.byref(out_ptr),
            ctypes.byref(out_size),
        )

        if ret != 0:
            raise RuntimeError(f"One-shot buffer decryption failed with code {ret}")

        size = out_size.value
        if not out_ptr or size == 0:
            return b""

        try:
            return bytes(ctypes.string_at(out_ptr, size))
        finally:
            lib.Mp4Decrypt_FreeBuffer(out_ptr)

    @classmethod
    def probe(cls, target: Union[str, bytes], custom_lib_path: Optional[str] = None) -> MediaInfo:
        lib = _get_lib(custom_lib_path)
        raw_info = Mp4MediaInfoStruct()

        if isinstance(target, str):
            file_bytes = os.path.abspath(target).encode("utf-8")
            ret = lib.Mp4Decrypt_ProbeFile(file_bytes, ctypes.byref(raw_info))
        else:
            in_len = len(target)
            in_buf = (ctypes.c_uint8 * in_len).from_buffer_copy(target)
            ret = lib.Mp4Decrypt_ProbeMemory(in_buf, in_len, ctypes.byref(raw_info))

        if ret != 0:
            raise RuntimeError(f"Media probe failed with code {ret}")

        return _parse_media_info(raw_info)

    @classmethod
    def get_api_version(cls, custom_lib_path: Optional[str] = None) -> int:
        return _get_lib(custom_lib_path).Mp4Decrypt_GetApiVersion()

    @classmethod
    def get_version_string(cls, custom_lib_path: Optional[str] = None) -> str:
        s = _get_lib(custom_lib_path).Mp4Decrypt_GetVersionString()
        return s.decode("utf-8", errors="replace") if s else ""

    @classmethod
    def get_version(cls, custom_lib_path: Optional[str] = None) -> VersionInfo:
        raw = _get_lib(custom_lib_path).Mp4Decrypt_GetVersionJson()
        parsed = json.loads(raw.decode("utf-8")) if raw else {}
        return VersionInfo(
            major=parsed.get("major", 1),
            minor=parsed.get("minor", 0),
            patch=parsed.get("patch", 0),
            abi_revision=parsed.get("abiRevision", 1),
            bento4_version=parsed.get("bento4Version", ""),
            build_date=parsed.get("buildDate", ""),
            version_string=cls.get_version_string(custom_lib_path),
        )

    # Alias for convenience
    version = get_version
