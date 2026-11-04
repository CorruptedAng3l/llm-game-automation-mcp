// Attachment-only previews. Never modify the saved burst or capture-space map.
import { decodePng, encodePng } from "./png.js";
import { resample } from "./imgtools.js";

export function burstImage(original: Buffer, jpeg: boolean, budget: number) {
  const metadata: Record<string, unknown> = { attached: false, resized: false,
    original_bytes: original.length, budget_bytes: budget, capture_space_updated: false, attempts: 0 };
  const omit = (reason: string) => ({ metadata: { ...metadata, reason }, bytes: undefined });
  if (!Number.isSafeInteger(budget) || budget < 1) return omit("Invalid attachment byte budget; saved original retained.");
  if (original.length <= budget) return { metadata: { ...metadata, attached: true, preview_bytes: original.length }, bytes: original };
  if (jpeg) return omit("JPEG sheet exceeds the attachment byte budget; JPEG preview resizing is unsupported. Use fewer frames or a smaller max_width.");
  try {
    // Bound decode/resample allocations to the native sheet's16M-pixel limit,
    // before the codec's more general64M-pixel limit permits allocation.
    const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
    if (original.length < 33 || !original.subarray(0, 8).equals(signature) || original.toString("ascii", 12, 16) !== "IHDR")
      return omit("Oversized sheet is not a supported PNG; saved original retained.");
    const w = original.readUInt32BE(16), h = original.readUInt32BE(20);
    if (w < 1 || h < 1 || w > 8192 || h > 8192 || w * h > 16 * 1024 * 1024)
      return omit("PNG sheet exceeds bounded preview geometry; saved original retained.");
    const source = decodePng(original, { maxPixels: 16 * 1024 * 1024, maxDimension: 8192 });
    Object.assign(metadata, { original_width: source.width, original_height: source.height });
    let factor = Math.min(0.75, Math.sqrt(budget / original.length) * 0.8);
    let lastW = source.width, lastH = source.height;
    for (let n = 1; n <= 4; n++) {
      const width = Math.max(1, Math.floor(source.width * factor)), height = Math.max(1, Math.floor(source.height * factor));
      if (width === lastW && height === lastH) break;
      lastW = width; lastH = height; metadata.attempts = n;
      const bytes = encodePng(resample(source, width, height));
      if (bytes.length <= budget) return { metadata: { ...metadata, attached: true, resized: true, preview_bytes: bytes.length,
        preview_width: width, preview_height: height, scale_x: width / source.width, scale_y: height / source.height }, bytes };
      if (width === 1 && height === 1) break;
      factor *= 0.5;
    }
    return omit("PNG preview still exceeds the attachment byte budget after bounded resizing; saved original retained.");
  } catch (e) {
    return omit(`PNG preview could not be decoded/resized: ${(e as Error).message}; saved original retained.`);
  }
}
