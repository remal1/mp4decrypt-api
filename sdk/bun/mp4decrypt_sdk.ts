/**
 * mp4decrypt-api - High-Level TypeScript / Bun SDK
 * ==================================================
 *
 * Object-oriented TypeScript wrapper using bun:ffi for high-performance,
 * thread-safe MP4/CMAF decryption, media probing, and custom streaming.
 */

import { dlopen, FFIType, ptr, toArrayBuffer, CString, JSCallback } from "bun:ffi";
import path from "node:path";
import fs from "node:fs";
import { fileURLToPath } from "node:url";

export enum Mp4LogLevel {
  DEBUG = 0,
  INFO  = 1,
  WARN  = 2,
  ERROR = 3,
  QUIET = 4,
}

export interface Mp4VersionInfo {
  major: number;
  minor: number;
  patch: number;
  abiRevision: number;
  bento4Version: string;
  buildDate: string;
  versionString: string;
}

export interface Mp4DecryptStats {
  totalBytesRead: number;
  totalBytesWritten: number;
  executionDurationMs: number;
  throughputMBps: number;
}

export interface Mp4TrackMetadata {
  trackId: number;
  streamType: "AUDIO" | "VIDEO" | "SUBTITLES" | "UNKNOWN";
  handlerType: string;
  codec: string;
  isEncrypted: boolean;
  schemeType: string;
  defaultKidHex: string;
  width: number;
  height: number;
  frameRate: number;
  audioChannels: number;
  sampleRate: number;
  durationSeconds: number;
}

export interface Mp4MediaInfo {
  trackCount: number;
  tracks: Mp4TrackMetadata[];
  containerBrands: string;
  durationSeconds: number;
  isFragmented: boolean;
}

export interface Mp4CustomStreamCallbacks {
  userContext?: any;
  read?: (buffer: Uint8Array, bytesToRead: number) => number;
  write?: (buffer: Uint8Array, bytesToWrite: number) => number;
  seek?: (offset: number) => number;
  tell?: () => number;
  size?: () => number;
}

