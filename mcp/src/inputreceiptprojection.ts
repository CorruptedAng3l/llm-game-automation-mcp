type Obj = Record<string, unknown>;
const object = (value: unknown): Obj | undefined => value && typeof value === 'object' && !Array.isArray(value) ? value as Obj : undefined;
const maximum = 18446744073709551615n;
function count(value: unknown): bigint | undefined {
  if (typeof value !== 'string' || !/^(0|[1-9][0-9]{0,19})$/.test(value)) return;
  const parsed = BigInt(value); return parsed <= maximum ? parsed : undefined;
}
const pollNames = ['GetAsyncKeyState', 'GetKeyState', 'GetKeyboardState', 'DirectInputState', 'DirectInputBuffer', 'GetRawInputData', 'GetRawInputBuffer'];
const dispatchName = 'WindowMessageDispatch';
function sameNames(value: unknown, expected: string[]): boolean {
  return Array.isArray(value) && value.length === expected.length && new Set(value).size === value.length && value.every(name => typeof name === 'string' && expected.includes(name));
}
const outcome = (seen: number, requested: number): string => seen === requested ? 'observed' : seen ? 'partial' : 'not_observed';

/** Version2 observations name dispatch separately from returned polling state.
 * No projection interprets either as application consumption or game response. */
export function projectInputReadObservation(value: unknown): Obj | undefined {
  const o = object(value); if (!o || o.version !== 2) return;
  const unavailable = (): Obj => ({outcome: 'unavailable', measurement: 'poll_returns_or_window_message_dispatch', application_response: null});
  if (o.started !== true || o.terminal !== true || o.frozen !== true)
    return {outcome: o.outcome === 'pending' ? 'pending' : 'unavailable', measurement: 'poll_returns_or_window_message_dispatch', application_response: null};
  if (o.application_response !== null || typeof o.applicable !== 'boolean' || typeof o.measured !== 'boolean') return unavailable();
  if (o.applicable === false) return o.outcome === 'not_applicable' ? {outcome: 'not_applicable', application_response: null} : unavailable();
  if (!Array.isArray(o.keys)) return unavailable();
  if (o.kind === 'pad') {
    if (!o.measured) return unavailable();
    const returns = count(o.pad_returns); if (returns === undefined || o.keys.length !== 0 || o.keys_requested !== 0) return unavailable();
    const observed = returns > 0n, names = observed ? ['XInput'] : [], expected = observed ? 'observed' : 'not_observed';
    if (o.outcome !== expected || o.polling_outcome !== expected || o.polling_measured !== true || o.all_requested_observed !== observed || o.all_requested_polling_observed !== observed || !sameNames(o.observed_channels, names)) return unavailable();
    return {outcome: expected, polling_outcome: expected, measurement: 'job_owned_pad_returns', application_response: null, input_observed: observed, reader_return_observed: observed, channels: names, polling_channels: names};
  }
  if (o.kind !== 'keys' || !Number.isInteger(o.keys_requested) || o.keys_requested !== o.keys.length || !o.keys.length || o.keys.length > 256) return unavailable();
  let observed = 0, polled = 0, dispatched = 0, allKeysMeasured = true, allPollMeasured = true, allMessagesMeasured = true;
  const keys = new Set<number>(), pollChannels = new Set<string>(), channels = new Set<string>(), projected: Obj[] = [];
  for (const value of o.keys) {
    const k = object(value);
    if (!k || typeof k.vk !== 'number' || !Number.isInteger(k.vk) || k.vk < 1 || k.vk > 255 || keys.has(k.vk) || typeof k.measured !== 'boolean' || typeof k.polling_measured !== 'boolean' || typeof k.messages_measured !== 'boolean') return unavailable();
    keys.add(k.vk);
    const reads = count(k.poll_reads), high = count(k.down_returns), messages = count(k.message_dispatches), stats = object(k.channels);
    const pollAvailable = reads !== undefined && high !== undefined && high <= reads;
    if (!stats || Object.keys(stats).length !== pollNames.length || (!pollAvailable && (k.poll_reads !== null || k.down_returns !== null))) return unavailable();
    let summedReads = 0n, summedHigh = 0n, countersComplete = true; const names: string[] = [];
    for (const name of pollNames) {
      const c = object(stats[name]); if (!c) return unavailable();
      const r = count(c.reads), d = count(c.down_returns);
      if (c.measured === true) {
        if (r === undefined || d === undefined || d > r) return unavailable();
        summedReads += r; summedHigh += d;
        if (d > 0n) names.push(name);
      } else if (c.measured === false && c.reads === null && c.down_returns === null) countersComplete = false;
      else return unavailable();
    }
    countersComplete &&= summedReads <= maximum && summedHigh <= maximum;
    if (pollAvailable !== countersComplete || (pollAvailable && (summedReads !== reads || summedHigh !== high))) return unavailable();
    if (k.messages_measured ? messages === undefined : k.message_dispatches !== null) return unavailable();
    const pollObserved = summedHigh > 0n, messageObserved = k.messages_measured && messages !== undefined && messages > 0n;
    if ((k.polling_measured && !pollObserved && !countersComplete) || (pollObserved && !k.polling_measured) || k.measured !== (pollObserved || messageObserved || (k.polling_measured && k.messages_measured))) return unavailable();
    if (pollObserved) ++polled; if (messageObserved) ++dispatched; if (pollObserved || messageObserved) ++observed;
    allKeysMeasured &&= k.measured; allPollMeasured &&= k.polling_measured; allMessagesMeasured &&= k.messages_measured;
    for (const name of names) {pollChannels.add(name); channels.add(name);}
    if (messageObserved) {names.push(dispatchName); channels.add(dispatchName);}
    if (!sameNames(k.observed_channels, names)) return unavailable();
    projected.push({vk: k.vk, channels: names, polling_channels: names.filter(name => name !== dispatchName), reader_return_observed: pollObserved ? true : k.polling_measured ? false : null,
      window_messages_dispatched: k.messages_measured ? messageObserved : null, ...(k.modifier_alias_ambiguous === true ? {modifier_alias_ambiguous: true} : {})});
  }
  const expected = allKeysMeasured ? outcome(observed, projected.length) : 'unavailable', pollOutcome = allPollMeasured ? outcome(polled, projected.length) : 'unavailable';
  const all = allKeysMeasured ? observed === projected.length : null, allPoll = allPollMeasured ? polled === projected.length : null, allMessages = allMessagesMeasured ? dispatched === projected.length : null;
  const named = [...pollNames, dispatchName].filter(name => channels.has(name));
  if (o.measured !== allKeysMeasured || o.keys_observed !== observed || o.keys_with_poll_high !== polled || o.keys_with_message_dispatch !== dispatched || o.outcome !== expected || o.all_requested_observed !== all || o.polling_measured !== allPollMeasured || o.polling_outcome !== pollOutcome || o.all_requested_polling_observed !== allPoll || o.all_requested_messages_dispatched !== allMessages || !sameNames(o.observed_channels, named)) return unavailable();
  return {outcome: expected, polling_outcome: pollOutcome, measurement: 'poll_returns_or_window_message_dispatch', application_response: null, input_observed: observed ? true : allKeysMeasured ? false : null,
    reader_return_observed: polled ? true : allPollMeasured ? false : null, window_messages_dispatched: allMessages, channels: named, polling_channels: pollNames.filter(name => pollChannels.has(name)),
    keys_observed: observed, keys_requested: projected.length, all_requested_observed: all, all_requested_polling_observed: allPoll, keys: projected};
}

