/**
 * mp4decrypt-api - Custom Streaming / Callback I/O Demo (Bun)
 * ============================================================
 *
 * Demonstrates Paradigm 3: Using user-defined C callbacks (wrapped in
 * TypeScript) to decrypt directly between custom stream interfaces
 * without using the filesystem or static buffers.
 *
 * Run with:
 *   bun examples/bun/demo_streaming.ts
 */

import { Mp4DecryptSession, Mp4LogLevel } from "../../sdk/bun/mp4decrypt_sdk";

async function main() {
  console.log("===================================================================");
  console.log("    MP4DECRYPT-API - CUSTOM STREAMING CALLBACK I/O DEMO (BUN)      ");
  console.log("===================================================================");

  const baseUrl = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey";
  const kidHex = "9eb4050de44b4802932e27d75083e266";
  const keyHex = "166634c675823c235a4a9446fad52e4d";

  console.log("Fetching encrypted media chunks...");
  const [initResp, segResp] = await Promise.all([
    fetch(`${baseUrl}/15/init.mp4`),
    fetch(`${baseUrl}/15/0001.m4s`),
  ]);
  const initBytes = new Uint8Array(await initResp.arrayBuffer());
  const segBytes = new Uint8Array(await segResp.arrayBuffer());
  const inputData = new Uint8Array(initBytes.byteLength + segBytes.byteLength);
  inputData.set(initBytes, 0);
  inputData.set(segBytes, initBytes.byteLength);

  // In-memory virtual seekable input stream
  let inPos = 0;

  // In-memory virtual seekable output buffer
  let outCapacity = 64 * 1024;
  let outBuffer = new Uint8Array(outCapacity);
  let outPos = 0;
  let outSize = 0;

  const session = new Mp4DecryptSession();
  try {
    session.addKey(kidHex, keyHex);
    session.setLogLevel(Mp4LogLevel.DEBUG);
    session.onLog((level, msg) => {
      console.log(`  [Native Log ${level}] ${msg}`);
    });

    console.log("\nStarting streaming decryption via decryptCustom()...");
    session.decryptCustom({
      input: {
        read: (buf, count) => {
          const available = Math.min(count, inputData.length - inPos);
          if (available <= 0) return 0;
          buf.set(inputData.subarray(inPos, inPos + available));
          inPos += available;
          return available;
        },
        seek: (offset) => {
          if (offset > inputData.length) return -1;
          inPos = offset;
          return 0;
        },
        tell: () => inPos,
        size: () => inputData.length,
      },
      output: {
        write: (buf, count) => {
          if (outPos + count > outCapacity) {
            outCapacity = Math.max(outCapacity * 2, outPos + count + 4096);
            const newBuf = new Uint8Array(outCapacity);
            newBuf.set(outBuffer);
            outBuffer = newBuf;
          }
          outBuffer.set(buf.subarray(0, count), outPos);
          outPos += count;
          if (outPos > outSize) outSize = outPos;
          return count;
        },
        seek: (offset) => {
          outPos = offset;
          if (outPos > outSize) outSize = outPos;
          return 0;
        },
        tell: () => outPos,
        size: () => outSize,
      },
    });

    const decryptedOutput = outBuffer.subarray(0, outSize);
    console.log(`\nStreamed decrypted output size: ${(decryptedOutput.length / 1024).toFixed(1)} KB`);

    const probeInfo = Mp4DecryptSession.probe(decryptedOutput);
    console.log(`Verification: Track encrypted = ${probeInfo.tracks[0]?.isEncrypted}`);

    if (probeInfo.tracks[0]?.isEncrypted === false) {
      console.log("\nSUCCESS: Streaming callback I/O decrypted media successfully!");
    }
  } finally {
    session.destroy();
  }
}

main().catch(console.error);
