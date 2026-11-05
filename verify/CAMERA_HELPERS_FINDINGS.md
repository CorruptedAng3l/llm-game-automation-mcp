# Session 7 camera and hold helpers

`hold_until_change` uses one bounded native async key job. It validates the matching owner,
PID and generation-bound job acknowledgement, then watches hashes for one adopted-window,
window-lifetime and geometry scope. It cancels only that job. Native duration remains the
release bound if the MCP disconnects; an absent/mismatched cancellation ACK remains unconfirmed.
No key_down, global release, action retry, or native readiness relaxation is used.

Validation and before/after receipts are produced by `verify/run_holduntilchangetest.ps1`.
The model checks scope changes, attachment changes, malformed ACKs/status/cancellation,
capture errors, unchanged-region timeout, native completion, image options and absence of
global cleanup. The MCP check uses a stateful fake CLI. These establish orchestration and
protocol behavior, not real game consumption, rendering or a native hold release measurement.

MEASURED: TypeScript build succeeds. The final protocol model is 15 PASS / 0 FAIL;
actual MCP integration is 6 PASS / 0 FAIL (21/0 combined). The same final runner against
the pre-helper MCP server is 15 PASS / 1 FAIL: the pure new helper contract still runs,
while tools/list proves the old server lacks hold_until_change and sends no hold.
Evidence: `verify/_out/hold_final` and `verify/_out/hold_final_before`.

## TOOL ISSUES

| tool | what went wrong | consequence | root cause (or UNKNOWN) | fix applied? | proposed fix | repro |
|---|---|---|---|---|---|---|
| git worktree checkout | Historical evidence paths exceed Windows path length. | Initial checkout failed before index population. | Long private worktree prefix plus tracked nested verify evidence. | Yes, own checkout only. | Sparse checkout of source, tests and verify scripts; `core.longpaths` only on relevant commands. | Worktree creation at `_scratch/session7_agents/camera_hold`. |
| MCP test harness | Validation errors are textual MCP error results. | First integration-test invalid-input arm attempted JSON parsing. | Fixture assumed every result text was JSON. | Yes, fixture accepts textual validation failure. | Check isError and zero CLI calls independently of JSON parsing. | `t_hold_until_change_mcp.mjs` invalid timeout case. |
| find-image | Template observation had no source capture scope and changed the saved map. | A later hash could not prove the matched image's window identity. | Frame scope was not propagated to image matching replies. | Yes, source receipt plus opt-in preserve-map and CLI capability. | Require source-scope equality before camera motion; refuse old CLI before template capture. | `t_visual_camera.mjs` targetScope/oldCli/noPreserve cases. |
| camera helper / compact status | Manual user ownership was read only from an obsolete top-level field. | Current native cursor_guard.owner_active was missed by preflight and warning. | Native status schema moved the field into cursor_guard. | Yes, typed nested field with legacy compatibility. | Preserve matching schema coverage across native/adapter tests. | t_visual_camera nestedOwner/ownerAfterMove, t_hold_until_change nestedOwner, t_status nested native fields. |
| look_at | A settle capture exception erased the preceding acknowledged move receipt. | Failure response could hide submitted motion despite stopping further input. | Step was appended only after settle returned. | Yes, record exact move immediately before observation. | Retain receipt on later observation failure without replay. | t_visual_camera scopeAfterMove/captureThrowsAfterMove. |
| re_* | (none used) | — | — | — | — | Static and fake-target tests only. |

## Loaded launch diagnostics (AD4)

MEASURED: a typed native top-level injected:true plus matching inject.pid/inject.injected:true receipt now adds launched/keep_pid markers to a failed MCP launch, retains ok:false/readiness, and states liveness:not_verified. Requested claim TTL is exposed as claim_pending / claim_ttl_s_requested, meaning confirmation was not observed; no new claim or pad action occurs while launch is unready. Failed-before-load, mismatched, malformed and explicitly exited receipts get no usable-PID marker. Focused pure classification and actual MCP fake-target test is 24 PASS / 0 FAIL; same final runner against the pre-fix server is 22 PASS / 2 FAIL. Evidence verify/_out/launch_loaded_after and launch_loaded_before. This verifies diagnostics/forwarding only, without actual retail or kernel liveness claims.

