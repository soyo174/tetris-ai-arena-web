// Browser port of the supplied tetris-duel-jsonl-v1 referee. Rows run bottom to top.
const WIDTH = 10, HEIGHT = 24;
const PIECES = [...'IOTSZJL'];
const sides = [0, 1];
const emptyRow = () => Array(WIDTH).fill(0);
const detached = value => structuredClone(value);

function deepFreeze(value) {
  if (value && typeof value === 'object' && !Object.isFrozen(value)) {
    Object.values(value).forEach(deepFreeze);
    Object.freeze(value);
  }
  return value;
}

const V4_KICKS_180 = {
  '0:2': [[0, 0], [0, 1], [1, 1], [-1, 1], [1, 0], [-1, 0]],
  '2:0': [[0, 0], [0, -1], [-1, -1], [1, -1], [-1, 0], [1, 0]],
  '1:3': [[0, 0], [1, 0], [1, 2], [1, 1], [0, 2], [0, 1]],
  '3:1': [[0, 0], [-1, 0], [-1, 2], [-1, 1], [0, 2], [0, 1]],
};

export const PREVIOUS_RULES = deepFreeze({
  profile: 'reference-provisional-v2',
  width: WIDTH, height: HEIGHT, main_height: 20,
  normal_attack: [0, 0, 1, 2, 4], tspin_attack: [0, 2, 4, 6],
  b2b_bonus: 1, perfect_clear_floor: 10, overtime_first: 210, overtime_interval: 10,
  think_ms: 1000, max_commands: 512, max_response_bytes: 65536,
  ambiguous_failure_outcome: 'draw',
  spawn_origins: Object.fromEntries(PIECES.map(p => [p, [3, p === 'I' ? 18 : 19]])),
  kicks_180: [[0,0], [-1,0], [1,0], [0,1], [0,2]], kicks_90_override: {}
});

export const LEGACY_RULES = deepFreeze({...PREVIOUS_RULES, profile: 'reference-provisional-v1'});
export const V3_RULES = deepFreeze({...PREVIOUS_RULES, profile: 'reference-provisional-v3',
  first_think_ms: 1000, think_ms: 50});
export const V4_RULES = deepFreeze({
  ...PREVIOUS_RULES,
  profile: 'reference-provisional-v4',
  overtime_first: 150,
  overtime_interval: 8,
  first_think_ms: 1000,
  think_ms: 100,
  time_bank_ms: 2000,
  ambiguous_failure_outcome: 'fault_loses',
  kicks_180: V4_KICKS_180,
  next_preview: 5,
  garbage_messiness: 25,
  garbage_cap: 8,
  cross_cancel: true,
  max_turns: 400,
  compact_observe: true,
});
export const V5_RULES = deepFreeze({
  profile: 'reference-realtime-v5',
  width: WIDTH,
  height: HEIGHT,
  main_height: 20,
  normal_attack: [0, 0, 1, 2, 4],
  tspin_attack: [0, 2, 4, 6],
  b2b_bonus: 1,
  perfect_clear_floor: 10,
  first_think_ms: 1000,
  think_ms: 100,
  time_bank_ms: 2000,
  max_commands: 512,
  max_response_bytes: 65536,
  ambiguous_failure_outcome: 'fault_loses',
  spawn_origins: Object.fromEntries(PIECES.map(p => [p, [3, p === 'I' ? 18 : 19]])),
  kicks_180: V4_KICKS_180,
  kicks_90_override: {},
  next_preview: 5,
  garbage_messiness: 25,
  garbage_cap: 8,
  cross_cancel: true,
  compact_observe: true,
  fps: 60,
  move_frames: 2,
  rotate_frames: 1,
  soft_drop_frames: 2,
  hold_frames: 2,
  lock_frames: 8,
  clear_frames: 12,
  special_clear_frames: 16,
  free_think_ms: 30,
  think_ms_per_frame: 25,
  max_think_frames: 6,
  garbage_delay_frames: 30,
  combo_block_limit: 3,
  overtime_first_frame: 3600,
  overtime_interval_frames: 240,
  max_frames: 10800,
});
export const V51_RULES = deepFreeze({...V5_RULES, profile: 'reference-realtime-v5.1'});
export const V52_RULES = deepFreeze({...V5_RULES, profile: 'reference-realtime-v5.2'});
const cumulativeRules = {...V52_RULES, profile: 'reference-realtime-v5.3', free_think_ms: 0};
delete cumulativeRules.think_ms_per_frame;
export const DEFAULT_RULES = deepFreeze(cumulativeRules);
const ruleProfiles = [LEGACY_RULES, PREVIOUS_RULES, V3_RULES, V4_RULES, V5_RULES, V51_RULES, V52_RULES, DEFAULT_RULES];

const spawnCells = {
  T: [[0,1],[1,1],[2,1],[1,2]], J: [[0,1],[1,1],[2,1],[0,2]],
  L: [[0,1],[1,1],[2,1],[2,2]], S: [[0,1],[1,1],[1,2],[2,2]],
  Z: [[1,1],[2,1],[0,2],[1,2]], I: [[0,2],[1,2],[2,2],[3,2]],
  O: [[1,1],[2,1],[1,2],[2,2]]
};
export const CELLS = deepFreeze(Object.fromEntries(Object.entries(spawnCells).map(([p, cells]) => {
  const rotations = [cells];
  const pivot2 = p === 'I' ? 3 : 2;
  for (let i = 1; i < 4; i++) {
    rotations.push(p === 'O' ? cells : rotations[i - 1].map(([x, y]) => [y, pivot2 - x]));
  }
  return [p, rotations];
})));

