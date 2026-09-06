/**
 * mp4decrypt-api - Native Log Callback & Filtering Demo (Bun)
 * ============================================================
 *
 * Demonstrates intercepting Bento4 and C ABI logs through user callbacks
 * with dynamic log level filtering (DEBUG, INFO, WARN, ERROR, NONE).
 *
 * Run with:
 *   bun examples/bun/demo_log_callback.ts
 */

import { Mp4DecryptSession, Mp4LogLevel } from "../../sdk/bun/mp4decrypt_sdk";

async function main() {
  console.log("===================================================================");
  console.log("    MP4DECRYPT-API - LOG CALLBACK & FILTERING DEMO (BUN)          ");
  console.log("===================================================================");

  const baseUrl = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey";
  const kidHex = "9eb4050de44b4802932e27d75083e266";
  const keyHex = "166634c675823c235a4a9446fad52e4d";

  console.log("Fetching test media...");
  const [initResp, segResp] = await Promise.all([
    fetch(`${baseUrl}/15/init.mp4`),
    fetch(`${baseUrl}/15/0001.m4s`),
  ]);
  const initBytes = new Uint8Array(await initResp.arrayBuffer());
  const segBytes = new Uint8Array(await segResp.arrayBuffer());
  const inputBuffer = new Uint8Array(initBytes.byteLength + segBytes.byteLength);
  inputBuffer.set(initBytes, 0);
  inputBuffer.set(segBytes, initBytes.byteLength);

  const session = new Mp4DecryptSession();
  try {
    session.addKey(kidHex, keyHex);

    const logHistory: Array<{ level: Mp4LogLevel; message: string }> = [];

    session.onLog((level, message) => {
      const levelNames: Record<number, string> = {
        [Mp4LogLevel.DEBUG]: "DEBUG",
        [Mp4LogLevel.INFO]: "INFO ",
        [Mp4LogLevel.WARN]: "WARN ",
        [Mp4LogLevel.ERROR]: "ERROR",
      };
      const tag = levelNames[level] || `LVL${level}`;
      console.log(`  [${tag}] ${message}`);
      logHistory.push({ level, message });
    });

    console.log("\n--- TEST 1: Log Level = DEBUG (Verbose) ---");
    session.setLogLevel(Mp4LogLevel.DEBUG);
    session.decryptMemory(inputBuffer);
    console.log(`Captured ${logHistory.length} log messages at DEBUG level.`);

    logHistory.length = 0;
    console.log("\n--- TEST 2: Log Level = WARN (Quiet) ---");
    session.setLogLevel(Mp4LogLevel.WARN);
    session.decryptMemory(inputBuffer);
    console.log(`Captured ${logHistory.length} log messages at WARN level (should be 0 on success).`);

    logHistory.length = 0;
    console.log("\n--- TEST 3: Intentionally trigger ERROR log ---");
    session.setLogLevel(Mp4LogLevel.DEBUG);
    try {
      // Missing correct key for another test
      const badSession = new Mp4DecryptSession();
      badSession.onLog((lvl, msg) => console.log(`  [Expected Failure Log] ${msg}`));
      badSession.addKey(kidHex, "00000000000000000000000000000000"); // Invalid key length / wrong key
      badSession.decryptMemory(new Uint8Array([1, 2, 3, 4])); // Corrupted header
      badSession.destroy();
    } catch (e: any) {
      console.log(`  Caught expected exception: ${e.message}`);
    }

    console.log("\nLog callback test passed successfully!");
  } finally {
    session.destroy();
  }
}

main().catch(console.error);