## Scoped hold admission and uncertain acknowledgements

Hold admission now requires native input_scope_version and submits expected_capture_scope
with its bounded async key request. Exact job ACK/status/cancel receipts must retain
capture_scope_checked:true. This depends on the shared native scheduler scope gate:
the adapter alone cannot fence a target change between baseline capture and admission.
Manual user ownership refuses before submission. A thrown/lost/refused transport ACK is
conservatively reported as hold_may_still_be_active until an exact terminal job receipt;
only a typed ok:false,input_injected:false refusal proves no admission. No retry or global
release is added. The final model is 22/0 and actual MCP fake-target check is 7/0 (29/0);
same final model against the preceding helper is 14/8. Evidence:
verify/_out/hold_fenced/before_model.log and verify/_out/hold_fenced_final.

## Optional native relative-motion scope fence

Raw relative motion can now accept expected_capture_scope as an exact canonical native
scope JSON string. The opt-in path requires one time-paced step, validates a fresh window
lifetime/client geometry scope under g_inputMx, and rechecks the pointer scope before
publication. Mismatch returns ok:false,input_injected:false and increments the relative
scope refusal counter. Existing unscoped motion behavior is unchanged. Both-architecture
native scope-comparison model is 18/0; both production core objects compile with no
warnings. Evidence verify/_out/relative_scope. Native command dispatch and actual game
input consumption were not executed by this isolated agent.

## Visual calibration and bounded landmark correction

observe_settle reports exact scoped pixel stability while leaving raw queue drain and
camera angle unmeasured. look_calibrate uses two small signed raw probes and reidentifies
a unique static template to measure a local 2x2 pixel-per-count response; look_at uses
that matrix with per-step count bounds, settle checks and a new template observation
after every move. Missing/ambiguous/source-scope mismatch, user ownership, stale
calibration, nonfinite response, unstable region, no visual progress, deadline or step
bound stop motion. Unconfirmed motion retains its receipt and may-have-applied flag;
there is no action retry. Optional images share the action observation path even after
an error. Native find-image now exposes its capture scope/clock receipt and an opt-in
preserve-map mode; old CLI fails the capability preflight before template capture.
Focused model30/0 plus actual MCP fake-target10/0 =40/0. Final evidence
verify/_out/visual_camera_final/run_1b543d6eeadc404b9475be87e78e9b0a.
The same final runner with the pre-visual MCP adapter is30/1: the pure new contract
still runs while tools/list proves the preceding adapter lacks all three visual tools.
Both native CLI architectures build with zero warnings and local help advertises
image_scope_version1 (2/0), verify/_out/visual_capture_build_final/run_112228c5055b42d8a1b876c0632b0476.
The MCP suite started before the shared QPC dependency was imported passes all47
enumerated files, verify/_out/visual_camera/npm_test.log; the newly imported dependency's
own key-schedule test was outside that original enumeration and belongs to its own gate.
This is simulated image-response/protocol validation, not actual camera angles, raw
queue drain, game smoothing, target identity semantics or game aiming validation.

## Integrated-source review follow-up

Reviewed root59fc50b against native image-scope/map-preservation/QPC receipts. Old CLI
and DLL refusals remain before motion and the native scope contract matches. Corrected
camera preflight and compact status to read typed cursor_guard.owner_active (plus legacy
top-level compatibility), with read-only observation still allowed during manual control.
look_at now records each acknowledged native move before waiting for its subsequent
capture, preserving that receipt even when the capture fails or its scope changes. Added
all four actual helper names to the MCP Tools discovery table. Focused visual34/0 plus
actual MCP13/0 =47/0; same model against preceding helper30/4. Hold model23/0 plus
actual MCP8/0 =31/0; compact-status actual MCP22/0. Evidence visual_camera_review,
hold_review and visual_camera/status_review.log. No GUI or actual input was run.
