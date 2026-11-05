// Optional MCP presentation. Native receipts and handler decisions stay untouched.
type Obj = Record<string, unknown>;
type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };
type Reply = { content: Content[]; isError?: boolean };
const object = (v: unknown): Obj | undefined => v !== null && typeof v === "object" && !Array.isArray(v) ? v as Obj : undefined;
const u64 = (v: unknown): bigint | undefined => typeof v === "string" && /^(0|[1-9][0-9]{0,19})$/.test(v) && BigInt(v) <= 18446744073709551615n ? BigInt(v) : undefined;

export function compactEnabled(option: unknown, env = process.env.VINPUT_MCP_COMPACT): boolean {
  return typeof option === "boolean" ? option : /^(1|true|yes|on)$/i.test(env?.trim() ?? "");
}

// Keep the fields needed to continue an action or interpret its observation.
// Warnings/notes/refusals are collected separately at every depth, including new
// diagnostics a future handler adds without knowing about this serializer.
const fields = new Set((
  "ok pid job_id job_kind job_state job_reason job_complete hold_ready accepted input_injected input_ok " +
  "input_injection_unknown delivered completed injected transitions completed_injected_points game_accepted_points " +
  "reader_return_observed application_response window_messages_dispatched di_poll_channel di_poll_mode di_poll_confirmed " +
  "di_release_confirmed di_press_release_confirmed position_poll_channel position_poll_confirmed button_released " +
  "path saved w h width height method capture_path frame image mapping mapping_used scale scale_y offset_x offset_y " +
  "source_w source_h origin_x origin_y image_w image_h client_w client_h region_client_w region_client_h format mimeType bytes " +
  "requested_region region_coordinate_system coordinate_system capture_mapping_persisted capture_after_ok " +
  "x y r g b hex key vk button down text found matched gone skipped changed stable settled timed_out clicked dry_run click candidates " +
  "line_count lang ocr_scale ocr_pass ocr_passes ocr_confidence text_only digits_only box centre center match_count match_score words " +
  "hash previous_hash score confidence threshold index count total completed cancelled released outcome aborted " +
  "requested_points requested_count completed_points processed_points skipped_points next_index uncertain_index verify_policy unchanged_limit unchanged_streak stopped_reason stop_reason reason samples elapsed_ms " +
  "stopped hold_started hold_may_still_be_active motion_score pixel_motion max_change_pct pixel_tolerance changed_pct mean_abs_rgb_diff_pct " +
  "grace_ms still_ms comparisons max_step_changed_pct quiet_window_ms admission_attempted admission_explicitly_refused hold_input " +
  "input_motion region metric measurement measured pixel_change_observed click_attempted hover_confirmed probe_region matched_reject_text " +
  "id kind native_batch timeout_ms requested client confirmed delivery_confirmed placement_confirmed confirmation_scope do_not_repeat_delivered_points " +
  "receipt region_diff before after before_hash after_hash baseline_observation final_observation initial_observation " +
  "baseline_samples baseline_changed_samples baseline_ms min_change_pct changes last_change_ms stable_window_ms stability_metric sample " +
  "capture_qpc_ticks frame_qpc_ticks frame_index timing_clock qpc_frequency capture_map_id " +
  "target_exited exit_code injected followed_from process_created launch_state waiting_for launcher_pid follow_pid follow_created " +
  "foreground_restored really_foreground app_active frames frame_api cursor raw msgs virtual_down " +
  "input_session displacement angle_degrees yaw_degrees measured_angle_degrees verification " +
  "relative_accounting_version requested_dx requested_dy device_dx device_dy applied_dx applied_dy raw_dx raw_dy steps_published cursor_clamped cursor_saturated relative_delivery " +
  "calibration probe_counts_per_axis initial_target final_target degrees_per_count target desired centered error_px cursor_aligned " +
  "requested_yaw_deg requested_pitch_deg submitted_dx submitted_dy counts_per_degree pitch_counts_per_degree calibration_source actual_yaw_deg actual_pitch_deg ms " +
  "text_input_observation backend directinput_keyboard_reader_observed evidence mapped " +
  "settle_ms_requested settle_ms_elapsed action_state release_confirmed attempted timing_profile_used box_stability_ms_requested box_stability_ms_observed box_stability_samples box_stability_ready " +
  "capture_after capture job jobs final_job sequence_input sequence_wait sequence_captures input_receipt cleanup capture_scope capture_mapping capture_observation framework_input number_read observation_frame_path observation_frame_error observation_frame_saved settle_ok settle_observation capture_stability attachment next_action readiness_state " +
  "sequence_start sequence_guard sequence_steps sequence_step sequence_line sequence_waiting_for_present anchor_source " +
  "present_anchor_target_index anchor_present_hook_index present_anchor_timeout_ms present_anchor_scope native_deadlines_anchor " +
  "observed observations matched_observation matched_frame_path matched_frame_available match_frame_requested_path match_frame_saved observer_armed " +
  "step_index_before_observation step_index_at_abort abort_confirmed start_event observed_points observation tooltip_ocr " +
  "context_region context_text context_ocr context timing timestamp_ms t_ms time_ms delta_ms rectangle rect " +
  "image_match matched_frame matched_text matches match results points receipts steps output " +
  "targets keys lines config status mode warnings notes note"
).split(/\s+/));
const semanticQueries = new Set(["list_targets", "list_keys", "config", "doctor", "log", "trace"]);
const opaqueFields = new Set(["capture_scope", "capture_mapping", "capture_observation", "framework_input", "number_read", "settle_observation", "capture_stability", "relative_delivery", "timing_profile_used", "image", "mapping", "mapping_used", "mapped", "attachment", "next_action", "calibration", "context", "sequence_start", "sequence_captures", "matched_observation"]);
const batchTools = new Set(["click_many", "click_grid", "hover_many"]);
const diagnosticKey = /warning|error|refus|fail|not_measured|timed_out|(?:^|_)(?:notes?|hint)$/i;
const active = (v: unknown): boolean => v !== undefined && v !== null && v !== false && v !== "" && v !== 0 && (!Array.isArray(v) || v.length > 0);

