/**
 * mp4decrypt-api - One-Shot Buffer Decryption Demo (Bun)
 * =======================================================
 *
 * Demonstrates 1-line zero-setup in-memory buffer decryption and
 * verifies that decrypted media is plaintext.
 *
 * Run with:
 *   bun examples/bun/demo_buffer_decrypt.ts
 */

import { Mp4DecryptSession } from "../../sdk/bun/mp4decrypt_sdk";

async function main() {
  console.log("===================================================================");
  console.log("    MP4DECRYPT-API - ONE-SHOT BUFFER DECRYPTION DEMO (BUN)         ");
  console.log("===================================================================");

  const baseUrl = "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey";
  const kidHex = "9eb4050de44b4802932e27d75083e266";
  const keyHex = "166634c675823c235a4a9446fad52e4d";

  console.log("Fetching encrypted init.mp4 and 0001.m4s into RAM...");
  const [initResp, segResp] = await Promise.all([
    fetch(`${baseUrl}/15/init.mp4`),
    fetch(`${baseUrl}/15/0001.m4s`),
  ]);

  const initBytes = new Uint8Array(await initResp.arrayBuffer());
  const segBytes = new Uint8Array(await segResp.arrayBuffer());

  // Combine init + media fragment into a single playable encrypted fMP4 buffer
  const encrypted = new Uint8Array(initBytes.byteLength + segBytes.byteLength);
  encrypted.set(initBytes, 0);
  encrypted.set(segBytes, initBytes.byteLength);

  console.log(`Encrypted buffer size: ${(encrypted.byteLength / 1024).toFixed(1)} KB`);

  // Verify before decryption
  const preProbe = Mp4DecryptSession.probe(encrypted);
  console.log(`Pre-decryption status: Track encrypted = ${preProbe.tracks[0]?.isEncrypted}, Scheme = ${preProbe.tracks[0]?.schemeType}`);

  // One-shot decryption
  console.log("\nExecuting Mp4DecryptSession.decryptBuffer(buffer, kidHex, keyHex)...");
  const t0 = performance.now();
  const decrypted = Mp4DecryptSession.decryptBuffer(encrypted, kidHex, keyHex);
  const t1 = performance.now();

  console.log(`Decrypted buffer size: ${(decrypted.byteLength / 1024).toFixed(1)} KB in ${(t1 - t0).toFixed(2)} ms`);

  // Verify after decryption
  const postProbe = Mp4DecryptSession.probe(decrypted);
  console.log(`Post-decryption status: Track encrypted = ${postProbe.tracks[0]?.isEncrypted}`);
  console.log(`Decrypted codec: ${postProbe.tracks[0]?.codec}`);

  if (postProbe.tracks[0]?.isEncrypted === false) {
    console.log("\nSUCCESS: Media was successfully decrypted in memory!");
  } else {
    throw new Error("Decryption verification failed: media still marked encrypted.");
  }
}

main().catch(console.error);
