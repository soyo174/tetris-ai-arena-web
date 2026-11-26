import {createDuel, PythonRandom, CELLS} from './engine.mjs';
import {apiFetch} from './api.mjs';
import {createHeader, makeFrame, playOfficialReplay, playTurn, serializeReplay, summarize, validateReplay, replayBotModes, MAX_REPLAY_BYTES, MAX_SERVER_REPLAY_BYTES} from './replay.mjs';

const $ = id => document.getElementById(id);
const NAMES = {greedy: '贪心 AI', random: '随机 AI', drop: '硬降 AI'};
const OFFICIAL_NAMES = {strong: '强 Bot', medium: '中 Bot', weak: '弱 Bot'};
const COLORS = {
  I: '#4fd8f5',
  O: '#f2cf5b',
  T: '#b78bfa',
  S: '#4ade80',
  Z: '#fb7185',
  J: '#60a5fa',
  L: '#fb923c',
  G: '#64748b',
  X: '#475569',
};
let session = null, viewIndex = 0, running = false, timer = null, busy = true, generation = 0;
let activeOfficialId = null;

function feedback(text = '', error = false) {
  const node = $('match-message');
  node.textContent = text;
  node.dataset.tone = error ? 'error' : 'info';
}

function paused() {
  running = false;
  clearTimeout(timer);
  timer = null;
}

function setOfficialActive(id = null) {
  activeOfficialId = id;
  for (const button of document.querySelectorAll('[data-official]')) {
    button.setAttribute('aria-pressed', String(button.dataset.official === id));
  }
}

function colorFrame(frame, previous, record) {
  frame.colors = frame.state.players.map((player, side) => {
    if (!previous || !record) return player.board.map(row => row.map(cell => cell === 2 ? 'G' : cell === 3 ? 'X' : cell ? 'J' : null));
    let colors = previous.colors[side].map(row => [...row]);
    const before = previous.state.players[side].board.map(row => [...row]);
    const lock = record.event.players[side].lock;
    if (lock) {
      const pose = lock.pose;
      for (const [dx, dy] of CELLS[pose.piece][pose.rotation]) {
        const x = pose.x + dx, y = pose.y + dy;
        if (x >= 0 && x < 10 && y >= 0 && y < 24) { colors[y][x] = pose.piece; before[y][x] = 1; }
      }
      colors = colors.filter((_, y) => !before[y].every(cell => cell !== 0 && cell !== 3));
      while (colors.length < 24) colors.push(Array(10).fill(null));
    }
    const rise = Math.min(24, record.event.players[side].risen + record.event.solid_risen);
    colors = [...Array.from({length: rise}, () => Array(10).fill(null)), ...colors].slice(0, 24);
    return player.board.map((row, y) => row.map((cell, x) => cell === 0 ? null : cell === 2 ? 'G' : cell === 3 ? 'X' : (colors[y][x] || 'J')));
  });
  return frame;
}