const standardKicks = {
  '0:1': [[0,0],[-1,0],[-1,1],[0,-2],[-1,-2]],
  '1:0': [[0,0],[1,0],[1,-1],[0,2],[1,2]],
  '2:3': [[0,0],[1,0],[1,1],[0,-2],[1,-2]],
  '3:2': [[0,0],[-1,0],[-1,-1],[0,2],[-1,2]]
};
standardKicks['1:2'] = standardKicks['1:0'];
standardKicks['2:1'] = standardKicks['0:1'];
standardKicks['3:0'] = standardKicks['3:2'];
standardKicks['0:3'] = standardKicks['2:3'];

const iKicks = {
  '0:1': [[0,0],[-2,0],[1,0],[-2,-1],[1,2]],
  '1:0': [[0,0],[2,0],[-1,0],[2,1],[-1,-2]],
  '1:2': [[0,0],[-1,0],[2,0],[-1,2],[2,-1]],
  '2:1': [[0,0],[1,0],[-2,0],[1,-2],[-2,1]]
};
iKicks['2:3'] = iKicks['1:0'];
iKicks['3:2'] = iKicks['0:1'];
iKicks['3:0'] = iKicks['2:1'];
iKicks['0:3'] = iKicks['1:2'];

function kicks(p, old, next, rules) {
  if ((next - old + 4) % 4 === 2) {
    return Array.isArray(rules.kicks_180) ? rules.kicks_180 : rules.kicks_180[old + ':' + next];
  }
  const key = old + ':' + next;
  return rules.kicks_90_override[p + ':' + key] ??
    (p === 'O' ? [[0,0]] : (p === 'I' ? iKicks : standardKicks)[key]);
}

export function geometry(rules = DEFAULT_RULES) {
  const kicks90 = {};
  const pivots = {};
  for (const p of PIECES) {
    pivots[p] = 'IO'.includes(p) ? [1.5, 1.5] : [1, 1];
    kicks90[p] = {};
    for (let old = 0; old < 4; old++) {
      for (const next of [(old + 1) % 4, (old + 3) % 4]) {
        kicks90[p][old + ':' + next] = kicks(p, old, next, rules);
      }
    }
  }
  return detached({cells: CELLS, pivots, kicks_90: kicks90, kicks_180: rules.kicks_180,
    coordinate_system: '0-based; rows bottom-to-top; y-up'});
}

function canonical(value) {
  if (Array.isArray(value)) return '[' + value.map(canonical).join(',') + ']';
  if (value && typeof value === 'object') {
    return '{' + Object.keys(value).sort().map(k => canonical(k) + ':' + canonical(value[k])).join(',') + '}';
  }
  return JSON.stringify(value).replace(/[\u007f-\uffff]/g,
    char => '\\u' + char.charCodeAt(0).toString(16).padStart(4, '0'));
}

async function sha256(text) {
  const bytes = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return Array.from(new Uint8Array(bytes), byte => byte.toString(16).padStart(2, '0')).join('');
}

export async function fingerprint(rules = DEFAULT_RULES) {
  return sha256(canonical({rules, geometry: geometry(rules)}));
}

export class PythonRandom {
  #words = new Uint32Array(624);
  #index = 624;

  constructor(seed = 0) {
    if (typeof seed !== 'bigint' && !Number.isSafeInteger(seed)) throw Error('integer seed required');
    let n = BigInt(seed);
    if (n < 0n) n = -n;
    const key = [];
    do {
      key.push(Number(n & 0xffffffffn));
      n >>= 32n;
    } while (n);
    const words = this.#words;
    const mix = (word, multiplier) => Math.imul(word ^ (word >>> 30), multiplier);
    words[0] = 19650218;
    for (let i = 1; i < 624; i++) words[i] = mix(words[i - 1], 1812433253) + i;
    let i = 1, j = 0;
    for (let k = Math.max(624, key.length); k > 0; k--) {
      words[i] = (words[i] ^ mix(words[i - 1], 1664525)) + key[j] + j;
      if (++i === 624) { words[0] = words[623]; i = 1; }
      j = (j + 1) % key.length;
    }
    for (let k = 623; k > 0; k--) {
      words[i] = (words[i] ^ mix(words[i - 1], 1566083941)) - i;
      if (++i === 624) { words[0] = words[623]; i = 1; }
    }
    words[0] = 0x80000000;
  }

  #word() {
    const words = this.#words;
    if (this.#index === 624) {
      for (let i = 0; i < 624; i++) {
        const y = (words[i] & 0x80000000) | (words[(i + 1) % 624] & 0x7fffffff);
        words[i] = words[(i + 397) % 624] ^ (y >>> 1) ^ ((y & 1) ? 0x9908b0df : 0);
      }
      this.#index = 0;
    }
    let y = words[this.#index++];
    y ^= y >>> 11;
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    return (y ^ (y >>> 18)) >>> 0;
  }

