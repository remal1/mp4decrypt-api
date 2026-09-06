/**
 * mp4decrypt-api - Parallel In-Process Decryption Demo (Bun)
 * ==========================================================
 *
 * Demonstrates thread safety and isolated state: multiple concurrent
 * Mp4DecryptSession instances running simultaneously in the same process.
 *
 * Run with:
 *   bun examples/bun/demo_parallel.ts
 */

import { Mp4DecryptSession } from "../../sdk/bun/mp4decrypt_sdk";

async function main() {
  console.log("===================================================================");
  console.log("    MP4DECRYPT-API - CONCURRENT PARALLEL SESSIONS DEMO (BUN)       ");
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

  const CONCURRENCY = 8;
  console.log(`\nLaunching ${CONCURRENCY} concurrent decryptions in parallel...`);

  const startTime = performance.now();

  const tasks = Array.from({ length: CONCURRENCY }, async (_, idx) => {
    const session = new Mp4DecryptSession();
    try {
      session.addKey(kidHex, keyHex);
      const output = session.decryptMemory(inputBuffer);
      const stats = session.getStats();
      const probe = Mp4DecryptSession.probe(output);
      const ok = probe.tracks[0]?.isEncrypted === false;
      return { idx, outputSize: output.length, executionDurationMs: stats.executionDurationMs, ok };
    } finally {
      session.destroy();
    }
  });

  const results = await Promise.all(tasks);
  const totalElapsed = performance.now() - startTime;

  console.log("\nResults:");
  let allOk = true;
  for (const r of results) {
    console.log(`  Task #${r.idx}: ${r.outputSize} bytes decrypted in ${r.executionDurationMs.toFixed(2)} ms (plaintext: ${r.ok})`);
    if (!r.ok) allOk = false;
  }

  console.log(`\nTotal elapsed for ${CONCURRENCY} tasks: ${totalElapsed.toFixed(2)} ms`);
  if (allOk) {
    console.log("SUCCESS: All parallel decryption sessions completed with zero collisions!");
  } else {
    throw new Error("One or more sessions failed validation.");
  }
}

main().catch(console.error);