function block(ctx, x, y, cell, color, {alpha = 1, ghost = false, kind = 'piece', fresh = false} = {}) {
  ctx.save();
  ctx.globalAlpha = alpha;
  const pad = Math.max(1.2, cell * 0.055);
  const px = x * cell + pad;
  const py = y * cell + pad;
  const size = cell - pad * 2;
  const radius = Math.max(2, cell * 0.14);

  ctx.beginPath();
  ctx.roundRect(px, py, size, size, radius);

  if (ghost) {
    ctx.fillStyle = color + '1f';
    ctx.fill();
    ctx.strokeStyle = color + '99';
    ctx.lineWidth = 1.5;
    ctx.setLineDash([3, 2]);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
    return;
  }

  ctx.fillStyle = color;
  ctx.fill();

  if (kind === 'solid') {
    ctx.strokeStyle = 'rgba(226, 232, 240, 0.32)';
    ctx.lineWidth = 1;
    ctx.stroke();
    ctx.strokeStyle = 'rgba(15, 23, 42, 0.55)';
    ctx.beginPath();
    ctx.moveTo(px + 3, py + size - 3);
    ctx.lineTo(px + size - 3, py + 3);
    ctx.stroke();
  } else if (kind === 'garbage') {
    ctx.fillStyle = 'rgba(255, 255, 255, 0.12)';
    ctx.fillRect(px + 2, py + 2, size - 4, Math.max(2, size * 0.18));
    ctx.fillStyle = 'rgba(15, 23, 42, 0.35)';
    ctx.fillRect(px + 2, py + size - 4, size - 4, 2.5);
  } else {
    const grad = ctx.createLinearGradient(px, py, px, py + size);
    grad.addColorStop(0, 'rgba(255, 255, 255, 0.32)');
    grad.addColorStop(0.45, 'rgba(255, 255, 255, 0.04)');
    grad.addColorStop(1, 'rgba(0, 0, 0, 0.24)');
    ctx.fillStyle = grad;
    ctx.fill();

    ctx.fillStyle = 'rgba(255, 255, 255, 0.38)';
    ctx.fillRect(px + 3, py + 2, size - 6, Math.max(1.5, cell * 0.08));

    if (fresh) {
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.85)';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }
  }
  ctx.restore();
}

function drawMini(id, piece) {
  const canvas = $(id), ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (!piece) {
    ctx.fillStyle = '#536788';
    ctx.font = '600 15px system-ui';
    ctx.textAlign = 'center';
    ctx.fillText('—', canvas.width / 2, canvas.height / 2 + 5);
    canvas.setAttribute('aria-label', '空的暂存格');
    return;
  }
  const cells = CELLS[piece][0], size = 21;
  const minX = Math.min(...cells.map(c => c[0])), maxX = Math.max(...cells.map(c => c[0]));
  const minY = Math.min(...cells.map(c => c[1])), maxY = Math.max(...cells.map(c => c[1]));
  ctx.save();
  ctx.translate((canvas.width - (maxX - minX + 1) * size) / 2, (canvas.height - (maxY - minY + 1) * size) / 2);
  for (const [x, y] of cells) block(ctx, x - minX, maxY - y, size, COLORS[piece]);
  ctx.restore();
  canvas.setAttribute('aria-label', `${piece} 方块`);
}