function findLibrary(customPath?: string): string {
  if (customPath && fs.existsSync(customPath)) return path.resolve(customPath);
  const scriptDir = path.dirname(fileURLToPath(import.meta.url));
  const repoRoot = path.resolve(scriptDir, "..", "..");
  const candidates = [
    path.join(repoRoot, "build", "Release", "mp4decrypt_api.dll"),
    path.join(repoRoot, "build", "mp4decrypt_api.dll"),
    path.join(repoRoot, "build", "libmp4decrypt_api.so"),
    path.join(repoRoot, "build", "libmp4decrypt_api.dylib"),
    path.join(process.cwd(), "build", "Release", "mp4decrypt_api.dll"),
    path.join(process.cwd(), "build", "mp4decrypt_api.dll"),
    path.join(process.cwd(), "build", "libmp4decrypt_api.so"),
    path.join(process.cwd(), "build", "libmp4decrypt_api.dylib"),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  throw new Error(`Could not find mp4decrypt_api shared library in candidates: ${candidates.join(", ")}`);
}

let _ffi: any = null;

function getFFI() {
  if (!_ffi) {
    const libPath = findLibrary();
    _ffi = dlopen(libPath, {
      Mp4Decrypt_Create: { args: [], returns: FFIType.ptr },
      Mp4Decrypt_Destroy: { args: [FFIType.ptr], returns: FFIType.void },
      Mp4Decrypt_GetLastError: { args: [FFIType.ptr], returns: FFIType.cstring },
      Mp4Decrypt_AddKey: { args: [FFIType.ptr, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },
      Mp4Decrypt_ClearKeys: { args: [FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_SetProgressCallback: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_SetLogCallback: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_SetLogLevel: { args: [FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      Mp4Decrypt_DecryptFile: { args: [FFIType.ptr, FFIType.cstring, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },
      Mp4Decrypt_DecryptMemory: { args: [FFIType.ptr, FFIType.ptr, FFIType.u64, FFIType.ptr, FFIType.u64, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_DecryptBuffer: { args: [FFIType.ptr, FFIType.u64, FFIType.cstring, FFIType.cstring, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_FreeBuffer: { args: [FFIType.ptr], returns: FFIType.void },
      Mp4Decrypt_DecryptCustom: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_Cancel: { args: [FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_GetStats: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_ProbeFile: { args: [FFIType.cstring, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_ProbeMemory: { args: [FFIType.ptr, FFIType.u64, FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_GetApiVersion: { args: [], returns: FFIType.i32 },
      Mp4Decrypt_GetVersion: { args: [FFIType.ptr], returns: FFIType.i32 },
      Mp4Decrypt_GetVersionString: { args: [], returns: FFIType.cstring },
      Mp4Decrypt_GetVersionJson: { args: [], returns: FFIType.cstring },
    });
  }
  return _ffi.symbols;
}

function parseMediaInfoBuffer(buf: Buffer): Mp4MediaInfo {
  const trackCount = buf.readUInt32LE(0);
  const tracks: Mp4TrackMetadata[] = [];
  const trackStructSize = 128;
  let offset = 8;

  for (let i = 0; i < trackCount; i++) {
    const trackId = buf.readUInt32LE(offset);
    const streamTypeInt = buf.readInt32LE(offset + 4);
    const handlerType = buf.toString("utf8", offset + 8, offset + 16).replace(/\0/g, "");
    const codec = buf.toString("utf8", offset + 16, offset + 48).replace(/\0/g, "");
    const isEncrypted = buf.readInt32LE(offset + 48) === 1;
    const schemeType = buf.toString("utf8", offset + 52, offset + 60).replace(/\0/g, "");
    const defaultKidHex = buf.toString("utf8", offset + 60, offset + 93).replace(/\0/g, "");

    const width = buf.readUInt32LE(offset + 96);
    const height = buf.readUInt32LE(offset + 100);
    const frameRate = buf.readDoubleLE(offset + 104);
    const audioChannels = buf.readUInt32LE(offset + 112);
    const sampleRate = buf.readUInt32LE(offset + 116);
    const durationSeconds = buf.readDoubleLE(offset + 120);

    let streamType: Mp4TrackMetadata["streamType"] = "UNKNOWN";
    if (streamTypeInt === 1) streamType = "AUDIO";
    else if (streamTypeInt === 2) streamType = "VIDEO";
    else if (streamTypeInt === 3) streamType = "SUBTITLES";

    tracks.push({
      trackId,
      streamType,
      handlerType,
      codec,
      isEncrypted,
      schemeType,
      defaultKidHex,
      width,
      height,
      frameRate,
      audioChannels,
      sampleRate,
      durationSeconds,
    });
    offset += trackStructSize;
  }

  const containerBrands = buf.toString("utf8", 4104, 4168).replace(/\0/g, "");
  const durationSeconds = buf.readDoubleLE(4168);
  const isFragmented = buf.readInt32LE(4176) === 1;

  return {
    trackCount,
    tracks,
    containerBrands,
    durationSeconds,
    isFragmented,
  };
}

/**
 * Object-oriented MP4 Decryption Session
 */
export class Mp4DecryptSession {
  private handle: any = null;
  private progressCb: JSCallback | null = null;
  private logCb: JSCallback | null = null;

  constructor() {
    const symbols = getFFI();
    this.handle = symbols.Mp4Decrypt_Create();
    if (!this.handle) {
      throw new Error("Failed to create Mp4DecryptSession native context.");
    }
  }

  public destroy(): void {
    if (this.handle) {
      const symbols = getFFI();
      symbols.Mp4Decrypt_Destroy(this.handle);
      this.handle = null;
    }
    if (this.progressCb) {
      this.progressCb.close();
      this.progressCb = null;
    }
    if (this.logCb) {
      this.logCb.close();
      this.logCb = null;
    }
  }

  public [Symbol.dispose](): void {
    this.destroy();
  }

  private checkHandle(): void {
    if (!this.handle) {
      throw new Error("Mp4DecryptSession has been destroyed.");
    }
  }

  public getLastError(): string {
    this.checkHandle();
    const symbols = getFFI();
    const s = symbols.Mp4Decrypt_GetLastError(this.handle);
    return s ? s.toString() : "";
  }

  public addKey(id: string | number, keyHex: string): this {
    this.checkHandle();
    const symbols = getFFI();
    const idStr = Buffer.from(String(id) + "\0", "utf8");
    const keyStr = Buffer.from(keyHex + "\0", "utf8");
    const ret = symbols.Mp4Decrypt_AddKey(this.handle, idStr, keyStr);
    if (ret !== 0) {
      throw new Error(`Failed to add key: ${this.getLastError()}`);
    }
    return this;
  }

  public clearKeys(): this {
    this.checkHandle();
    const symbols = getFFI();
    symbols.Mp4Decrypt_ClearKeys(this.handle);
    return this;
  }

  public onProgress(callback: (step: number, total: number) => void): this {
    this.checkHandle();
    const symbols = getFFI();
    if (this.progressCb) {
      this.progressCb.close();
    }
    this.progressCb = new JSCallback(
      (step: number, total: number, _userData: any) => {
        callback(step, total);
      },
      {
        args: [FFIType.u32, FFIType.u32, FFIType.ptr],
        returns: FFIType.void,
      }
    );
    symbols.Mp4Decrypt_SetProgressCallback(this.handle, this.progressCb.ptr, null);
    return this;
  }

  public onLog(callback: (level: Mp4LogLevel, message: string) => void): this {
    this.checkHandle();
    const symbols = getFFI();
    if (this.logCb) {
      this.logCb.close();
    }
    this.logCb = new JSCallback(
      (level: number, msgPtr: any, _userData: any) => {
        const msg = msgPtr ? new CString(msgPtr).toString() : "";
        callback(level as Mp4LogLevel, msg);
      },
      {
        args: [FFIType.i32, FFIType.ptr, FFIType.ptr],
        returns: FFIType.void,
      }
    );
    symbols.Mp4Decrypt_SetLogCallback(this.handle, this.logCb.ptr, null);
    return this;
  }

  public setLogLevel(level: Mp4LogLevel): this {
    this.checkHandle();
    const symbols = getFFI();
    symbols.Mp4Decrypt_SetLogLevel(this.handle, level);
    return this;
  }

  public cancel(): void {
    this.checkHandle();
    const symbols = getFFI();
    symbols.Mp4Decrypt_Cancel(this.handle);
  }

  public decryptFile(inputPath: string, outputPath: string, fragmentsInfoPath?: string): void {
    this.checkHandle();
    const symbols = getFFI();
    const inStr = Buffer.from(path.resolve(inputPath) + "\0", "utf8");
    const outStr = Buffer.from(path.resolve(outputPath) + "\0", "utf8");
    const fragStr = fragmentsInfoPath ? Buffer.from(path.resolve(fragmentsInfoPath) + "\0", "utf8") : null;

    const ret = symbols.Mp4Decrypt_DecryptFile(this.handle, inStr, outStr, fragStr);
    if (ret !== 0) {
      throw new Error(`File decryption failed (${ret}): ${this.getLastError()}`);
    }
  }

  public decryptMemory(input: Uint8Array, fragmentsInfo?: Uint8Array): Uint8Array {
    this.checkHandle();
    const symbols = getFFI();
    const outDataBuf = new BigUint64Array(1);
    const outSizeBuf = new BigUint64Array(1);

    const ret = symbols.Mp4Decrypt_DecryptMemory(
      this.handle,
      ptr(input),
      BigInt(input.byteLength),
      fragmentsInfo ? ptr(fragmentsInfo) : null,
      fragmentsInfo ? BigInt(fragmentsInfo.byteLength) : 0n,
      ptr(outDataBuf),
      ptr(outSizeBuf)
    );

    if (ret !== 0) {
      throw new Error(`Memory decryption failed (${ret}): ${this.getLastError()}`);
    }

    const outPtr = Number(outDataBuf[0]);
    const outSize = Number(outSizeBuf[0]);
    if (outPtr === 0 || outSize === 0) {
      return new Uint8Array(0);
    }

    const arrayBuf = toArrayBuffer(outPtr, 0, outSize);
    const resultCopy = new Uint8Array(arrayBuf.slice(0));
    symbols.Mp4Decrypt_FreeBuffer(outPtr);
    return resultCopy;
  }

  public decryptCustom(streams: {
    input: Mp4CustomStreamCallbacks;
    output: Mp4CustomStreamCallbacks;
    fragmentsInfo?: Mp4CustomStreamCallbacks;
  }): void {
    this.checkHandle();
    const symbols = getFFI();

    // Helper to build a native Mp4CustomStream struct buffer (48 bytes: user_data, read, write, seek, tell, size)
    const activeCallbacks: JSCallback[] = [];
    const buildStreamStruct = (cb: Mp4CustomStreamCallbacks): Buffer => {
      const struct = Buffer.alloc(48);
      // Offset 0: user_data (8 bytes) -> 0

      if (cb.read) {
        const readCb = new JSCallback(
          (_userData: any, bufPtr: any, bytesToRead: bigint) => {
            const temp = new Uint8Array(toArrayBuffer(bufPtr, 0, Number(bytesToRead)));
            return BigInt(cb.read!(temp, Number(bytesToRead)));
          },
          { args: [FFIType.ptr, FFIType.ptr, FFIType.u64], returns: FFIType.i64 }
        );
        activeCallbacks.push(readCb);
        struct.writeBigUInt64LE(BigInt(readCb.ptr), 8);
      }

      if (cb.write) {
        const writeCb = new JSCallback(
          (_userData: any, bufPtr: any, bytesToWrite: bigint) => {
            const temp = new Uint8Array(toArrayBuffer(bufPtr, 0, Number(bytesToWrite)));
            return BigInt(cb.write!(temp, Number(bytesToWrite)));
          },
          { args: [FFIType.ptr, FFIType.ptr, FFIType.u64], returns: FFIType.i64 }
        );
        activeCallbacks.push(writeCb);
        struct.writeBigUInt64LE(BigInt(writeCb.ptr), 16);
      }

      if (cb.seek) {
        const seekCb = new JSCallback(
          (_userData: any, offset: bigint) => {
            return BigInt(cb.seek!(Number(offset)));
          },
          { args: [FFIType.ptr, FFIType.u64], returns: FFIType.i64 }
        );
        activeCallbacks.push(seekCb);
        struct.writeBigUInt64LE(BigInt(seekCb.ptr), 24);
      }

      if (cb.tell) {
        const tellCb = new JSCallback(
          (_userData: any) => {
            return BigInt(cb.tell!());
          },
          { args: [FFIType.ptr], returns: FFIType.i64 }
        );
        activeCallbacks.push(tellCb);
        struct.writeBigUInt64LE(BigInt(tellCb.ptr), 32);
      }

      if (cb.size) {
        const sizeCb = new JSCallback(
          (_userData: any) => {
            return BigInt(cb.size!());
          },
          { args: [FFIType.ptr], returns: FFIType.u64 }
        );
        activeCallbacks.push(sizeCb);
        struct.writeBigUInt64LE(BigInt(sizeCb.ptr), 40);
      }

      return struct;
    };

    const inStruct = buildStreamStruct(streams.input);
    const outStruct = buildStreamStruct(streams.output);
    const fragStruct = streams.fragmentsInfo ? buildStreamStruct(streams.fragmentsInfo) : null;

    try {
      const ret = symbols.Mp4Decrypt_DecryptCustom(
        this.handle,
        ptr(inStruct),
        ptr(outStruct),
        fragStruct ? ptr(fragStruct) : null
      );
      if (ret !== 0) {
        throw new Error(`Custom stream decryption failed (${ret}): ${this.getLastError()}`);
      }
    } finally {
      for (const cb of activeCallbacks) {
        cb.close();
      }
    }
  }

  public getStats(): Mp4DecryptStats {
    this.checkHandle();
    const symbols = getFFI();
    const statsBuf = Buffer.alloc(32);
    const ret = symbols.Mp4Decrypt_GetStats(this.handle, ptr(statsBuf));
    if (ret !== 0) {
      throw new Error(`Failed to get stats: ${this.getLastError()}`);
    }
    return {
      totalBytesRead: Number(statsBuf.readBigUInt64LE(0)),
      totalBytesWritten: Number(statsBuf.readBigUInt64LE(8)),
      executionDurationMs: statsBuf.readDoubleLE(16),
      throughputMBps: statsBuf.readDoubleLE(24),
    };
  }

  // -------------------------------------------------------------------------
  // Static Convenience Methods
  // -------------------------------------------------------------------------

  public static decryptBuffer(buffer: Uint8Array, kidOrTrack: string | number, keyHex: string): Uint8Array {
    const symbols = getFFI();
    const outDataBuf = new BigUint64Array(1);
    const outSizeBuf = new BigUint64Array(1);
    const idStr = Buffer.from(String(kidOrTrack) + "\0", "utf8");
    const keyStr = Buffer.from(keyHex + "\0", "utf8");

    const ret = symbols.Mp4Decrypt_DecryptBuffer(
      ptr(buffer),
      BigInt(buffer.byteLength),
      idStr,
      keyStr,
      ptr(outDataBuf),
      ptr(outSizeBuf)
    );

    if (ret !== 0) {
      throw new Error(`One-shot buffer decryption failed (code ${ret})`);
    }

    const outPtr = Number(outDataBuf[0]);
    const outSize = Number(outSizeBuf[0]);
    if (outPtr === 0 || outSize === 0) {
      return new Uint8Array(0);
    }

    const arrayBuf = toArrayBuffer(outPtr, 0, outSize);
    const resultCopy = new Uint8Array(arrayBuf.slice(0));
    symbols.Mp4Decrypt_FreeBuffer(outPtr);
    return resultCopy;
  }

  public static probe(target: string | Uint8Array): Mp4MediaInfo {
    const symbols = getFFI();
    const probeBuf = Buffer.alloc(4184);

    let ret: number;
    if (typeof target === "string") {
      const fileStr = Buffer.from(path.resolve(target) + "\0", "utf8");
      ret = symbols.Mp4Decrypt_ProbeFile(fileStr, ptr(probeBuf));
    } else {
      ret = symbols.Mp4Decrypt_ProbeMemory(ptr(target), BigInt(target.byteLength), ptr(probeBuf));
    }

    if (ret !== 0) {
      throw new Error(`Media probe failed (code ${ret})`);
    }

    return parseMediaInfoBuffer(probeBuf);
  }

  public static get apiVersion(): number {
    return getFFI().Mp4Decrypt_GetApiVersion();
  }

  public static get versionString(): string {
    const s = getFFI().Mp4Decrypt_GetVersionString();
    return s ? s.toString() : "";
  }

  public static getVersion(): Mp4VersionInfo {
    const symbols = getFFI();
    const raw = symbols.Mp4Decrypt_GetVersionJson();
    const parsed = JSON.parse(raw ? raw.toString() : "{}");
    return {
      major: parsed.major ?? 1,
      minor: parsed.minor ?? 0,
      patch: parsed.patch ?? 0,
      abiRevision: parsed.abiRevision ?? 1,
      bento4Version: parsed.bento4Version ?? "",
      buildDate: parsed.buildDate ?? "",
      versionString: Mp4DecryptSession.versionString,
    };
  }

  public static getVersionJson(): any {
    const s = getFFI().Mp4Decrypt_GetVersionJson();
    return JSON.parse(s ? s.toString() : "{}");
  }
}
