/**
 * mp4decrypt-api - File Decryption Demo (Bun)
 * ============================================
 *
 * Demonstrates:
 * 1. File-based decryption with OOP `Mp4DecryptSession`.
 * 2. Progress reporting callback.
 * 3. Execution statistics (duration, bytes, MB/s throughput).
 *
 * Run with:
 *   bun examples/bun/demo_file_decrypt.ts
 */

import { Mp4DecryptSession } from "../../sdk/bun/mp4decrypt_sdk";
import fs from "node:fs";
import path from "node:path";

async function main() {
  console.log("===================================================================");
  console.log("       MP4DECRYPT-API - FILE DECRYPTION DEMO (BUN)                 ");
  console.log("===================================================================");

  const baseUrl = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey";
  const kidHex = "9eb4050de44b4802932e27d75083e266";
  const keyHex = "166634c675823c235a4a9446fad52e4d";

  const encFile = path.join(import.meta.dir, "test_encrypted.mp4");
  const decFile = path.join(import.meta.dir, "test_decrypted.mp4");

  console.log("Preparing encrypted sample file...");
  const [initResp, segResp] = await Promise.all([
    fetch(`${baseUrl}/15/init.mp4`),
    fetch(`${baseUrl}/15/0001.m4s`),
  ]);
  const initBytes = new Uint8Array(await initResp.arrayBuffer());
  const segBytes = new Uint8Array(await segResp.arrayBuffer());
  const combined = new Uint8Array(initBytes.byteLength + segBytes.byteLength);
  combined.set(initBytes, 0);
  combined.set(segBytes, initBytes.byteLength);
  fs.writeFileSync(encFile, combined);
  console.log(`Saved encrypted file to: ${encFile} (${(combined.byteLength / 1024).toFixed(1)} KB)`);

  const session = new Mp4DecryptSession();

  try {
    session.addKey(kidHex, keyHex);

    session.onProgress((step, total) => {
      const pct = total > 0 ? ((step / total) * 100).toFixed(1) : "0.0";
      process.stdout.write(`\rDecryption Progress: ${step}/${total} (${pct}%)`);
    });

    console.log("\nRunning session.decryptFile()...");
    session.decryptFile(encFile, decFile);
    console.log("\nDecryption finished successfully!");

    const stats = session.getStats();
    console.log("\nSession Performance Metrics:");
    console.log(`  Bytes Read   : ${stats.totalBytesRead} bytes`);
    console.log(`  Bytes Written: ${stats.totalBytesWritten} bytes`);
    console.log(`  Duration     : ${stats.executionDurationMs.toFixed(2)} ms`);
    console.log(`  Throughput   : ${stats.throughputMBps.toFixed(2)} MB/s`);

    // Verify output file
    const probeResult = Mp4DecryptSession.probe(decFile);
    console.log(`\nVerified output file: Encrypted = ${probeResult.tracks[0]?.isEncrypted}`);
  } finally {
    session.destroy();
    if (fs.existsSync(encFile)) fs.unlinkSync(encFile);
    if (fs.existsSync(decFile)) fs.unlinkSync(decFile);
  }

  console.log("\nFile decryption demo completed successfully!");
}

main().catch(console.error);