function drawBoard(side, frame) {
  const letter = side ? 'b' : 'a', canvas = $(`board-${letter}`), ctx = canvas.getContext('2d');
  const cell = canvas.width / 10, rows = frame.state.players[side].board, obs = frame.observations[side];
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Subtle vertical background depth
  const bgGrad = ctx.createLinearGradient(0, 0, 0, canvas.height);
  bgGrad.addColorStop(0, '#070d1b');
  bgGrad.addColorStop(1, '#0b1326');
  ctx.fillStyle = bgGrad;
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // Spawn buffer shading (top 4 rows)
  ctx.fillStyle = side ? 'rgba(179, 146, 255, 0.08)' : 'rgba(94, 208, 255, 0.08)';
  ctx.fillRect(0, 0, canvas.width, cell * 4);

  // Crisp grid lines
  ctx.strokeStyle = 'rgba(148, 179, 232, 0.085)';
  ctx.lineWidth = 1;
  for (let x = 1; x < 10; x++) {
    ctx.beginPath();
    ctx.moveTo(x * cell + 0.5, 0);
    ctx.lineTo(x * cell + 0.5, canvas.height);
    ctx.stroke();
  }
  for (let y = 1; y < 24; y++) {
    ctx.beginPath();
    ctx.moveTo(0, y * cell + 0.5);
    ctx.lineTo(canvas.width, y * cell + 0.5);
    ctx.stroke();
  }

  // Highlight cells from the latest locked piece (if not cleared/shifted away)
  const freshCells = new Set();
  const lock = frame.event?.players?.[side]?.lock;
  if (lock && !lock.lines && !(frame.event.players[side].risen || frame.event.solid_risen)) {
    const pose = lock.pose;
    for (const [dx, dy] of CELLS[pose.piece][pose.rotation]) {
      freshCells.add(`${pose.x + dx},${pose.y + dy}`);
    }
  }

  // Locked blocks
  for (let y = 0; y < 24; y++) {
    for (let x = 0; x < 10; x++) {
      const cellVal = rows[y][x];
      if (!cellVal) continue;
      const colorKey = frame.colors[side][y][x];
      const kind = cellVal === 3 ? 'solid' : cellVal === 2 ? 'garbage' : 'piece';
      block(ctx, x, 23 - y, cell, COLORS[colorKey] || '#60a5fa', {
        kind,
        fresh: kind === 'piece' && freshCells.has(`${x},${y}`),
      });
    }
  }

  // Active spawning piece + ghost landing preview
  if (!frame.state.finished) {
    const piece = obs.self.current, cells = CELLS[piece][0], [ox, oy] = obs.rules.spawn_origins[piece];
    const fits = y => cells.every(([dx, dy]) => ox + dx >= 0 && ox + dx < 10 && y + dy >= 0 && y + dy < 24 && rows[y + dy][ox + dx] === 0);
    if (fits(oy)) {
      let gy = oy;
      while (fits(gy - 1)) gy--;
      for (const [dx, dy] of cells) block(ctx, ox + dx, 23 - (gy + dy), cell, COLORS[piece], {ghost: true});
      for (const [dx, dy] of cells) block(ctx, ox + dx, 23 - (oy + dy), cell, COLORS[piece], {alpha: 0.72});
    }
  }

  // Spawn buffer divider line
  ctx.strokeStyle = side ? 'rgba(179, 146, 255, 0.48)' : 'rgba(94, 208, 255, 0.48)';
  ctx.setLineDash([6, 4]);
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(0, 4 * cell);
  ctx.lineTo(canvas.width, 4 * cell);
  ctx.stroke();
  ctx.setLineDash([]);

  // Left-edge vertical incoming garbage indicator bar on canvas
  const warning = obs.self.warnings.reduce((sum, packet) => sum + packet.lines, 0);
  if (warning > 0) {
    const barHeight = Math.min(20, warning) * cell;
    ctx.fillStyle = warning >= 6 ? 'rgba(251, 113, 133, 0.85)' : 'rgba(251, 191, 36, 0.78)';
    ctx.fillRect(0, canvas.height - barHeight, 4, barHeight);
  }

  canvas.setAttribute('aria-label', `选手 ${letter.toUpperCase()} 的棋盘，10 列 24 行，第 ${frame.state.next_turn - 1} 回合`);
}

const eliminationReasons = new Map([
  ['next_spawn_blocked', '下一块出生受阻'],
  ['hold_spawn_blocked', '暂存后出生受阻'],
  ['spawn_blocked', '出生受阻'],
  ['garbage_overflow', '垃圾顶出'],
  ['solid_overflow', '加时实心行顶出'],
  ['thinking_timeout', 'Bot 响应超时'],
  ['compile_error', 'Bot 编译失败'],
  ['compile_timeout', 'Bot 编译超时'],
  ['bot_eof', 'Bot 提前退出'],
  ['bot_input_unavailable', 'Bot 无法接收观察'],
  ['response_too_large', 'Bot 回复超过大小限制'],
  ['invalid JSON response', 'Bot 回复不是有效的 JSON'],
]);

