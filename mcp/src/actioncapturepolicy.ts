// Explicit zero disables capture. Per-tool omitted defaults are applied by the
// caller before this shared action policy; they must never replace an explicit0.
export type CaptureAfter = boolean | number | 'immediate';
export function captureFrames(value: CaptureAfter | undefined): number | undefined {
  if (value === true) return 3;
  if (value === 'immediate') return 0;
  if (typeof value === 'number' && Number.isInteger(value) && value > 0 && value <= 3600) return value;
}
export const captureEnabled = (value: CaptureAfter | undefined): boolean => captureFrames(value) !== undefined;
