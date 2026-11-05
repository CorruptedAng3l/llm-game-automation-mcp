// Image helpers for what the model SEES: a labelled coordinate grid so it can read exact client coordinates off a screenshot.
// Everything here edits the RETURNED copy only; the PNG vinput saved is never touched.
import type { Image } from "./png.js";

/** How the returned image relates to the game's client area: image_px = (client_px - offset) * scale. */
export interface Mapping { scale: number; scale_y?: number; offset_x: number; offset_y: number }

/** Validate before any output allocation. Keep generated PNGs within decodePng's pixel limit and the native map limit. */
export function imageOutputSize(width: number, height: number): { width: number; height: number; bytes: number } {
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1 ||
      width > 32768 || height > 32768 || width * height > 64_000_000)
    throw new RangeError(`unsupported image output size ${width}x${height}; use at most 32768 per dimension and 64000000 pixels (reduce scale/zoom or set max_width)`);
  return { width, height, bytes: width * height * 4 };
}

// 3x5 digit glyphs, one string per row
const GLYPHS: Record<string, string[]> = {
  "0": ["111", "101", "101", "101", "111"], "1": ["010", "110", "010", "010", "111"], "2": ["111", "001", "111", "100", "111"],
  "3": ["111", "001", "111", "001", "111"], "4": ["101", "101", "111", "001", "001"], "5": ["111", "100", "111", "001", "111"],
  "6": ["111", "100", "111", "101", "111"], "7": ["111", "001", "001", "001", "001"], "8": ["111", "101", "111", "101", "111"],
  "9": ["111", "101", "111", "001", "111"],
};
const GLYPH_SCALE = 2;               // each glyph pixel is 2x2 image pixels
const GLYPH_W = 3 * GLYPH_SCALE, GLYPH_H = 5 * GLYPH_SCALE, GAP = GLYPH_SCALE;

function blend(img: Image, x: number, y: number, r: number, g: number, b: number, a: number) {
  if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
  const o = (y * img.width + x) * 4;
  img.data[o] = Math.round(img.data[o] * (1 - a) + r * a);
  img.data[o + 1] = Math.round(img.data[o + 1] * (1 - a) + g * a);
  img.data[o + 2] = Math.round(img.data[o + 2] * (1 - a) + b * a);
  img.data[o + 3] = 255;
}
function textWidth(t: string) { return t.length * GLYPH_W + (t.length - 1) * GAP; }
/** white digits on a black box, top-left at (x,y), clipped to the image */
function drawLabel(img: Image, x: number, y: number, t: string) {
  const w = textWidth(t) + 4, h = GLYPH_H + 4;
  for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) blend(img, x + xx, y + yy, 0, 0, 0, 0.8);
  let cx = x + 2;
  for (const ch of t) {
    const g = GLYPHS[ch];
    if (g) for (let r = 0; r < 5; r++) for (let c = 0; c < 3; c++) if (g[r][c] === "1")
      for (let dy = 0; dy < GLYPH_SCALE; dy++) for (let dx = 0; dx < GLYPH_SCALE; dx++) blend(img, cx + c * GLYPH_SCALE + dx, y + 2 + r * GLYPH_SCALE + dy, 255, 255, 255, 1);
    cx += GLYPH_W + GAP;
  }
}

/**
 * Draw a grid line at every multiple of `step` in CLIENT pixels (not image pixels), labelled with the client coordinate:
 * x values along the top edge, y values along the left edge. Returns a new image; the input is not modified.
 */
export function drawGrid(src: Image, m: Mapping, step: number): Image {
  const img: Image = { width: src.width, height: src.height, data: Buffer.from(src.data) };
  const clientX0 = m.offset_x, clientX1 = m.offset_x + img.width / m.scale;
  const clientY0 = m.offset_y, clientY1 = m.offset_y + img.height / (m.scale_y ?? m.scale);
  const vx: number[] = [], hy: number[] = [];
  for (let c = Math.ceil(clientX0 / step) * step; c < clientX1; c += step) vx.push(c);
  for (let c = Math.ceil(clientY0 / step) * step; c < clientY1; c += step) hy.push(c);
  const toX = (c: number) => Math.round((c - m.offset_x) * m.scale), toY = (c: number) => Math.round((c - m.offset_y) * (m.scale_y ?? m.scale));
  // lines: magenta, 55% so the picture stays readable; major lines (every 5th step) a little stronger
  for (const c of vx) { const x = toX(c), a = (c / step) % 5 === 0 ? 0.75 : 0.5; for (let y = 0; y < img.height; y++) blend(img, x, y, 255, 0, 255, a); }
  for (const c of hy) { const y = toY(c), a = (c / step) % 5 === 0 ? 0.75 : 0.5; for (let x = 0; x < img.width; x++) blend(img, x, y, 255, 0, 255, a); }
  // labels, thinned out when lines are closer together than a label is wide
  const every = (pitch: number, w: number) => Math.max(1, Math.ceil((w + 4) / Math.max(1, pitch)));
  const maxDigits = (arr: number[]) => Math.max(1, ...arr.map((v) => String(v).length));
  const ex = every(step * m.scale, textWidth("0".repeat(maxDigits(vx))) + 4);
  const ey = every(step * (m.scale_y ?? m.scale), GLYPH_H + 4 + 2);
  vx.forEach((c, i) => { if (i % ex === 0) drawLabel(img, toX(c) + 2, 0, String(c)); });
  hy.forEach((c, i) => { if (i % ey === 0) drawLabel(img, 0, toY(c) + 2, String(c)); });
  return img;
}

/**
 * Resize to newW x newH by exact area averaging (so downscaled text stays readable); when enlarging, each source pixel is
 * repeated (crisp, nothing invented). Returns a new image.
 */
export function resample(src: Image, newW: number, newH: number): Image {
  const size = imageOutputSize(newW, newH);
  if (newW === src.width && newH === src.height) return src;
  const out: Image = { width: newW, height: newH, data: Buffer.alloc(size.bytes) };
  const sx = src.width / newW, sy = src.height / newH;
  for (let y = 0; y < newH; y++) {
    const y0 = y * sy, y1 = (y + 1) * sy;
    for (let x = 0; x < newW; x++) {
      const x0 = x * sx, x1 = (x + 1) * sx;
      let r = 0, g = 0, b = 0, a = 0, wsum = 0;
      for (let yy = Math.floor(y0); yy < Math.min(src.height, Math.ceil(y1)); yy++) {
        const wy = Math.min(yy + 1, y1) - Math.max(yy, y0);
        for (let xx = Math.floor(x0); xx < Math.min(src.width, Math.ceil(x1)); xx++) {
          const w = wy * (Math.min(xx + 1, x1) - Math.max(xx, x0));
          const o = (yy * src.width + xx) * 4;
          r += src.data[o] * w; g += src.data[o + 1] * w; b += src.data[o + 2] * w; a += src.data[o + 3] * w; wsum += w;
        }
      }
      const o = (y * newW + x) * 4;
      out.data[o] = Math.round(r / wsum); out.data[o + 1] = Math.round(g / wsum); out.data[o + 2] = Math.round(b / wsum); out.data[o + 3] = Math.round(a / wsum);
    }
  }
  return out;
}

/** image pixel -> client pixel (rounded), the inverse of image_px = (client_px - offset) * scale */
export function toClient(m: Mapping, x: number, y: number): [number, number] {
  return [Math.round(m.offset_x + x / m.scale), Math.round(m.offset_y + y / (m.scale_y ?? m.scale))];
}
