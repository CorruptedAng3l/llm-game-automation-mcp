// The native target recognizes its current owned window at command time. These
// flags mark omitted caller values; an old DLL keeps the existing defaults.
export function frameworkClickTiming(hold: number | undefined, hover: number | undefined, positioned: boolean) {
  return { hold: hold ?? 140, hover: hover ?? 80, budgetHold: hold ?? 400, budgetHover: positioned ? hover ?? 350 : 0,
    native: { ...(hold === undefined ? { framework_default_hold: true } : {}),
      ...(positioned && hover === undefined ? { framework_default_hover: true } : {}) },
    argv: [...(hold === undefined ? ['--framework_default_hold=true'] : []),
      ...(positioned && hover === undefined ? ['--framework_default_hover=true'] : [])] };
}