  getrandbits(k) {
    if (!Number.isSafeInteger(k) || k < 0) throw Error('invalid bit count');
    if (!k) return 0;
    if (k <= 32) return this.#word() >>> (32 - k);
    let result = 0n;
    for (let offset = 0; offset < k; offset += 32) {
      const bits = Math.min(32, k - offset);
      result |= BigInt(this.#word() >>> (32 - bits)) << BigInt(offset);
    }
    return k <= 53 ? Number(result) : result;
  }

  randbelow(n) {
    if (!Number.isSafeInteger(n) || n < 1) throw Error('positive integer bound required');
    const bits = n <= 0xffffffff ? 32 - Math.clz32(n) : n.toString(2).length;
    let value;
    do { value = this.getrandbits(bits); } while (value >= n);
    return value;
  }

  randrange(n) { return this.randbelow(n); }
  choice(items) { return items[this.randbelow(items.length)]; }
  shuffle(items) {
    for (let i = items.length - 1; i > 0; i--) {
      const j = this.randbelow(i + 1);
      [items[i], items[j]] = [items[j], items[i]];
    }
  }
}

class SevenBag {
  sequence = [];
  constructor(seed) { this.rng = new PythonRandom(seed); }

  at(index) {
    while (this.sequence.length <= index) {
      const bag = PIECES.slice();
      this.rng.shuffle(bag);
      this.sequence.push(...bag);
    }
    return this.sequence[index];
  }
}

const translations = {LEFT: [-1,0], RIGHT: [1,0], DOWN: [0,-1]};
const rotations = {CW: 1, CCW: -1, ROT180: 2};
const moves = new Set([...Object.keys(translations), ...Object.keys(rotations), 'HARD_DROP']);

function validate(commands, rules) {
  if (!Array.isArray(commands) || !commands.length || commands.length > rules.max_commands) {
    throw Error('commands must be a nonempty bounded list');
  }
  for (const move of commands) if (!moves.has(move)) throw Error('unknown instruction');
  if (commands.indexOf('HARD_DROP') !== commands.length - 1) {
    throw Error('route must end with exactly one HARD_DROP');
  }
}

function spawn(piece, rules) {
  const [x, y] = rules.spawn_origins[piece];
  return {piece, x, y, rotation: 0, last: 'SPAWN', kick_index: 0};
}

function fits(rows, p, x = p.x, y = p.y, rotation = p.rotation) {
  for (const [dx, dy] of CELLS[p.piece][rotation]) {
    const col = x + dx, row = y + dy;
    if (col < 0 || col >= WIDTH || row < 0 || row >= HEIGHT || rows[row][col] !== 0) return false;
  }
  return true;
}

function step(rows, p, command, rules) {
  if (Object.hasOwn(translations, command)) {
    const [dx, dy] = translations[command];
    if (fits(rows, p, p.x + dx, p.y + dy)) {
      p.x += dx;
      p.y += dy;
      p.last = 'MOVE';
      p.kick_index = 0;
    }
    return;
  }
  if (command === 'HARD_DROP') {
    while (fits(rows, p, p.x, p.y - 1)) {
      p.y--;
      p.last = 'DROP';
      p.kick_index = 0;
    }
    return;
  }
  const rotation = (p.rotation + rotations[command] + 4) % 4;
  const offsets = kicks(p.piece, p.rotation, rotation, rules);
  for (let i = 0; i < offsets.length; i++) {
    const [dx, dy] = offsets[i];
    if (fits(rows, p, p.x + dx, p.y + dy, rotation)) {
      p.x += dx;
      p.y += dy;
      p.rotation = rotation;
      p.last = command === 'ROT180' ? 'ROT180' : 'ROT90';
      p.kick_index = i + 1;
      return;
    }
  }
}

function spinType(rows, pose, lines) {
  if (pose.piece !== 'T' || !['ROT90', 'ROT180'].includes(pose.last)) return 'none';
  const {x, y} = pose;
  const occupied = (col, row) => col < 0 || col >= WIDTH || row < 0 || (row < HEIGHT && rows[row][col] !== 0);
  const corners = [occupied(x, y+2), occupied(x+2, y+2), occupied(x+2, y), occupied(x, y)];
  if (corners.filter(Boolean).length < 3) return 'none';
  if ((corners[pose.rotation] && corners[(pose.rotation+1) % 4]) ||
      lines >= 2 || (pose.last === 'ROT90' && pose.kick_index === 5)) return 'full';
  return 'mini';
}

function execute(board, piece, commands, rules) {
  const pose = spawn(piece, rules);
  const rows = board.rows;
  if (!fits(rows, pose)) return null;
  for (const command of commands) step(rows, pose, command, rules);
  for (const [dx, dy] of CELLS[piece][pose.rotation]) rows[pose.y + dy][pose.x + dx] = 1;
  const kept = rows.filter(row => row.includes(0) || row.includes(3));
  const lines = HEIGHT - kept.length;
  const spin = spinType(rows, pose, lines);
  for (let i = 0; i < lines; i++) kept.push(emptyRow());
  board.rows = kept;
  return {pose, lines, spin, perfect_clear: !kept.some(row => row.some(Boolean))};
}

/** Reconstruct the decisive rotation on the pre-lock board, including wall kicks. */
export function traceTSpin(rows, player, rules, solidRisen = 0) {
  const lock = player.lock;
  if (!lock || lock.spin === 'none') return null;
  const board = {rows: detached(rows)};
  if (solidRisen) rise(board, solidRisen);
  const pose = spawn('T', rules);
  let rotation = null;
  for (const command of player.commands) {
    const quarterTurns = rotations[command];
    const before = quarterTurns ? {...pose} : null;
    step(board.rows, pose, command, rules);
    if (before && pose.rotation !== before.rotation) rotation = {from: before, quarterTurns};
  }
  if (!rotation) return null;
  return {...rotation, to: lock.pose, rows: board.rows};
}

function solidFloorHeight(rows) {
  let height = 0;
  while (height < HEIGHT && rows[height].every(cell => cell === 3)) height++;
  return height;
}

function rise(board, count, hole = null, keepSolidFloor = false) {
  const floor = keepSolidFloor ? solidFloorHeight(board.rows) : 0;
  const added = Math.min(count, HEIGHT - floor);
  const retainedTop = HEIGHT - added;
  const overflow = (keepSolidFloor && count > added) ||
    board.rows.slice(retainedTop).some(row => row.some(Boolean));
  const row = Array(WIDTH).fill(hole === null ? 3 : 2);
  if (hole !== null) row[hole] = 0;
  const inserted = Array.from({length: added}, () => row.slice());
  board.rows = [...board.rows.slice(0, floor), ...inserted, ...board.rows.slice(floor, retainedTop)];
  return overflow;
}

function riseHoles(board, holes, keepSolidFloor = true) {
  let overflow = false;
  for (const hole of holes) {
    if (rise(board, 1, hole, keepSolidFloor)) overflow = true;
  }
  return overflow;
}

function generateHoles(rng, lines, messiness) {
  let hole = rng.randbelow(WIDTH);
  const holes = [hole];
  for (let i = 1; i < lines; i++) {
    if (messiness > 0 && rng.randbelow(100) < messiness) {
      hole = (hole + 1 + rng.randbelow(WIDTH - 1)) % WIDTH;
    }
    holes.push(hole);
  }
  return holes;
}

function warningHoles(w) {
  return Array.isArray(w.holes) ? w.holes.slice() : Array(w.lines).fill(w.hole);
}

function isProgramFault(reason) {
  return !['hold_spawn_blocked', 'spawn_blocked'].includes(reason);
}

export function preLockFrames(commands, holdUsed, thinkFrames, rules) {
  let cost = holdUsed ? rules.hold_frames : 0;
  for (const cmd of commands) {
    if (cmd === 'LEFT' || cmd === 'RIGHT') cost += rules.move_frames;
    else if (cmd === 'CW' || cmd === 'CCW' || cmd === 'ROT180') cost += rules.rotate_frames;
    else if (cmd === 'DOWN') cost += rules.soft_drop_frames;
  }
  return cost + Math.max(0, Math.min(rules.max_think_frames, thinkFrames | 0));
}

function postLockFrames(lines, spin, rules) {
  if (!lines) return rules.lock_frames;
  const clear = lines === 4 || spin === 'full' ? rules.special_clear_frames : rules.clear_frames;
  return rules.lock_frames + clear;
}

function overtimeCount(frame, rules) {
  if (frame < rules.overtime_first_frame) return 0;
  return 1 + Math.floor((frame - rules.overtime_first_frame) / rules.overtime_interval_frames);
}

function score(lock, chain, rules) {
  const {lines, spin, perfect_clear} = lock;
  if (!lines) {
    chain.combo = 0;
    return 0;
  }
  chain.combo++;
  const spinAttack = spin === 'full' || (spin === 'mini' && lines >= 2);
  let attack = (spinAttack ? rules.tspin_attack : rules.normal_attack)[lines];
  if (spinAttack || lines === 4) {
    if (chain.b2b) attack += rules.b2b_bonus;
    chain.b2b = true;
  } else if (spin !== 'mini') {
    chain.b2b = false;
  }
  attack += [2, 5, 7, 9, 12].filter(bound => chain.combo > bound).length;
  return perfect_clear ? Math.max(attack, rules.perfect_clear_floor) : attack;
}

function newPlayer(piece) {
  return {
    board: {rows: Array.from({length: HEIGHT}, emptyRow)}, current: piece, next_index: 1,
    hold: null, hold_used: false, warnings: [], chain: {combo: 0, b2b: false},
    alive: true, total_lines: 0, total_sent: 0,
    next_frame: 0, pieces_placed: 0, combo_blocks: 0, pending: null,
  };
}

class Duel {
  #bags;
  #garbage;
  #geometry;
  turn = 1;
  frame = 0;
  overtime_applied = 0;
  actions = {};
  think_frames = {};
  failures = {};
  finished = false;
  winner = null;
  outcome = 'ongoing';