function description(event) {
  const messages = [];
  event.players.forEach((player, side) => {
    const who = side ? 'B 方' : 'A 方';
    if (player.lock?.lines) messages.push(`${who}${player.lock.spin === 'full' ? ' T 旋' : player.lock.spin === 'mini' ? ' Mini' : ''}消除 ${player.lock.lines} 行${player.lock.perfect_clear ? ' · 全消' : ''}`);
    if (player.sent) messages.push(`${who}发出 ${player.sent} 行攻击`);
    if (player.cancelled) messages.push(`${who}抵消 ${player.cancelled} 行`);
    if (player.risen) messages.push(`${who}升起 ${player.risen} 行垃圾`);
    if (player.reasons.length) {
      const reasons = player.reasons.map(reason => eliminationReasons.get(reason) ?? `Bot 失误（${reason}）`);
      messages.push(`${who}出局 · ${reasons.join('、')}`);
    }
  });
  if (event.solid_risen) messages.push('加时：双方升起 1 行实心行');
  if (!messages.length) messages.push('双方完成落块');
  return messages.join('；');
}

function turnInstructions(decision) {
  const details = document.createElement('details');
  details.className = 'turn-instructions';
  const summary = document.createElement('summary');
  summary.textContent = '查看本手双方指令';
  details.append(summary);
  for (const [side, hold] of decision.holds.entries()) {
    const row = document.createElement('p'), route = document.createElement('code');
    row.append(side ? 'B 方：' : 'A 方：');
    const commands = [...(hold ? ['HOLD'] : []), ...(decision.actions[side] ?? [])];
    route.textContent = commands.length ? commands.join(' · ') : '无有效走法';
    row.append(route);
    details.append(row);
  }
  return details;
}

function outcomeText(state) {
  return state.outcome === 'no_contest' ? '本局取消' : state.winner === null ? '双方平局' : `${state.winner ? 'B 方' : 'A 方'}获胜`;
}

function canAdvance() { return session && (viewIndex < session.frames.length - 1 || (session.kind === 'live' && !session.game.finished)); }
function canRewind() { return session && viewIndex > 0; }

