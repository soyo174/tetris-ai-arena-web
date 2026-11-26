import {createDuel, DEFAULT_RULES, LEGACY_RULES, choose} from './engine.mjs';

export const MAX_REPLAY_BYTES = 8 * 1024 * 1024;
export const MAX_SERVER_REPLAY_BYTES = 32 * 1024 * 1024;
const MAX_TURNS = 2000;
const engineVersions = new Map([[LEGACY_RULES.profile, '0.1.0'], [DEFAULT_RULES.profile, '0.2.0']]);
const copy = value => structuredClone(value);
const isObject = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const canonical = value => Array.isArray(value) ? value.map(canonical) : isObject(value)
  ? Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])])) : value;
const equal = (a, b) => JSON.stringify(canonical(a)) === JSON.stringify(canonical(b));

export function makeFrame(game, event = null) {
  return {state: game.snapshot(), event: event ? copy(event) : null,
    observations: [game.observe(0), game.observe(1)]};
}

export function createHeader(game, modes) {
  return {type: 'HEADER', version: 1, protocol: 'tetris-duel-jsonl-v1', engine_version: engineVersions.get(game.rules.profile),
    seed: game.seed, rules: copy(game.rules), rules_sha256: game.rulesFingerprint,
    bots: modes.map(mode => ['browser', '--mode', mode]),
    implementation: 'tetra-arena-web-1.2.0', timing: 'browser-local; no competitive time enforcement'};
}

/** Only recognize the known invocation shapes; an external --mode is its own option. */
export function replayBotModes(header) {
  return [0, 1].map(side => {
    const argv = header.bots?.[side];
    if (!Array.isArray(argv) || argv.some(value => typeof value !== 'string')) return null;
    const knownMode = value => ['greedy', 'random', 'drop'].includes(value);
    if (argv.length === 3 && argv[0] === 'browser' && argv[1] === '--mode' && knownMode(argv[2])) return argv[2];
    const executable = (argv[0] || '').split(/[\\/]/).at(-1);
    if (argv.length === 6 && /^python(?:\d+(?:\.\d+)*)?(?:\.exe)?$/i.test(executable)
        && argv[1] === '-m' && argv[2] === 'arena' && argv[3] === 'bot'
        && argv[4] === '--mode' && ['greedy', 'random'].includes(argv[5])) return argv[5];
    return null;
  });
}

export function playTurn(game, modes, rngs) {
  if (game.finished) throw new Error('比赛已经结束。');
  for (let side = 0; side < 2; side++) {
    try {
      let [kind, commands] = choose(game.observe(side), modes[side], rngs[side]);
      if (kind === 'HOLD') {
        if (!game.hold(side)) continue;
        [kind, commands] = choose(game.observe(side), modes[side], rngs[side]);
      }
      if (kind !== 'PLAY') throw new Error('AI 没有提交完整走法');
      game.submit(side, commands);
    } catch (error) {
      if (!(side in game.failures) && !(side in game.actions)) game.fail(side, String(error.message || error));
    }
  }
  const decision = {holds: game.players.map(player => player.hold_used),
    actions: copy(game.actions), failures: copy(game.failures)};
  const event = game.settle();
  return {type: 'TURN', decision, event: copy(event), state: game.snapshot()};
}

export function summarize(game, event) {
  if (!game.finished || !event) return null;
  return {type: 'RESULT', winner: game.winner, draw: game.outcome === 'draw', outcome: game.outcome,
    turns: game.turn - 1, seed: game.seed, rules_sha256: game.rulesFingerprint,
    total_lines: game.players.map(player => player.total_lines),
    total_sent: game.players.map(player => player.total_sent),
    final_reasons: event.players.map(player => copy(player.reasons))};
}

export function serializeReplay(header, turns, result = null) {
  const records = [{...copy(header), partial: !result}, ...turns.map(copy)];
  if (result) records.push(copy(result));
  return records.map(record => JSON.stringify(record)).join('\n') + '\n';
}

function validateDecision(decision) {
  if (!isObject(decision) || !Array.isArray(decision.holds) || decision.holds.length !== 2
      || decision.holds.some(value => typeof value !== 'boolean')) throw new Error('回放的 Hold 决策格式不正确。');
  for (const field of ['actions', 'failures']) {
    if (!isObject(decision[field]) || Object.keys(decision[field]).some(side => side !== '0' && side !== '1')) {
      throw new Error('回放决策包含无效的选手编号。');
    }
  }
  for (const [side, reason] of Object.entries(decision.failures)) {
    if (typeof reason !== 'string' || reason.length > 500 || side in decision.actions) throw new Error('回放的失败决策格式不正确。');
  }
}

function validateBoardState(state) {
  if (!isObject(state) || !Array.isArray(state.players) || state.players.length !== 2) throw new Error('回放的选手状态不完整。');
  for (const player of state.players) {
    if (!Array.isArray(player.board) || player.board.length !== 24 || player.board.some(row =>
      !Array.isArray(row) || row.length !== 10 || row.some(cell => !Number.isInteger(cell) || cell < 0 || cell > 3))) {
      throw new Error('回放棋盘必须是有效的 10 × 24 状态。');
    }
  }
}