  constructor(seed, rules, rulesFingerprint, bagSeeds, garbageSeeds) {
    this.seed = seed;
    this.rules = rules;
    this.rulesFingerprint = rulesFingerprint;
    this.#geometry = geometry(rules);
    this.#bags = bagSeeds.map(s => new SevenBag(s));
    this.#garbage = garbageSeeds.map(s => new PythonRandom(s));
    this.players = sides.map(side => newPlayer(this.#bags[side].at(0)));
  }

  get isRealtime() {
    return this.rules.profile === V5_RULES.profile || this.hasDelayedLocks;
  }

  get hasDelayedLocks() {
    return this.rules.profile === V51_RULES.profile || this.hasScheduledOvertime;
  }

  get hasScheduledOvertime() {
    return this.rules.profile === V52_RULES.profile || this.rules.profile === DEFAULT_RULES.profile;
  }

  #nextOvertimeFrame() {
    if (!this.hasScheduledOvertime) return Infinity;
    return this.rules.overtime_first_frame + this.overtime_applied * this.rules.overtime_interval_frames;
  }

  get isV4Plus() {
    return this.rules.profile === V4_RULES.profile || this.isRealtime;
  }

  activeSides() {
    if (!this.isRealtime) return [0, 1];
    const earliest = Math.min(this.players[0].next_frame, this.players[1].next_frame);
    if (this.hasDelayedLocks && (this.#nextOvertimeFrame() <= earliest || earliest > this.rules.max_frames)) return [];
    return sides.filter(i => this.players[i].next_frame === earliest && this.players[i].pending === null);
  }

  #side(side) {
    if (side !== 0 && side !== 1) throw Error('duel side must be 0 or 1');
    return this.players[side];
  }

  #ready(side) {
    const p = this.#side(side);
    if (this.finished || Object.hasOwn(this.actions, side) || Object.hasOwn(this.failures, side)) {
      throw Error('turn is already closed for this player');
    }
    if (this.isRealtime && !this.activeSides().includes(side)) {
      throw Error('player is not active at the current frame');
    }
    return p;
  }

  observe(side) {
    const p = this.#side(side), other = this.players[1 - side];
    const own = {
      board: p.board.rows,
      current: p.current,
      next: this.#bags[side].at(p.next_index),
      hold: p.hold,
      can_hold: !p.hold_used,
      warnings: p.warnings.map(({lines, due}) => ({lines, due})),
      combo: p.chain.combo,
      b2b: p.chain.b2b,
      total_lines: p.total_lines,
      total_sent: p.total_sent,
    };
    if (this.rules.next_preview && this.rules.next_preview > 1) {
      own.next_queue = Array.from({length: this.rules.next_preview}, (_, offset) => this.#bags[side].at(p.next_index + offset));
    }
    const opponent = {id: 1 - side, alive: other.alive, board: other.board.rows};
    let includeStatic;
    if (this.isRealtime) {
      own.next_frame = p.next_frame;
      own.pieces_placed = p.pieces_placed;
      own.combo_blocks = p.combo_blocks;
      opponent.next_frame = other.next_frame;
      opponent.pieces_placed = other.pieces_placed;
      includeStatic = !this.rules.compact_observe || (p.pieces_placed === 0 && !p.hold_used);
    } else {
      includeStatic = !this.rules.compact_observe || (this.turn === 1 && !p.hold_used);
    }
    const payload = {
      protocol: 'tetris-duel-jsonl-v1',
      turn: this.turn,
      you: side,
      rules_sha256: this.rulesFingerprint,
      self: own,
      opponent,
    };
    if (this.isRealtime) payload.frame = this.frame;
    if (includeStatic) {
      payload.rules = this.rules;
      payload.geometry = this.#geometry;
    }
    return detached(payload);
  }

  hold(side) {
    const p = this.#ready(side);
    if (p.hold_used) throw Error('second HOLD in one turn');
    const old = p.current;
    p.current = p.hold ?? this.#bags[side].at(p.next_index++);
    p.hold = old;
    p.hold_used = true;
    if (!fits(p.board.rows, spawn(p.current, this.rules))) {
      this.failures[side] = 'hold_spawn_blocked';
      return false;
    }
    return true;
  }

  submit(side, commands, thinkFrames = 0) {
    this.#ready(side);
    validate(commands, this.rules);
    if (this.isRealtime) {
      if (!Number.isInteger(thinkFrames) || thinkFrames < 0 || thinkFrames > this.rules.max_think_frames) {
        throw Error('invalid think_frames');
      }
      this.think_frames[side] = thinkFrames;
    }
    this.actions[side] = commands.slice();
  }

  fail(side, reason) {
    this.#ready(side);
    this.failures[side] = String(reason);
  }

  settle() {
    return this.isRealtime ? this.#settleRealtime() : this.#settleTurnBased();
  }

  #scheduleLocks(ready, currentFrame) {
    for (const side of ready) {
      const player = this.players[side];
      const commands = this.actions[side] ?? null;
      const thinkFrames = this.think_frames[side] ?? 0;
      player.pending = {
        commands, failure: this.failures[side] ?? null,
        hold_used: player.hold_used, think_frames: thinkFrames,
      };
      if (commands !== null) {
        player.next_frame += preLockFrames(commands, player.hold_used, thinkFrames, this.rules);
      }
      player.hold_used = false;
    }

    let nextReady = Infinity, nextLock = Infinity;
    for (const player of this.players) {
      if (player.pending === null) nextReady = Math.min(nextReady, player.next_frame);
      else nextLock = Math.min(nextLock, player.next_frame);
    }
    const overtimeFrame = this.#nextOvertimeFrame();
    if (overtimeFrame <= Math.min(nextReady, nextLock, this.rules.max_frames)) return [overtimeFrame, []];
    if (Math.min(nextReady, nextLock) > this.rules.max_frames) return [this.rules.max_frames, []];
    // A newly ready seat might submit an immediate drop on a queued lock's frame.
    if (nextReady <= nextLock) return [currentFrame, []];
    return [nextLock, sides.filter(i => this.players[i].pending !== null && this.players[i].next_frame === nextLock)];
  }

  #settleRealtime() {
    if (this.finished) throw Error('match is finished');
    let active = this.activeSides();
    if (active.some(i => !Object.hasOwn(this.actions, i) && !Object.hasOwn(this.failures, i))) {
      throw Error('active players must finish before settlement');
    }
    const stepIndex = this.turn;
    let eventFrame = Math.min(this.players[0].next_frame, this.players[1].next_frame);
    let moves = Object.fromEntries(active.map(i => [i, {
      commands: this.actions[i] ?? null, failure: this.failures[i] ?? null,
      hold_used: this.players[i].hold_used, think_frames: this.think_frames[i] ?? 0,
    }]));
    if (this.hasDelayedLocks) {
      [eventFrame, active] = this.#scheduleLocks(active, eventFrame);
      moves = Object.fromEntries(active.map(i => [i, this.players[i].pending]));
    }
    this.frame = eventFrame;

    const records = sides.map(i => ({
      commands: moves[i]?.commands ?? null, attack: 0, sent: 0, cancelled: 0, risen: 0, reasons: []
    }));

    const targetOt = overtimeCount(eventFrame, this.rules);
    const solidDelta = Math.max(0, targetOt - this.overtime_applied);
    if (solidDelta > 0) {
      for (const i of sides) {
        if (rise(this.players[i].board, solidDelta)) records[i].reasons.push('solid_overflow');
      }
      this.overtime_applied = targetOt;
    }

    const lockedLines = [0, 0];
    for (const i of active) {
      const p = this.players[i], record = records[i];
      const move = moves[i];
      if (this.hasDelayedLocks) record.hold_used = move.hold_used;
      p.pending = null;
      if (move.failure !== null) {
        record.reasons.push(move.failure);
        continue;
      }
      const lock = execute(p.board, p.current, move.commands, this.rules);
      if (lock === null) {
        record.reasons.push('spawn_blocked');
        continue;
      }
      record.lock = lock;
      record.attack = score(lock, p.chain, this.rules);
      lockedLines[i] = lock.lines;
      const beforeLock = preLockFrames(move.commands, move.hold_used, move.think_frames, this.rules);
      const afterLock = postLockFrames(lock.lines, lock.spin, this.rules);
      record.frames = beforeLock + afterLock;
      record.think_frames = move.think_frames;
      const delay = this.hasDelayedLocks ? afterLock : record.frames;
      p.total_lines += lock.lines;
      p.pieces_placed++;
      p.current = this.#bags[i].at(p.next_index++);
      p.next_frame = eventFrame + delay;
    }

    const outgoing = [0, 0];
    const cap = this.rules.garbage_cap;
    for (const i of active) {
      const p = this.players[i], record = records[i];
      let remaining = record.attack;
      const due = [], future = [];
      for (const w of p.warnings) {
        const used = Math.min(remaining, w.lines);
        remaining -= used;
        record.cancelled += used;
        const lines = w.lines - used;
        if (!lines) continue;
        const nextPacket = {lines, due: w.due, holes: warningHoles(w).slice(used)};
        if (w.due > eventFrame) future.push(nextPacket);
        else due.push(nextPacket);
      }
      outgoing[i] = remaining;

      if (due.length && lockedLines[i] > 0 && p.combo_blocks < this.rules.combo_block_limit) {
        p.combo_blocks++;
        const deferred = due.map(w => ({lines: w.lines, due: eventFrame, holes: warningHoles(w)}));
        p.warnings = [...deferred, ...future];
        record.combo_blocked = true;
      } else {
        const deferred = [];
        let budget = cap;
        for (const w of due) {
          const holes = warningHoles(w);
          const take = Math.min(holes.length, budget);
          if (take > 0) {
            budget -= take;
            record.risen += take;
            if (riseHoles(p.board, holes.slice(0, take), true)) {
              record.reasons.push('garbage_overflow');
            }
          }
          const leftover = holes.slice(take);
          if (leftover.length) {
            deferred.push({lines: leftover.length, due: eventFrame + 1, holes: leftover});
          }
        }
        p.warnings = [...deferred, ...future];
        if (lockedLines[i] === 0 || (due.length && !this.hasDelayedLocks)) p.combo_blocks = 0;
      }
    }

    if (active.length === 2 && this.rules.cross_cancel) {
      const mutual = Math.min(outgoing[0], outgoing[1]);
      if (mutual) {
        for (const i of sides) {
          outgoing[i] -= mutual;
          records[i].cancelled += mutual;
        }
      }
    }

    for (const sender of active) {
      const lines = outgoing[sender], receiver = 1 - sender;
      records[sender].sent = lines;
      this.players[sender].total_sent += lines;
      if (lines) {
        const holes = generateHoles(this.#garbage[receiver], lines, this.rules.garbage_messiness);
        this.players[receiver].warnings.push({
          lines, due: eventFrame + this.rules.garbage_delay_frames, holes,
        });
      }
    }

    const checkSides = solidDelta > 0 ? sides : active;
    for (const i of checkSides) {
      const p = this.players[i], {reasons} = records[i];
      if (!fits(p.board.rows, spawn(p.current, this.rules))) reasons.push('next_spawn_blocked');
      p.alive = reasons.length === 0;
    }
    for (const i of active) this.players[i].hold_used = false;

    const survivors = sides.filter(i => this.players[i].alive);
    const nextClock = Math.min(this.players[0].next_frame, this.players[1].next_frame);
    const overtimeOnly = this.hasScheduledOvertime && solidDelta > 0;
    const timeLimitHit = (eventFrame >= this.rules.max_frames && !overtimeOnly)
      || (!this.hasDelayedLocks && nextClock > this.rules.max_frames);
    this.finished = survivors.length < 2 || timeLimitHit;
    this.winner = survivors.length === 1 ? survivors[0] : null;
    if (this.finished) {
      if (survivors.length === 1) {
        this.outcome = 'win';
      } else if (survivors.length === 2) {
        this.outcome = 'draw';
      } else {
        const faultSides = Object.entries(moves)
          .filter(([, move]) => move.failure !== null && isProgramFault(move.failure))
          .map(([side]) => Number(side));
        if (this.rules.ambiguous_failure_outcome === 'fault_loses' && faultSides.length === 1) {
          this.winner = 1 - faultSides[0];
          this.outcome = 'win';
        } else if (faultSides.length) {
          this.outcome = this.rules.ambiguous_failure_outcome === 'fault_loses'
            ? 'draw' : this.rules.ambiguous_failure_outcome;
        } else {
          this.outcome = 'draw';
        }
      }
    } else {
      this.frame = this.hasDelayedLocks
        ? Math.min(nextClock, this.rules.max_frames, this.#nextOvertimeFrame())
        : nextClock;
    }

    this.turn++;
    this.actions = {};
    this.think_frames = {};
    this.failures = {};
    return {
      turn: stepIndex, frame: eventFrame, active, players: records, solid_risen: solidDelta,
      finished: this.finished, winner: this.winner, outcome: this.outcome,
    };
  }

  #settleTurnBased() {
    if (this.finished) throw Error('match is finished');
    if (sides.some(i => !Object.hasOwn(this.actions, i) && !Object.hasOwn(this.failures, i))) {
      throw Error('both players must finish before settlement');
    }
    const turn = this.turn;
    const records = sides.map(i => ({
      commands: this.actions[i] ?? null, attack: 0, sent: 0, cancelled: 0, risen: 0, reasons: []
    }));

    for (const i of sides) {
      const p = this.players[i], record = records[i];
      if (Object.hasOwn(this.failures, i)) {
        record.reasons.push(this.failures[i]);
        continue;
      }
      const lock = execute(p.board, p.current, this.actions[i], this.rules);
      if (lock === null) {
        record.reasons.push('spawn_blocked');
        continue;
      }
      record.lock = lock;
      record.attack = score(lock, p.chain, this.rules);
      p.total_lines += lock.lines;
      p.current = this.#bags[i].at(p.next_index++);
    }

    const outgoing = [0, 0];
    const keepSolidFloor = this.rules.profile !== LEGACY_RULES.profile;
    const cap = this.rules.garbage_cap ?? null;
    for (const i of sides) {
      const p = this.players[i], record = records[i];
      let remaining = record.attack;
      const due = [], future = [];
      for (const w of p.warnings) {
        const used = Math.min(remaining, w.lines);
        remaining -= used;
        record.cancelled += used;
        const lines = w.lines - used;
        if (!lines) continue;
        const nextPacket = Array.isArray(w.holes)
          ? {lines, due: w.due, holes: w.holes.slice(used)}
          : {lines, due: w.due, hole: w.hole};
        if (w.due > turn) future.push(nextPacket);
        else due.push(nextPacket);
      }
      outgoing[i] = remaining;
      const deferred = [];
      let budget = cap;
      for (const w of due) {
        if (budget === null) {
          record.risen += w.lines;
          const overflow = Array.isArray(w.holes)
            ? riseHoles(p.board, w.holes, keepSolidFloor)
            : rise(p.board, w.lines, w.hole, keepSolidFloor);
          if (overflow) record.reasons.push('garbage_overflow');
          continue;
        }
        const holes = warningHoles(w);
        const take = Math.min(holes.length, budget);
        if (take > 0) {
          budget -= take;
          record.risen += take;
          if (riseHoles(p.board, holes.slice(0, take), keepSolidFloor)) {
            record.reasons.push('garbage_overflow');
          }
        }
        const leftover = holes.slice(take);
        if (leftover.length) {
          deferred.push({lines: leftover.length, due: turn + 1, holes: leftover});
        }
      }
      p.warnings = [...deferred, ...future];
    }

    if (this.rules.cross_cancel) {
      const mutual = Math.min(outgoing[0], outgoing[1]);
      if (mutual) {
        for (const i of sides) {
          outgoing[i] -= mutual;
          records[i].cancelled += mutual;
        }
      }
    }

    for (const sender of sides) {
      const lines = outgoing[sender], receiver = 1 - sender;
      records[sender].sent = lines;
      this.players[sender].total_sent += lines;
      if (lines) {
        if (this.isV4Plus) {
          const holes = generateHoles(this.#garbage[receiver], lines, this.rules.garbage_messiness);
          this.players[receiver].warnings.push({lines, due: turn + 1, holes});
        } else {
          this.players[receiver].warnings.push({
            lines, due: turn + 1, hole: this.#garbage[receiver].randbelow(WIDTH)
          });
        }
      }
    }

    const solid = turn >= this.rules.overtime_first &&
      (turn - this.rules.overtime_first) % this.rules.overtime_interval === 0;
    for (const i of sides) {
      const p = this.players[i], {reasons} = records[i];
      if (solid && rise(p.board, 1)) reasons.push('solid_overflow');
      if (!fits(p.board.rows, spawn(p.current, this.rules))) reasons.push('next_spawn_blocked');
      p.alive = reasons.length === 0;
      p.hold_used = false;
    }
    const survivors = sides.filter(i => this.players[i].alive);
    const turnLimitHit = Boolean(this.rules.max_turns && turn >= this.rules.max_turns);
    this.finished = survivors.length < 2 || turnLimitHit;
    this.winner = survivors.length === 1 ? survivors[0] : null;
    if (this.finished) {
      if (survivors.length === 1) {
        this.outcome = 'win';
      } else if (survivors.length === 2) {
        this.outcome = 'draw';
      } else {
        const faultSides = Object.entries(this.failures)
          .filter(([, reason]) => isProgramFault(reason))
          .map(([side]) => Number(side));
        if (this.rules.ambiguous_failure_outcome === 'fault_loses' && faultSides.length === 1) {
          this.winner = 1 - faultSides[0];
          this.outcome = 'win';
        } else if (faultSides.length) {
          this.outcome = this.rules.ambiguous_failure_outcome === 'fault_loses'
            ? 'draw' : this.rules.ambiguous_failure_outcome;
        } else {
          this.outcome = 'draw';
        }
      }
    }
    this.turn++;
    this.actions = {};
    this.failures = {};
    return {turn, players: records, solid_risen: Number(solid), finished: this.finished,
      winner: this.winner, outcome: this.outcome};
  }

  snapshot() {
    const v4Plus = this.isV4Plus;
    const rt = this.isRealtime;
    const players = this.players.map(({board, hold_used, warnings, next_frame, pieces_placed, combo_blocks, pending, ...fields}) => {
      const entry = {
        board: board.rows,
        warnings: warnings.map(w => v4Plus
          ? {lines: w.lines, due: w.due, holes: warningHoles(w)}
          : {lines: w.lines, due: w.due, hole: w.hole}),
        ...fields,
      };
      if (rt) {
        entry.next_frame = next_frame;
        entry.pieces_placed = pieces_placed;
        entry.combo_blocks = combo_blocks;
      }
      if (this.hasDelayedLocks && pending !== null) entry.pending = pending;
      return entry;
    });
    const snap = {
      next_turn: this.turn, finished: this.finished, winner: this.winner,
      outcome: this.outcome, players,
    };
    if (rt) {
      snap.frame = this.frame;
      snap.overtime_applied = this.overtime_applied;
    }
    return detached(snap);
  }
}

export async function createDuel(seed, rules = DEFAULT_RULES) {
  if (!Number.isSafeInteger(seed)) throw Error('seed must be a safe integer');
  const supported = ruleProfiles.find(profile => profile.profile === rules?.profile);
  if (!supported || (rules !== supported && canonical(rules) !== canonical(supported))) {
    throw Error('网页仅支持已发布的默认规则配置。');
  }
  const isLegacy = supported === LEGACY_RULES;
  const isV4Plus = [V4_RULES, V5_RULES, V51_RULES, V52_RULES, DEFAULT_RULES].includes(supported);
  const pieceDomains = isLegacy ? ['pieces-v1'] : ['pieces-v2:0', 'pieces-v2:1'];
  const garbageDomains = isV4Plus ? ['garbage-v4:0', 'garbage-v4:1'] : ['garbage-v1'];
  const derive = async domain => BigInt('0x' + await sha256(seed + ':' + domain));
  const [hash, pieceSeeds, rawGarbageSeeds] = await Promise.all([
    fingerprint(supported),
    Promise.all(pieceDomains.map(derive)),
    Promise.all(garbageDomains.map(derive)),
  ]);
  const bagSeeds = isLegacy ? [pieceSeeds[0], pieceSeeds[0]] : pieceSeeds;
  const garbageSeeds = isV4Plus ? rawGarbageSeeds : [rawGarbageSeeds[0], rawGarbageSeeds[0]];
  return new Duel(seed, supported, hash, bagSeeds, garbageSeeds);
}

const routes = [];
for (const rotation of [[], ['CW'], ['ROT180'], ['CCW']]) {
  for (let shift = -5; shift <= 5; shift++) {
    routes.push([...rotation, ...Array(Math.abs(shift)).fill(shift < 0 ? 'LEFT' : 'RIGHT'), 'HARD_DROP']);
  }
}

function evaluate(rows, lines) {
  const base = solidFloorHeight(rows);
  let holes = 0, totalHeight = 0, bumpiness = 0, tallest = 0, previous = 0;
  for (let x = 0; x < WIDTH; x++) {
    let top = HEIGHT;
    while (top > base && rows[top - 1][x] === 0) top--;
    const height = top - base;
    for (let y = base; y < top; y++) if (rows[y][x] === 0) holes++;
    totalHeight += height;
    if (x) bumpiness += Math.abs(previous - height);
    previous = height;
    tallest = Math.max(tallest, height);
  }
  return 1.2 * lines - 0.52 * totalHeight - 2.8 * holes - 0.22 * bumpiness - 0.15 * tallest;
}

function greedy(piece, rows, rules, options) {
  let best = -1e9, commands = ['HARD_DROP'];
  if (!fits(rows, spawn(piece, rules))) return [best, commands];
  for (const route of options) {
    const board = {rows: rows.map(r => r.slice())};
    const lock = execute(board, piece, route, rules);
    const value = evaluate(board.rows, lock.lines);
    if (value > best) {
      best = value;
      commands = route;
    }
  }
  return [best, commands];
}

export function choose(observation, mode, rng, fallbackRules = null) {
  const {self} = observation;
  const rules = observation.rules ?? fallbackRules ?? DEFAULT_RULES;
  if (mode === 'drop') return ['PLAY', ['HARD_DROP']];
  const options = routes.filter(route => route.length <= rules.max_commands);
  if (mode === 'random') {
    if (!options.length || !fits(self.board, spawn(self.current, rules))) return ['PLAY', ['HARD_DROP']];
    return ['PLAY', rng.choice(options).slice()];
  }
  if (mode !== 'greedy') throw Error('unknown strategy');
  const [best, commands] = greedy(self.current, self.board, rules, options);
  if (self.can_hold) {
    const [other] = greedy(self.hold ?? self.next, self.board, rules, options);
    if (other > best + 0.8) return ['HOLD', null];
  }
  return ['PLAY', commands.slice()];
}