function render() {
  $('start-match').disabled = busy;
  $('import-replay').disabled = busy;
  $('restart-match').disabled = busy || !session;
  $('export-replay').disabled = busy || !session || !session.turns.length;
  $('pause-match').disabled = busy || !canAdvance();
  $('step-match').disabled = busy || !canAdvance();
  if ($('step-back')) $('step-back').disabled = busy || !canRewind();
  $('pause-label').textContent = running ? '暂停' : session?.kind === 'replay' ? '播放回放' : '继续';
  $('pause-match').dataset.paused = String(!running);
  $('pause-match').setAttribute('aria-label', running ? '暂停比赛' : '继续比赛或回放');
  $('timeline').disabled = busy || !session || session.frames.length < 2;
  if (!session) {
    $('match-status').textContent = '准备棋盘中';
    $('match-status').dataset.state = 'ready';
    return;
  }
  const frame = session.frames[viewIndex], turn = frame.state.next_turn - 1;
  const partialEnd = session.kind === 'replay' && !session.result && viewIndex === session.frames.length - 1;
  $('turn').textContent = turn;
  $('timeline').max = session.frames.length - 1;
  $('timeline').value = viewIndex;
  $('timeline-value').textContent = `${turn} / ${session.frames.length - 1} 回合`;

  let status, state;
  if (busy) { status = '正在读取回放'; state = 'ready'; }
  else if (frame.state.finished) { status = outcomeText(frame.state); state = 'finished'; }
  else if (partialEnd) { status = '阶段回放已结束'; state = 'replay'; }
  else if (session.kind === 'replay') { status = running ? '回放播放中' : '回放已暂停'; state = 'replay'; }
  else if (running) { status = '对战进行中'; state = 'running'; }
  else { status = session.demo ? '演示局 · 已暂停' : '比赛已暂停'; state = 'paused'; }

  $('match-status').textContent = status;
  $('match-status').dataset.state = state;

  const historyNote = partialEnd
    ? (session.turns.length ? '尚无赛果，可拖动时间轴查看' : '仅含初始棋盘')
    : viewIndex < session.frames.length - 1
      ? '正在回看历史回合'
      : session.demo
        ? '本地演算演示'
        : session.kind === 'replay'
          ? '裁判重算校验通过'
          : '独立七袋 · 同步结算';
  $('match-detail').textContent = `种子 ${session.seed} · 裁判 ${session.header.engine_version} · ${historyNote}`;

  for (let side = 0; side < 2; side++) {
    const letter = side ? 'b' : 'a', player = frame.state.players[side], own = frame.observations[side].self;
    $('bot-name-' + letter).textContent = session.names[side];
    for (const [id, value] of Object.entries({
      lines: player.total_lines,
      sent: player.total_sent,
      combo: player.chain.combo,
      b2b: player.chain.b2b ? 'ON' : '—',
    })) {
      $(id + '-' + letter).textContent = value;
    }

    const warning = own.warnings.reduce((sum, packet) => sum + packet.lines, 0);
    $('warning-' + letter).textContent = warning;
    $('warning-meter-' + letter).value = Math.min(24, warning);
    $('warning-meter-' + letter).title = warning ? `${warning} 行来袭，下一回合结算` : '暂无来袭垃圾';
    const incomingBox = $('incoming-' + letter);
    if (incomingBox) incomingBox.dataset.active = String(warning > 0);

    const card = $('player-card-' + letter);
    if (card) {
      if (frame.state.finished) {
        card.dataset.outcome = frame.state.winner === side ? 'winner' : frame.state.winner === null ? 'draw' : 'loser';
      } else {
        delete card.dataset.outcome;
      }
    }

    const label = $('player-state-' + letter);
    if (label) {
      if (frame.state.finished) {
        if (frame.state.winner === side) { label.textContent = '获胜'; label.dataset.tone = 'win'; }
        else if (frame.state.winner === null) { label.textContent = '平局'; label.dataset.tone = 'draw'; }
        else { label.textContent = '出局'; label.dataset.tone = 'lose'; }
      } else if (warning > 0) {
        label.textContent = `来袭 ${warning} 行`;
        label.dataset.tone = 'danger';
      } else {
        label.textContent = running ? '对战中' : '';
        delete label.dataset.tone;
      }
    }

    drawBoard(side, frame);
    drawMini('current-' + letter, own.current);
    drawMini('hold-' + letter, own.hold);
    drawMini('next-' + letter, own.next);
  }

  const feed = $('event-feed');
  feed.replaceChildren();
  const events = session.frames.slice(1, viewIndex + 1).slice(-12).reverse();
  if (!events.length) {
    const li = document.createElement('li');
    li.className = 'empty-event';
    li.textContent = '两位选手已就位，点击“播放”或按空格键开始观战。';
    feed.append(li);
  }
  for (const {event} of events) {
    const li = document.createElement('li');
    const row = document.createElement('div');
    row.className = 'event-row';
    const time = document.createElement('span');
    time.className = 'event-turn';
    time.textContent = `T${String(event.turn).padStart(3, '0')}`;
    const detail = document.createElement('span');
    detail.className = 'event-description';
    detail.textContent = description(event);
    row.append(time, detail);
    li.append(row);

    const hasKo = event.players.some(p => p.reasons.length > 0);
    li.classList.add('event-interactive');
    if (event.turn === turn) li.classList.add('event-current');
    if (hasKo) li.classList.add('event-ko');
    li.addEventListener('click', e => {
      if (e.target.closest('details')) return;
      if (busy || !session) return;
      paused();
      viewIndex = Math.max(0, Math.min(session.frames.length - 1, event.turn));
      render();
    });

    if (event.turn === turn && session.turns[viewIndex - 1]) {
      li.append(turnInstructions(session.turns[viewIndex - 1].decision));
    }
    feed.append(li);
  }
}

function addTurn(target) {
  const record = playTurn(target.game, target.modes, target.rngs);
  target.turns.push(record);
  target.frames.push(colorFrame(makeFrame(target.game, record.event), target.frames.at(-1), record));
  if (target.game.finished) target.result = summarize(target.game, record.event);
}

function advance() {
  if (busy || !canAdvance()) return false;
  if (viewIndex < session.frames.length - 1) viewIndex++;
  else { addTurn(session); viewIndex = session.frames.length - 1; session.demo = false; }
  if (!canAdvance()) paused();
  render();
  return true;
}