function applyDecision(game, decision) {
  for (let side = 0; side < 2; side++) if (decision.holds[side]) game.hold(side);
  for (const [side, reason] of Object.entries(decision.failures)) {
    if (!(side in game.failures)) game.fail(Number(side), reason);
  }
  for (const [side, commands] of Object.entries(decision.actions)) game.submit(Number(side), commands);
  if (!equal(game.actions, decision.actions) || !equal(game.failures, decision.failures)
      || !equal(game.players.map(player => player.hold_used), decision.holds)) {
    throw new Error('记录的决策与实际执行结果不一致');
  }
  return game.settle();
}

export async function playOfficialReplay({header, decisions, result}) {
  const game = await createDuel(header.seed, header.rules);
  if (header.rules_sha256 !== game.rulesFingerprint ||
      header.engine_version !== engineVersions.get(game.rules.profile)) {
    throw new Error('官方回放规则与裁判版本不匹配。');
  }
  if (!Array.isArray(decisions) || decisions.length > MAX_TURNS) throw new Error('官方回放回合数无效。');
  const frames = [makeFrame(game)], turns = [];
  let event = null;
  for (const decision of decisions) {
    validateDecision(decision);
    event = applyDecision(game, decision);
    turns.push({type: 'TURN', decision: copy(decision), event, state: game.snapshot()});
    frames.push(makeFrame(game, event));
  }
  if (!event || !equal(summarize(game, event), result)) throw new Error('官方回放赛果校验失败。');
  return {header: copy(header), turns, frames, result: copy(result)};
}

/** Validate before exposing imported data to the controller or replacing a live match. */
export async function validateReplay(text, {maxBytes = MAX_REPLAY_BYTES} = {}) {
  if (![MAX_REPLAY_BYTES, MAX_SERVER_REPLAY_BYTES].includes(maxBytes)) throw new Error('无效的回放大小限制。');
  if (typeof text !== 'string' || new TextEncoder().encode(text).byteLength > maxBytes) {
    throw new Error(`回放文件过大，请选择不超过 ${maxBytes / 1024 / 1024} MB 的 JSONL 文件。`);
  }
  const lines = text.trim().split(/\r?\n/).filter(line => line.trim());
  if (!lines.length || lines.length > MAX_TURNS + 2) throw new Error('回放为空或超过 2000 回合的限制。');
  let records;
  try { records = lines.map(line => JSON.parse(line)); }
  catch { throw new Error('文件不是有效的 JSONL 回放，请检查 JSON 格式。'); }
  const header = records.shift();
  if (!isObject(header) || header.type !== 'HEADER' || header.version !== 1
      || header.protocol !== 'tetris-duel-jsonl-v1') throw new Error('回放头格式或版本不受支持。');
  if (!Number.isSafeInteger(header.seed)) throw new Error('回放种子超出网页支持的整数范围（±9007199254740991）。');
  const expectedVersion = engineVersions.get(header.rules?.profile);
  if (!expectedVersion || header.engine_version !== expectedVersion) {
    throw new Error('回放的裁判版本与规则版本不匹配或不受支持。');
  }
  const game = await createDuel(header.seed, header.rules);
  if (header.rules_sha256 !== game.rulesFingerprint) throw new Error('回放的规则校验值不一致。');
  const frames = [makeFrame(game)], turns = [];
  let result = null, last = null;
  for (const record of records) {
    if (result) throw new Error('终局结果之后不能再出现其他记录。');
    if (!isObject(record)) throw new Error('回放记录格式不正确。');
    if (record.type === 'RESULT') {
      const expected = summarize(game, last);
      if (!expected || Object.keys(expected).some(key => !equal(record[key], expected[key]))) {
        throw new Error('终局结果或出局原因与重新计算的结果不一致。');
      }
      result = copy(record);
      continue;
    }
    if (record.type !== 'TURN' || game.finished) throw new Error('回放的回合顺序不正确，或比赛已经结束。');
    validateDecision(record.decision);
    validateBoardState(record.state);
    try {
      last = applyDecision(game, record.decision);
    } catch (error) { throw new Error(`第 ${turns.length + 1} 回合的走法无效：${error.message}`); }
    if (!equal(last, record.event) || !equal(game.snapshot(), record.state)) {
      throw new Error(`第 ${turns.length + 1} 回合的棋盘或结算状态与重算结果不一致。`);
    }
    turns.push(copy(record));
    frames.push(makeFrame(game, last));
    if (turns.length % 32 === 0) await new Promise(resolve => setTimeout(resolve, 0));
  }
  if (!result && (header.partial !== true || game.finished)) throw new Error('回放不完整或被截断：缺少终局结果。');
  return {header: copy(header), turns, frames, result};
}
