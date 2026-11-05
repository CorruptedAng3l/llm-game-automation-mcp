// Pure command construction. No game, clock, process or native input access.
export const MOVE_DIRECTIONS = ["up", "up-right", "right", "down-right", "down", "down-left", "left", "up-left"] as const;
type Direction = typeof MOVE_DIRECTIONS[number];
export function moveVector(direction: Direction, speed: "walk" | "run", rotationDeg = 0) {
  const signs: Record<Direction, readonly [number, number]> = {
    up: [0, 1], "up-right": [1, 1], right: [1, 0], "down-right": [1, -1],
    down: [0, -1], "down-left": [-1, -1], left: [-1, 0], "up-left": [-1, 1],
  };
  const [x, y] = signs[direction], magnitude = speed === "run" ? 32767 : 20000;
  const divisor = x && y ? Math.SQRT2 : 1;
  const radians = rotationDeg * Math.PI / 180, c = Math.cos(radians), s = Math.sin(radians);
  // Clockwise compensation: +90 maps intended screen-up to positive LX.
  return { lx: Math.round((x * c + y * s) * magnitude / divisor), ly: Math.round((-x * s + y * c) * magnitude / divisor), magnitude };
}
export function smoothMoveStates(vector: { lx: number; ly: number }, ms: number) {
  const rampMs = Math.min(120, Math.floor(ms / 3));
  const state = (at_ms: number, factor: number) => ({
    at_ms, buttons: "none", lx: Math.round(vector.lx * factor), ly: Math.round(vector.ly * factor), rx: 0, ry: 0, lt: 0, rt: 0,
  });
  const states = [];
  for (let i = 0; i <= 4; i++) states.push(state(Math.round(rampMs * i / 4), Math.sin(Math.PI * i / 8) ** 2));
  states.push(state(ms - rampMs, 1));
  for (let i = 1; i <= 4; i++) states.push(state(ms - rampMs + Math.round(rampMs * i / 4), i === 4 ? 0 : Math.cos(Math.PI * i / 8) ** 2));
  return { states, rampMs };
}

// Angular rate must come from a real caller calibration, not image pixels.
export function calibratedLook(degrees: number, calibration: { rx: number; degrees_per_second: number }) {
  const { rx, degrees_per_second: rate } = calibration;
  if (!Number.isFinite(degrees) || degrees === 0 || Math.abs(degrees) > 360) return { error: "degrees must be finite, nonzero and within -360..360" };
  if (!Number.isInteger(rx) || rx === 0 || rx < -32768 || rx > 32767 || !Number.isFinite(rate) || rate <= 0)
    return { error: "calibration requires nonzero XInput rx and a positive finite angular rate" };
  const rawMs = Math.abs(degrees) / rate * 1000;
  if (!Number.isFinite(rawMs) || rawMs < 1 || rawMs > 60000) return { error: "calibrated duration is outside 1..60000ms; use a different calibrated deflection/angle; nothing was sent" };
  const appliedRx = degrees > 0 ? rx : -rx;
  if (appliedRx < -32768 || appliedRx > 32767) return { error: "opposite calibrated rx is outside XInput range; recalibrate at a representable deflection; nothing was sent" };
  const ms = Math.round(rawMs), predicted = Math.sign(degrees) * rate * ms / 1000;
  return { rx: appliedRx, ms, raw_ms: rawMs, predicted_degrees: predicted };
}

// Requested stick-space trajectory; game prompt recognition is not measured.
export function rotateStick(stick: "left" | "right", startDeg: number, direction: "clockwise" | "counterclockwise", turns: number, ms: number, radius = 32767, endDwellMs = 50) {
  if ((stick !== "left" && stick !== "right") || (direction !== "clockwise" && direction !== "counterclockwise") || !Number.isFinite(startDeg) || Math.abs(startDeg) > 360 || !Number.isFinite(turns) || turns < 0.125 || turns > 4)
    return { error: "invalid stick/start angle/direction/turn count; nothing was sent" };
  if (!Number.isInteger(ms) || ms < 1 || ms > 60000 || !Number.isInteger(radius) || radius < 1 || radius > 32767 || !Number.isInteger(endDwellMs) || endDwellMs < 1 || endDwellMs > 1000)
    return { error: "rotation duration/radius/end dwell out of range; nothing was sent" };
  const segments = Math.ceil(24 * turns);
  if (segments + 1 > 256 || ms < segments) return { error: "rotation duration must give every point a distinct millisecond; nothing was sent" };
  const sweep = (direction === "clockwise" ? 1 : -1) * 360 * turns, states = [];
  for (let i = 0; i <= segments; i++) {
    const angle = (startDeg + sweep * i / segments) * Math.PI / 180;
    const state = { at_ms: Math.round(ms * i / segments), buttons: "none", lx: 0, ly: 0, rx: 0, ry: 0, lt: 0, rt: 0 };
    state[stick === "left" ? "lx" : "rx"] = Math.round(radius * Math.sin(angle));
    state[stick === "left" ? "ly" : "ry"] = Math.round(radius * Math.cos(angle));
    states.push(state);
  }
  return { states, segments, end_deg_unwrapped: startDeg + sweep, total_ms: ms + endDwellMs };
}

export interface DrivePhase { ms: number; throttle?: number; steer?: number; brake?: number }
/** Requested mapped controls only: LT may reverse in some games, not stop. */
export function driveStates(phases: DrivePhase[], tail?: boolean | { amount: number; ms: number }) {
  const byte = (v = 0) => Number.isInteger(v) && v >= 0 && v <= 255;
  if (!phases.length || phases.length > 16 || phases.some((p) => !Number.isInteger(p.ms) || p.ms < 1 || p.ms > 60000 ||
      !byte(p.throttle) || !byte(p.brake) || !Number.isInteger(p.steer ?? 0) || (p.steer ?? 0) < -32768 || (p.steer ?? 0) > 32767))
    return { error: "driving phases require bounded duration, byte triggers and signed16-bit steering; nothing was sent" };
  const brake = tail === true ? { amount: 255, ms: 250 } : tail || undefined;
  if (brake && (!byte(brake.amount) || !Number.isInteger(brake.ms) || brake.ms < 1 || brake.ms > 5000))
    return { error: "brake_after requires amount0..255 and duration1..5000ms; nothing was sent" };
  const total = phases.reduce((n, p) => n + p.ms, 0) + (brake?.ms ?? 0);
  if (total > 60000) return { error: "all driving phases plus brake_after must fit60000ms; nothing was sent" };
  const state = (at_ms: number, p: DrivePhase, phase: string) => ({ at_ms, phase, buttons: "none",
    lx: p.steer ?? 0, ly: 0, rx: 0, ry: 0, lt: p.brake ?? 0, rt: p.throttle ?? 0 });
  let at = 0;
  const states = phases.map((p, i) => { const s = state(at, p, `drive_${i + 1}`); at += p.ms; return s; });
  if (brake) { states.push(state(at, { ms: brake.ms, brake: brake.amount }, "brake_after")); at += brake.ms; }
  states.push(state(at, { ms: 0 }, "neutral"));
  return { states, total_ms: total, brake_after: brake ?? null };
}