function diagnostics(value: unknown): unknown {
  if (Array.isArray(value)) {
    const children = value.map(diagnostics);
    return children.some(v => v !== undefined) ? children.map(v => v ?? null) : undefined;
  }
  const o = object(value);
  if (!o) return;
  const result: Obj = {};
  for (const [k, v] of Object.entries(o)) {
    if (diagnosticKey.test(k) && active(v)) result[k] = v;
    else {
      const child = diagnostics(v);
      if (child !== undefined) result[k] = child;
    }
  }
  return Object.keys(result).length ? result : undefined;
}

function reader(value: unknown): Obj | undefined {
  const o = object(value);
  if (!o) return;
  // Pending, ambiguous, or unmeasured counters must never become named readers.
  const measured = o.version === 1 && o.measured === true && o.started === true && o.terminal === true && o.frozen === true;
  const result: Obj = { outcome: measured || o.outcome === "pending" || o.outcome === "not_applicable" ? o.outcome : "unavailable", measurement:"polling_returns_only", application_response: null };
  if (o.kind === "pad") {
    const count = u64(o.pad_returns);
    result.channels = measured && count !== undefined && count > 0n ? ["XInput"] : [];
    if (measured && count !== undefined) result.reader_return_observed = count > 0n;
  } else if (Array.isArray(o.keys)) {
    result.keys = o.keys.map(value => {
      const k = object(value) ?? {}, out: Obj = { vk: k.vk };
      const high = u64(k.down_returns), reads = u64(k.poll_reads), messages = u64(k.message_dispatches);
      const keyMeasured = measured && k.measured === true && high !== undefined && reads !== undefined && high <= reads;
      if (keyMeasured) out.reader_return_observed = high > 0n;
      if (messages !== undefined) out.window_messages_dispatched = messages > 0n;
      const channels = object(k.channels);
      let total = 0n, valid = !!channels;
      const names: string[] = [];
      for (const [name, value] of Object.entries(channels ?? {})) {
        const c = object(value), count = u64(c?.down_returns), reads = u64(c?.reads);
        if (c?.measured !== true || count === undefined || reads === undefined || count > reads) { valid = false; continue; }
        total += count;
        if (count > 0n) names.push(name);
      }
      if (keyMeasured && valid && total === high) out.channels = names;
      else if (keyMeasured && high > 0n) out.channels_unknown = true;
      if (k.modifier_alias_ambiguous === true) out.modifier_alias_ambiguous = true;
      return out;
    });
  }
  return result;
}

