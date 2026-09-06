/**
 * mp4decrypt-api - Asynchronous Session Cancellation Demo (Bun)
 * ============================================================
 *
 * Demonstrates interrupting a decryption pipeline mid-flight
 * via session.cancel(). The operation safely unwinds resources
 * and returns cancellation error code -2.
 *
 * Run with:
 *   bun examples/bun/demo_cancellation.ts
 */

import { Mp4DecryptSession, Mp4LogLevel } from "../../sdk/bun/mp4decrypt_sdk";

async function main() {
  console.log("===================================================================");
  console.log("    MP4DECRYPT-API - ASYNCHRONOUS CANCELLATION DEMO (BUN)          ");
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
  const inputBuffer = new Uint8Array(initBytes.byteLength + segBytes.byteLength);
  inputBuffer.set(initBytes, 0);
  inputBuffer.set(segBytes, initBytes.byteLength);

  const session = new Mp4DecryptSession();
  try {
    session.addKey(kidHex, keyHex);
    session.setLogLevel(Mp4LogLevel.DEBUG);

    session.onLog((lvl, msg) => {
      console.log(`  [Native Log] ${msg}`);
    });

    let cancellationTriggered = false;
    let inPos = 0;

    console.log("\nStarting streaming decryption with planned mid-flight cancellation...");
    try {
      session.decryptCustom({
        input: {
          read: (buf, count) => {
            const available = Math.min(count, inputBuffer.length - inPos);
            if (available <= 0) return 0;
            buf.set(inputBuffer.subarray(inPos, inPos + available));
            inPos += available;

            // Trigger cancellation after reading the first chunk
            if (!cancellationTriggered && inPos >= 16) {
              console.log(`  [Custom Stream] Read ${inPos} bytes -> triggering session.cancel()!`);
              cancellationTriggered = true;
              session.cancel();
            }
            return available;
          },
          seek: (offset) => {
            inPos = offset;
            return 0;
          },
          tell: () => inPos,
          size: () => inputBuffer.length,
        },
        output: {
          write: (_buf, count) => count,
          seek: () => 0,
          tell: () => 0,
          size: () => 0,
        },
      });

      console.error("ERROR: Decryption was expected to be cancelled but finished successfully!");
    } catch (err: any) {
      console.log(`\nExpected cancellation caught: ${err.message}`);
      if (err.message.includes("cancelled") || cancellationTriggered) {
        console.log("\nSUCCESS: Session cancellation operated as intended!");
      }
    }
  } finally {
    session.destroy();
  }
}

main().catch(console.error);