/** Optional JSON decorator for the existing completed-job presenter. Undefined
 * means legacy version: leave the caller's version1 path authoritative. */
export function qualifyInputReceiptV2(native: Obj): Obj | undefined {
  const observation = object(native.input_read_observation); if (observation?.version !== 2) return;
  if (native.ok !== true || native.accepted !== false || native.job_state !== 'completed' || native.job_complete !== true || native.input_injected !== true || typeof native.job_id !== 'string' || !native.job_id) return native;
  const read = projectInputReadObservation(observation)!;
  if (read.outcome === 'not_applicable') return native;
  if ((read.outcome === 'unavailable' && read.input_observed !== true) || read.outcome === 'pending') return {...native, input_read_warning: 'Native input observations are incomplete or inconsistent; application consumption and response remain unmeasured. Do not replay automatically.'};
  const result: Obj = {...native, input_observed: read.input_observed, reader_return_observed: read.reader_return_observed, observed_input_channels: read.channels,
    polling_outcome: read.polling_outcome, window_messages_dispatched: read.window_messages_dispatched, application_response: null};
  if (read.outcome === 'observed') result.input_read_note = 'The named polling returns or exact-key subclass message dispatch were observed. Message dispatch does not prove application consumption; game response remains unmeasured.';
  else result.input_read_warning = `${read.outcome === 'unavailable' ? 'Named positive input observations are retained, but other requested keys have unavailable evidence.' : read.outcome === 'partial' ? 'Some requested keys had no observed dispatch or polling high-state return.' : 'No requested dispatch or polling high-state return was observed in this finite job window.'} Other APIs or later dispatch may be outside this observation. This does not prove the game ignored input; do not replay automatically.`;
  return result;
}