function compactValue(value: unknown,retainError=true): unknown {
  if (Array.isArray(value)) return value.map(v=>compactValue(v));
  const o = object(value);
  if (!o) return value;
  const result: Obj = {};
  for (const [k, v] of Object.entries(o)) if (fields.has(k) || /_measured$|_receipt$|(?:^|_)qpc_ticks$/.test(k)) result[k] = opaqueFields.has(k) ? v : compactValue(v);
  const read = reader(o.input_read_observation);
  if (read) result.input_read = read;
  // A failed nested receipt can contain essential diagnostics outside the short
  // field list. Keep that receipt whole, like a top-level error.
  if (o.ok === false&&retainError) return o;
  const warnings = object(diagnostics(o));
  if (warnings) for (const [k, v] of Object.entries(warnings))
    if (!Object.hasOwn(result, k)) result[k] = v;
  return result;
}

function observedFrame(value:unknown,sharedScope:boolean):unknown{
 const o=object(value);if(!o)return value;
 const out:Obj={};for(const key of ['capture_qpc_ticks','frame_qpc_ticks','frame_index','captured_present','qpc_frequency','timing_clock'])if(o[key]!==undefined)out[key]=o[key];
 if(!sharedScope&&o.capture_scope!==undefined)out.capture_scope=o.capture_scope;
 return {...out,...object(diagnostics(o))};
}
function batchPoint(value:unknown,hover:boolean,sharedScope:boolean):unknown{
 const p=object(value);if(!p)return value;
 if(hover){
  const client=object(p.client),ocr=object(p.tooltip_ocr),out:Obj={index:p.index,...(client?{x:client.x,y:client.y}:{}),...(p.probe_region!==undefined?{probe_region:p.probe_region}:{}),...(typeof ocr?.text==='string'?{text:ocr.text}:{}),...(p.reason!==undefined?{reason:p.reason}:{}),...object(diagnostics(p))};
  const observed=object(observedFrame(p.observation,sharedScope));if(observed)Object.assign(out,observed);
  if(ocr){out.ocr_capture_qpc_ticks=ocr.capture_qpc_ticks??null;if(!sharedScope&&ocr.capture_scope!==undefined)out.capture_scope=ocr.capture_scope;}
  if(p.move_receipt!==undefined&&p.hover_confirmed!==true)out.move_receipt=p.move_receipt;
  return out;
 }
 const out=compactValue(p) as Obj;
 if(p.receipt!==undefined&&(p.delivery_confirmed!==true||p.confirmed===false))out.receipt=p.receipt;
 if(p.qualified_receipt!==undefined&&(p.delivery_confirmed!==true||p.confirmed===false))out.qualified_receipt=p.qualified_receipt;
 if(p.move_receipt!==undefined&&p.hover_confirmed!==true)out.move_receipt=p.move_receipt;
 const diff=object(p.region_diff);if(diff){out.changed=typeof p.changed==='boolean'?p.changed:typeof diff.changed==='boolean'?diff.changed:null;out.region_diff={...(compactValue(diff) as Obj),before:observedFrame(diff.before,sharedScope),after:observedFrame(diff.after,sharedScope)};}
 const ocr=object(p.tooltip_ocr);if(ocr){out.tooltip_ocr={text:ocr.text,...object(observedFrame(ocr,sharedScope)),...object(diagnostics(ocr))};}
 const observation=object(p.observation);if(observation)out.observation=observedFrame(observation,sharedScope);
 return out;
}
function compactBatch(value:Obj,tool:string):Obj{
 const out=compactValue(value,false) as Obj,sharedScope=!!object(value.capture_scope);
 if(value.failure_receipt!==undefined)out.failure_receipt=value.failure_receipt;
 if(Array.isArray(value.points))out.points=value.points.map(p=>batchPoint(p,tool==='hover_many',sharedScope));
 return out;
}

/** Compact only the leading JSON text; image bytes, stderr and other texts are
 * retained verbatim. Errors retain their original diagnostic contract. */
export function compactReply<T extends Reply>(reply: T, enabled: boolean, tool: string): T {
  if (!enabled || reply.isError&&!batchTools.has(tool) || semanticQueries.has(tool)) return reply;
  const first = reply.content[0];
  if (first?.type !== "text") return reply;
  let original: unknown;
  try { original = JSON.parse(first.text); } catch { return reply; }
  const json = object(original);
  if (!json || json.ok === false&&!batchTools.has(tool)) return reply;
  const compact = batchTools.has(tool)?compactBatch(json,tool):compactValue(json) as Obj;
  return { ...reply, content: [{ type: "text", text: JSON.stringify({ ...compact, compact: true }) }, ...reply.content.slice(1)] };
}
