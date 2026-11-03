// Minimal PNG codec on node:zlib (no native deps). Decodes 8-bit grayscale / gray+alpha / RGB / RGBA, non-interlaced
// (what vinput writes: RGB or RGBA) to RGBA; anything else is a clear error. Encodes RGBA.
import { deflateSync, inflateSync } from "node:zlib";

export interface Image { width: number; height: number; /** RGBA, 4 bytes per pixel, row-major */ data: Buffer }

const SIG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function crc32(buf: Buffer, start = 0, end = buf.length): number {
  let c = 0xffffffff;
  for (let i = start; i < end; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export class PngError extends Error {}

export function decodePng(buf: Buffer): Image {
  if (buf.length < 33 || !buf.subarray(0, 8).equals(SIG)) throw new PngError("not a PNG file (bad signature)");
  let pos = 8, width = 0, height = 0, bitDepth = 0, colorType = -1, interlace = 0, sawEnd = false;
  const idat: Buffer[] = [];
  while (pos + 12 <= buf.length) {
    const len = buf.readUInt32BE(pos), type = buf.toString("latin1", pos + 4, pos + 8);
    const dataStart = pos + 8, dataEnd = dataStart + len;
    if (dataEnd + 4 > buf.length) throw new PngError(`truncated PNG (chunk ${type} runs past the end of the file)`);
    if (buf.readUInt32BE(dataEnd) !== crc32(buf, pos + 4, dataEnd)) throw new PngError(`corrupt PNG (CRC mismatch in chunk ${type})`);
    if (type === "IHDR") {
      width = buf.readUInt32BE(dataStart); height = buf.readUInt32BE(dataStart + 4);
      bitDepth = buf[dataStart + 8]; colorType = buf[dataStart + 9]; interlace = buf[dataStart + 12];
    } else if (type === "IDAT") idat.push(buf.subarray(dataStart, dataEnd));
    else if (type === "IEND") { sawEnd = true; break; }
    pos = dataEnd + 4;
  }
  if (colorType < 0) throw new PngError("corrupt PNG (no IHDR)");
  if (bitDepth !== 8) throw new PngError(`unsupported PNG: bit depth ${bitDepth} (only 8-bit is supported)`);
  if (interlace !== 0) throw new PngError("unsupported PNG: interlaced (only non-interlaced is supported)");
  const channels = ({ 0: 1, 2: 3, 4: 2, 6: 4 } as Record<number, number>)[colorType];
  if (!channels) throw new PngError(`unsupported PNG: color type ${colorType} (only grayscale, gray+alpha, RGB and RGBA are supported)`);
  if (!sawEnd || !idat.length) throw new PngError("truncated PNG (no image data / IEND)");
  if (!width || !height || width * height > 64_000_000) throw new PngError(`unsupported PNG size ${width}x${height}`);
  const raw = inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  if (raw.length < (stride + 1) * height) throw new PngError("corrupt PNG (image data shorter than the header says)");
  const out = Buffer.alloc(width * height * 4);
  let prev = Buffer.alloc(stride);
  for (let y = 0; y < height; y++) {
    const ft = raw[y * (stride + 1)];
    const cur = Buffer.from(raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
    for (let i = 0; i < stride; i++) {
      const a = i >= channels ? cur[i - channels] : 0, b = prev[i], c = i >= channels ? prev[i - channels] : 0;
      let add: number;
      switch (ft) {
        case 0: add = 0; break;
        case 1: add = a; break;
        case 2: add = b; break;
        case 3: add = (a + b) >> 1; break;
        case 4: { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); add = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; break; }
        default: throw new PngError(`corrupt PNG (unknown filter type ${ft})`);
      }
      cur[i] = (cur[i] + add) & 0xff;
    }
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4, s = x * channels;
      if (channels === 1) { out[o] = out[o + 1] = out[o + 2] = cur[s]; out[o + 3] = 255; }
      else if (channels === 2) { out[o] = out[o + 1] = out[o + 2] = cur[s]; out[o + 3] = cur[s + 1]; }
      else { out[o] = cur[s]; out[o + 1] = cur[s + 1]; out[o + 2] = cur[s + 2]; out[o + 3] = channels === 4 ? cur[s + 3] : 255; }
    }
    prev = cur;
  }
  return { width, height, data: out };
}

function chunk(type: string, data: Buffer): Buffer {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0); out.write(type, 4, "latin1"); data.copy(out, 8);
  out.writeUInt32BE(crc32(out, 4, 8 + data.length), 8 + data.length);
  return out;
}

/** Encode RGBA to an 8-bit RGB PNG (alpha dropped when all 255) or RGBA PNG. Rows use filter "Sub". */
export function encodePng(img: Image): Buffer {
  const { width, height, data } = img;
  if (data.length !== width * height * 4) throw new PngError("encodePng: data length does not match width*height*4");
  let opaque = true;
  for (let i = 3; i < data.length; i += 4) if (data[i] !== 255) { opaque = false; break; }
  const ch = opaque ? 3 : 4, stride = width * ch;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y++) {
    const row = y * (stride + 1);
    raw[row] = 1;
    for (let x = 0; x < width; x++) for (let c = 0; c < ch; c++) {
      const v = data[(y * width + x) * 4 + c], left = x > 0 ? data[(y * width + x - 1) * 4 + c] : 0;
      raw[row + 1 + x * ch + c] = (v - left) & 0xff;
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = opaque ? 2 : 6;
  return Buffer.concat([SIG, chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw, { level: 6 })), chunk("IEND", Buffer.alloc(0))]);
}