function rewind() {
  if (busy || !canRewind()) return false;
  paused();
  viewIndex = Math.max(0, viewIndex - 1);
  render();
  return true;
}

function tick() {
  if (!running) return;
  const token = generation;
  timer = setTimeout(() => {
    if (!running || token !== generation) return;
    try { advance(); if (running) tick(); }
    catch (error) { paused(); feedback(`本局暂停：${error.message}`, true); render(); }
  }, 650 / Number($('speed').value || 1));
}

function resume() {
  if (busy || !canAdvance()) return;
  paused(); running = true; session.demo = false; feedback(); render(); tick();
}

async function newMatch(seed, modes, {autoplay = true, demo = false} = {}) {
  const token = ++generation;
  setOfficialActive(null);
  paused(); busy = true; feedback(); render();
  try {
    const game = await createDuel(seed);
    if (token !== generation) return;
    const target = {
      kind: 'live', seed, modes, names: modes.map(mode => NAMES[mode]), game,
      rngs: [new PythonRandom(0), new PythonRandom(0)], header: createHeader(game, modes),
      frames: [colorFrame(makeFrame(game))], turns: [], result: null, demo,
    };
    if (demo) for (let i = 0; i < 24 && !game.finished; i++) addTurn(target);
    session = target; viewIndex = target.frames.length - 1; busy = false; render();
    if (autoplay) resume();
  } catch (error) {
    if (token === generation) { busy = false; feedback(`无法开始比赛：${error.message}`, true); render(); }
  }
}

function readSettings() {
  const raw = $('seed').value.trim();
  if (!/^[+-]?\d+$/.test(raw) || !Number.isSafeInteger(Number(raw))) throw new Error('请输入有效的整数种子，范围为 ±9007199254740991。');
  return {seed: Number(raw), modes: [$('bot-a').value, $('bot-b').value]};
}

$('start-match').addEventListener('click', () => {
  try {
    const {seed, modes} = readSettings();
    try { localStorage.setItem('tetra-arena-settings', JSON.stringify({seed, modes})); } catch { /* optional browser persistence */ }
    newMatch(seed, modes);
  } catch (error) { feedback(error.message, true); }
});

if ($('random-seed')) {
  $('random-seed').addEventListener('click', () => {
    $('seed').value = String(Math.floor(Math.random() * 999999) + 1);
  });
}

$('pause-match').addEventListener('click', () => { if (running) { paused(); render(); } else resume(); });
$('step-match').addEventListener('click', () => { paused(); feedback(); try { advance(); } catch (error) { feedback(error.message, true); render(); } });
if ($('step-back')) {
  $('step-back').addEventListener('click', () => { feedback(); rewind(); });
}
$('restart-match').addEventListener('click', () => {
  if (!session || busy) return;
  if (session.kind === 'replay') { paused(); viewIndex = 0; feedback(); render(); }
  else newMatch(session.seed, session.modes, {autoplay: false});
});
$('speed').addEventListener('change', () => { if (running) { clearTimeout(timer); tick(); } });
$('timeline').addEventListener('input', () => {
  if (!session || busy) return;
  paused();
  viewIndex = Math.max(0, Math.min(session.frames.length - 1, Number($('timeline').value)));
  render();
});
$('seed').addEventListener('keydown', event => { if (event.key === 'Enter' && !busy) { $('start-match').click(); } });
$('rules-open').addEventListener('click', () => $('rules-dialog').showModal());
$('rules-dialog').addEventListener('click', event => {
  if (event.target === $('rules-dialog')) {
    const r = $('rules-dialog').getBoundingClientRect();
    if (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom) $('rules-dialog').close();
  }
});
$('import-replay').addEventListener('click', () => $('replay-file').click());

