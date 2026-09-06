/**
 * mp4decrypt-api - Media Probing Demo (Bun)
 * =========================================
 *
 * Demonstrates inspecting container format, video resolution, framerate,
 * audio channels, codecs, protection schemes, and default KIDs without
 * requiring decryption keys.
 *
 * Run with:
 *   bun examples/bun/demo_probe.ts
 */

import { Mp4DecryptSession } from "../../sdk/bun/mp4decrypt_sdk";
import fs from "node:fs";
import path from "node:path";

async function main() {
  console.log("===================================================================");
  console.log("         MP4DECRYPT-API - MEDIA PROBING DEMO (BUN)                 ");
  console.log("===================================================================");

  console.log(`API Version    : ${Mp4DecryptSession.apiVersion}`);
  console.log(`Version String : ${Mp4DecryptSession.versionString}`);

  const baseUrl = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey";
  console.log(`\nFetching test vector media from ${baseUrl}/15/init.mp4...`);

  const initResp = await fetch(`${baseUrl}/15/init.mp4`);
  if (!initResp.ok) {
    throw new Error(`Failed to download test vector: ${initResp.status} ${initResp.statusText}`);
  }
  const initBuf = new Uint8Array(await initResp.arrayBuffer());
  console.log(`Downloaded ${initBuf.byteLength} bytes.`);

  console.log("\n[1] Probing in-memory buffer with Mp4DecryptSession.probe(buffer)...");
  const info = Mp4DecryptSession.probe(initBuf);

  console.log("\nContainer Information:");
  console.log(`  Brands        : ${info.containerBrands}`);
  console.log(`  Duration      : ${info.durationSeconds.toFixed(2)}s`);
  console.log(`  Is Fragmented : ${info.isFragmented ? "Yes (fMP4)" : "No"}`);
  console.log(`  Track Count   : ${info.trackCount}`);

  console.log("\nTracks:");
  for (const track of info.tracks) {
    console.log(`  - Track #${track.trackId} [${track.streamType}]`);
    console.log(`      Handler Type  : ${track.handlerType}`);
    console.log(`      Codec         : ${track.codec}`);
    console.log(`      Encrypted     : ${track.isEncrypted ? "YES" : "NO"}`);
    if (track.isEncrypted) {
      console.log(`      Scheme Type   : ${track.schemeType}`);
      console.log(`      Default KID   : ${track.defaultKidHex}`);
    }
    if (track.streamType === "VIDEO") {
      console.log(`      Resolution    : ${track.width}x${track.height}`);
      console.log(`      Frame Rate    : ${track.frameRate.toFixed(2)} fps`);
    } else if (track.streamType === "AUDIO") {
      console.log(`      Channels      : ${track.audioChannels}`);
      console.log(`      Sample Rate   : ${track.sampleRate} Hz`);
    }
    console.log(`      Duration      : ${track.durationSeconds.toFixed(2)}s`);
  }

  // Also test probing from a file
  const tmpFile = path.join(import.meta.dir, "temp_init.mp4");
  fs.writeFileSync(tmpFile, initBuf);
  try {
    console.log(`\n[2] Probing from local file path with Mp4DecryptSession.probe("${tmpFile}")...`);
    const fileInfo = Mp4DecryptSession.probe(tmpFile);
    console.log(`  Verified file probe: ${fileInfo.trackCount} track(s), encrypted: ${fileInfo.tracks[0]?.isEncrypted}`);
  } finally {
    if (fs.existsSync(tmpFile)) fs.unlinkSync(tmpFile);
  }

  console.log("\nMedia probing demo completed successfully!");
}

main().catch(console.error);