function acceptReplay(replay, names = null) {
  replay.frames.forEach((frame, i) => colorFrame(frame, i ? replay.frames[i - 1] : null, i ? replay.turns[i - 1] : null));
  const modes = replayBotModes(replay.header);
  session = {
    ...replay, kind: 'replay', seed: replay.header.seed, modes,
    names: names || modes.map((mode, side) => NAMES[mode] || `外部 AI ${side ? 'B' : 'A'}`), demo: false,
  };
  viewIndex = 0; busy = false;
  feedback(replay.turns.length ? `已校验 ${replay.turns.length} 回合${replay.result ? '完整' : '阶段'}回放。` : '已校验初始棋盘，此文件尚未记录任何回合。');
  render();
}

async function loadLadderReplay(matchId) {
  if (!/^[a-f0-9]{32}$/.test(matchId)) { feedback('天梯对局编号无效，请从排行榜重新打开。', true); return; }
  const token = ++generation;
  setOfficialActive(null);
  paused(); busy = true; feedback('正在下载并重算天梯对局…'); render();
  try {
    const [metadataResponse, replayResponse] = await Promise.all([
      apiFetch(`/api/matches/${matchId}`),
      apiFetch(`/api/matches/${matchId}/replay`),
    ]);
    if (!metadataResponse.ok || !replayResponse.ok) throw new Error('无法获取该对局，请确认天梯服务正在运行。');
    if (Number(replayResponse.headers.get('content-length')) > MAX_SERVER_REPLAY_BYTES) throw new Error('服务器回放超过 32 MiB。');
    const metadata = await metadataResponse.json();
    const replay = await validateReplay(await replayResponse.text(), {maxBytes: MAX_SERVER_REPLAY_BYTES});
    if (token !== generation) return;
    if (!replay.result) throw new Error('天梯比赛缺少完整赛果。');
    acceptReplay(replay, [metadata.a, metadata.b].map(bot => `${bot.name} · v${bot.version}`));
    if (metadata.practice) {
      session.practiceMatch = matchId;
      if (document.hidden) concealPracticeReplay();
    }
    feedback(`天梯对局已通过 ${replay.turns.length} 回合重算校验，点击播放开始观看。`);
  } catch (error) {
    if (token !== generation) return;
    busy = false; render(); feedback(`载入天梯回放失败：${error.message}`, true);
  }
}

let officialReplays;
async function loadOfficialReplay(id, autoplay) {
  const token = ++generation;
  setOfficialActive(id);
  paused(); busy = true; feedback('正在校验官方 Bot 回放…'); render();
  try {
    officialReplays ??= fetch(new URL('./examples/official-replays.json', import.meta.url)).then(response => {
      if (!response.ok) throw new Error('官方回放暂不可用。');
      return response.json();
    }).catch(error => { officialReplays = null; throw error; });
    const match = (await officialReplays).find(entry => entry.id === id);
    if (!match) throw new Error('没有找到这组官方 Bot 回放。');
    const replay = await playOfficialReplay(match);
    if (token !== generation) return;
    acceptReplay(replay, id.split('-').map(tier => OFFICIAL_NAMES[tier]));
    if (autoplay) resume();
    feedback(`官方 Bot 实战回放 · ${replay.turns.length} 回合`);
  } catch (error) {
    if (token !== generation) return;
    setOfficialActive(null);
    busy = false; render(); feedback(error.message, true);
  }
}

for (const button of document.querySelectorAll('[data-official]')) {
  button.addEventListener('click', () => void loadOfficialReplay(button.dataset.official, true));
}

$('replay-file').addEventListener('change', async event => {
  const file = event.target.files?.[0]; event.target.value = ''; if (!file) return;
  if (file.size > MAX_REPLAY_BYTES) { feedback('回放文件过大，请选择不超过 8 MB 的 JSONL 文件。', true); return; }
  const token = ++generation, wasRunning = running;
  setOfficialActive(null);
  paused(); busy = true; feedback('正在重新计算并校验回放…'); render();
  try {
    const replay = await validateReplay(await file.text());
    if (token !== generation) return;
    acceptReplay(replay);
  } catch (error) {
    if (token !== generation) return;
    busy = false; render(); if (wasRunning) resume(); feedback(`导入失败：${error.message}`, true);
  }
});

$('export-replay').addEventListener('click', () => {
  if (!session || !session.turns.length) return;
  const text = serializeReplay(session.header, session.turns, session.result);
  const url = URL.createObjectURL(new Blob([text], {type: 'application/x-ndjson'}));
  const anchor = document.createElement('a'); anchor.href = url;
  anchor.download = `tetra_seed${session.seed}_${session.turns.length}turns${session.result ? '' : '_partial'}.jsonl`;
  document.body.append(anchor); anchor.click(); anchor.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  feedback(`已导出 ${session.turns.length} 回合${session.result ? '完整' : '阶段'}回放。`);
});

// Keyboard shortcuts for ergonomic replay inspection
document.addEventListener('keydown', event => {
  if (event.ctrlKey || event.metaKey || event.altKey || busy || $('rules-dialog').open) return;
  if (event.target.closest('input, select, button, textarea, a')) return;
  if (event.code === 'Space') {
    event.preventDefault();
    $('pause-match').click();
  } else if (event.key === 'ArrowRight') {
    event.preventDefault();
    $('step-match').click();
  } else if (event.key === 'ArrowLeft') {
    event.preventDefault();
    if ($('step-back')) $('step-back').click();
  } else if (event.key === 'Home' && session) {
    event.preventDefault();
    paused(); viewIndex = 0; render();
  } else if (event.key === 'End' && session) {
    event.preventDefault();
    paused(); viewIndex = session.frames.length - 1; render();
  } else if (event.key === 'r' || event.key === 'R') {
    event.preventDefault();
    $('restart-match').click();
  } else if (['1', '2', '3', '4'].includes(event.key)) {
    const speeds = ['1', '2', '4', '8'];
    $('speed').value = speeds[Number(event.key) - 1];
    $('speed').dispatchEvent(new Event('change'));
  }
});

function concealPracticeReplay() {
  if (session?.practiceMatch) document.body.classList.add('checking-practice-access');
}

async function restorePracticeAccess() {
  if (!session?.practiceMatch || document.hidden || !document.body.classList.contains('checking-practice-access')) return;
  const current = session;
  let allowed = false;
  try {
    const response = await apiFetch(`/api/matches/${current.practiceMatch}`, {
      cache: 'no-store', signal: AbortSignal.timeout(5000),
    });
    allowed = response.ok;
  } catch { /* Keep the replay private if the service cannot recheck access. */ }
  if (session !== current || document.hidden) return;
  if (!allowed) {
    session = null;
    await newMatch(7, ['greedy', 'greedy'], {autoplay: false, demo: true});
    feedback('当前无法访问此自定义回放，请登录发起对局的账号后重新打开。', true);
  }
  document.body.classList.remove('checking-practice-access');
}

window.addEventListener('pagehide', concealPracticeReplay);
window.addEventListener('pageshow', () => void restorePracticeAccess());
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    if (running) { paused(); render(); }
    concealPracticeReplay();
  } else void restorePracticeAccess();
});

try {
  const stored = JSON.parse(localStorage.getItem('tetra-arena-settings') || 'null');
  if (stored && Number.isSafeInteger(stored.seed) && stored.modes?.length === 2 && stored.modes.every(mode => NAMES[mode])) {
    $('seed').value = stored.seed; $('bot-a').value = stored.modes[0]; $('bot-b').value = stored.modes[1];
  }
} catch { /* default selectors stay usable */ }
const requestedMatch = new URLSearchParams(location.search).get('match');
await newMatch(7, ['greedy', 'greedy'], {autoplay: false, demo: !requestedMatch});
if (requestedMatch) await loadLadderReplay(requestedMatch);
else await loadOfficialReplay('strong-medium', false);
