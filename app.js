import {createDuel, CELLS, traceTSpin} from './engine.mjs?v=0.5.3-local';
import {apiFetch} from './api.mjs';
import {LocalBot} from './local-bots.mjs';
import {createHeader, makeFrame, playOfficialReplay, serializeReplay, summarize, validateReplay, replayBotModes, MAX_REPLAY_BYTES, MAX_SERVER_REPLAY_BYTES, CURRENT_ENGINE_VERSION} from './replay.mjs?v=0.5.3-local';

const $ = id => document.getElementById(id);
const NAMES = {weak: '弱 Bot', medium: '中 Bot', strong: '强 Bot', neno: 'neno', northstar: '北极星'};
const DEFAULT_BOTS = ['neno', 'northstar'];
const SEAT_INPUTS = ['a', 'b'];
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
let advancing = null;
let displayedFrame = null, spinAnimation = 0;
const SPIN_DURATION = 340;
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');

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

function drawBoard(side, frame, spin = null) {
  const letter = side ? 'b' : 'a', canvas = $(`board-${letter}`), ctx = canvas.getContext('2d');
  const cell = canvas.width / 10, obs = frame.observations[side];
  const rows = spin?.trace.rows ?? frame.state.players[side].board;
  const colors = spin?.colors ?? frame.colors[side];
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
  if (!spin && lock && !lock.lines && !(frame.event.players[side].risen || frame.event.solid_risen)) {
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
      const colorKey = cellVal === 3 ? 'X' : cellVal === 2 ? 'G' : colors[y]?.[x];
      const kind = cellVal === 3 ? 'solid' : cellVal === 2 ? 'garbage' : 'piece';
      block(ctx, x, 23 - y, cell, COLORS[colorKey] || '#60a5fa', {
        kind,
        fresh: kind === 'piece' && freshCells.has(`${x},${y}`),
      });
    }
  }

  if (spin) {
    const {from, to, quarterTurns} = spin.trace;
    const progress = 1 - (1 - spin.progress) ** 2;
    const x = from.x + (to.x - from.x) * progress;
    const y = from.y + (to.y - from.y) * progress;
    ctx.save();
    ctx.translate((x + 1.5) * cell, (22.5 - y) * cell);
    ctx.rotate(quarterTurns * Math.PI / 2 * progress);
    for (const [dx, dy] of CELLS.T[from.rotation]) {
      block(ctx, dx - 1.5, 0.5 - dy, cell, COLORS.T, {fresh: true});
    }
    ctx.restore();
  } else if (!frame.state.finished) {
    const piece = obs.self.current, cells = CELLS[piece][0];
    const spawnOrigins = (obs.rules ?? session.header.rules).spawn_origins;
    const [ox, oy] = spawnOrigins[piece];
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

function isRealtimeSession(target = session) {
  return Boolean(target?.header?.rules?.fps);
}

function formatClock(frame, fps = 60) {
  const totalSec = Math.max(0, frame) / fps;
  const mins = Math.floor(totalSec / 60);
  const secs = totalSec - mins * 60;
  return `${String(mins).padStart(2, '0')}:${secs.toFixed(1).padStart(4, '0')}`;
}

function description(event) {
  const messages = [];
  const active = event.active ?? [0, 1];
  event.players.forEach((player, side) => {
    const who = side ? 'B 方' : 'A 方';
    const spin = spinTitle(player.lock);
    if (spin) messages.push(`${who} ${spin}`);
    else if (player.lock?.lines) messages.push(`${who}消除 ${player.lock.lines} 行${player.lock.perfect_clear ? ' · 全消' : ''}`);
    if (player.sent) messages.push(`${who}发出 ${player.sent} 行攻击`);
    if (player.cancelled) messages.push(`${who}抵消 ${player.cancelled} 行`);
    if (player.combo_blocked) messages.push(`${who}连消阻断垃圾`);
    if (player.risen) messages.push(`${who}升起 ${player.risen} 行垃圾`);
    if (player.reasons.length) {
      const reasons = player.reasons.map(reason => eliminationReasons.get(reason) ?? `Bot 失误（${reason}）`);
      messages.push(`${who}出局 · ${reasons.join('、')}`);
    }
  });
  if (event.solid_risen) messages.push(`加时：双方升起 ${event.solid_risen} 行实心行`);
  if (!messages.length) {
    if (event.frame !== undefined && active.length === 0) {
      messages.push(event.finished ? '达到比赛时限' : '等待落块');
    } else if (event.frame !== undefined && active.length === 1) {
      const side = active[0], f = event.players[side].frames;
      messages.push(`${side ? 'B 方' : 'A 方'}落块${f ? `（${f} 帧）` : ''}`);
    } else {
      messages.push('双方完成落块');
    }
  }
  return messages.join('；');
}

function turnInstructions({decision, event}) {
  const details = document.createElement('details');
  details.className = 'turn-instructions';
  const summary = document.createElement('summary');
  summary.textContent = '查看本手双方指令';
  details.append(summary);
  for (const [side, hold] of decision.holds.entries()) {
    const row = document.createElement('p'), route = document.createElement('code');
    row.append(side ? 'B 方：' : 'A 方：');
    const player = event.players[side];
    const settled = Object.hasOwn(player, 'hold_used');
    const commands = [...((settled ? player.hold_used : hold) ? ['HOLD'] : []),
      ...(settled ? player.commands ?? [] : decision.actions[side] ?? [])];
    route.textContent = commands.length ? commands.join(' · ') : '无有效走法';
    row.append(route);
    details.append(row);
  }
  return details;
}

function outcomeText(state) {
  return state.outcome === 'no_contest' ? '本局取消' : state.winner === null ? '双方平局' : `${state.winner ? 'B 方' : 'A 方'}获胜`;
}

function spinTitle(lock) {
  if (!lock || lock.spin === 'none') return '';
  const name = lock.spin === 'mini' ? 'Mini T-Spin' : 'T-Spin';
  const clear = ['', '单消', '双消', '三消'][lock.lines];
  return clear ? `${name} · ${clear}` : name;
}

function renderBoards(frame, replaySide = null) {
  if (displayedFrame === frame && replaySide === null) return;
  displayedFrame = frame;
  cancelAnimationFrame(spinAnimation);
  const previous = session.frames[viewIndex - 1];
  const rules = session.header.rules;
  const solidRisen = rules.fps ? frame.event?.solid_risen ?? 0 : 0;
  const spins = frame.state.players.map((player, side) => {
    const label = $(side ? 'spin-b' : 'spin-a');
    const record = frame.event?.players[side];
    const title = spinTitle(record?.lock);
    label.textContent = title;
    const trace = title && previous
      ? traceTSpin(previous.state.players[side].board, record, rules, solidRisen)
      : null;
    label.hidden = !trace;
    if (!trace) return null;
    label.setAttribute('aria-label', '重播 ' + (side ? 'B' : 'A') + ' 方的 ' + title + ' 旋转');
    label.title = '点击重播旋转';
    if (replaySide === null ? reducedMotion.matches : replaySide !== side) return null;
    const beforeColors = previous.colors[side];
    return {trace, colors: [...Array(solidRisen).fill(null), ...beforeColors]};
  });
  for (const [side, spin] of spins.entries()) {
    drawBoard(side, frame, spin && {...spin, progress: 0});
  }
  if (!spins.some(Boolean)) return;

  const start = performance.now();
  function animate(now) {
    const progress = Math.max(0, (now - start) / SPIN_DURATION);
    for (const [side, spin] of spins.entries()) {
      if (spin) drawBoard(side, frame, progress < 1 ? {...spin, progress} : null);
    }
    if (progress < 1) spinAnimation = requestAnimationFrame(animate);
  }
  spinAnimation = requestAnimationFrame(animate);
}

function canAdvance() { return session && (viewIndex < session.frames.length - 1 || (session.kind === 'live' && !session.game.finished)); }
function canRewind() { return session && viewIndex > 0; }

function render() {
  $('start-match').disabled = busy;
  $('import-replay').disabled = busy;
  $('restart-match').disabled = busy || !session;
  $('export-replay').disabled = busy || !session || !session.turns.length;
  const calculating = advancing === session && session !== null;
  $('pause-match').disabled = busy || !canAdvance() || (calculating && !running);
  $('step-match').disabled = busy || !canAdvance() || calculating;
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
  const realtime = isRealtimeSession();
  const fps = session.header.rules.fps || 60;
  const curClockFrame = frame.event?.frame ?? frame.state.frame ?? 0;
  const totalClockFrame = session.frames.at(-1)?.event?.frame ?? session.frames.at(-1)?.state?.frame ?? curClockFrame;
  const partialEnd = session.kind === 'replay' && !session.result && viewIndex === session.frames.length - 1;
  if ($('round-label')) $('round-label').textContent = realtime ? 'TIME' : 'TURN';
  $('turn').textContent = realtime ? formatClock(curClockFrame, fps) : turn;
  $('timeline').max = session.frames.length - 1;
  $('timeline').value = viewIndex;
  $('timeline-value').textContent = realtime
    ? `${formatClock(curClockFrame, fps)} / ${formatClock(totalClockFrame, fps)} · ${turn} 步`
    : `${turn} / ${session.frames.length - 1} 回合`;

  let status, state;
  if (busy) { status = '正在读取回放'; state = 'ready'; }
  else if (frame.state.finished) { status = outcomeText(frame.state); state = 'finished'; }
  else if (partialEnd) { status = '阶段回放已结束'; state = 'replay'; }
  else if (session.kind === 'replay') { status = running ? '回放播放中' : '回放已暂停'; state = 'replay'; }
  else if (running || calculating) { status = '本地对战进行中'; state = 'running'; }
  else { status = session.turns.length ? '本地对战已暂停' : '本地对战已就绪'; state = 'paused'; }

  $('match-status').textContent = status;
  $('match-status').dataset.state = state;

  const historyNote = partialEnd
    ? (session.turns.length ? '尚无赛果，可拖动时间轴查看' : '仅含初始棋盘')
    : viewIndex < session.frames.length - 1
      ? '正在回看历史回合'
      : session.kind === 'replay'
        ? '裁判重算校验通过'
        : '浏览器本地运行';
  $('match-detail').textContent = `种子 ${session.seed} · 裁判 v${CURRENT_ENGINE_VERSION} · ${historyNote}`;

  for (let side = 0; side < 2; side++) {
    const letter = side ? 'b' : 'a', player = frame.state.players[side], own = frame.observations[side].self;
    $('bot-name-' + letter).textContent = session.names[side];
    const paceEl = $('pace-' + letter);
    if (paceEl) {
      if (realtime && curClockFrame > 0) {
        const secs = curClockFrame / fps;
        const pps = ((player.pieces_placed || 0) / secs).toFixed(2);
        const apm = ((player.total_sent * 60) / secs).toFixed(1);
        paceEl.textContent = `${pps} PPS · ${apm} APM`;
      } else {
        paceEl.textContent = realtime ? '0.00 PPS · 0.0 APM' : '';
      }
    }
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

    drawMini('current-' + letter, own.current);
    drawMini('hold-' + letter, own.hold);
    drawMini('next-' + letter, own.next);
    const queueNode = $('queue-' + letter);
    if (queueNode) {
      const rest = Array.isArray(own.next_queue) ? own.next_queue.slice(1) : [];
      queueNode.replaceChildren(...rest.map(piece => {
        const badge = document.createElement('span');
        badge.className = `queue-chip piece-${piece}`;
        badge.textContent = piece;
        return badge;
      }));
      queueNode.hidden = rest.length === 0;
    }
  }

  renderBoards(frame);

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
    time.textContent = event.frame !== undefined ? formatClock(event.frame, fps) : `T${String(event.turn).padStart(3, '0')}`;
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
      li.append(turnInstructions(session.turns[viewIndex - 1]));
    }
    feed.append(li);
  }
}

function replaceSession(target) {
  session?.bots?.forEach(bot => bot.close());
  session = target;
}

async function openLocalBots(target) {
  feedback('正在加载本地 Bot…');
  const loaded = await Promise.allSettled(target.modes.map((mode, side) => LocalBot.open(mode, target.files[side])));
  const bots = loaded.filter(entry => entry.status === 'fulfilled').map(entry => entry.value);
  if (target !== session || bots.length !== target.modes.length) {
    bots.forEach(bot => bot.close());
    if (target !== session) return false;
    throw loaded.find(entry => entry.status === 'rejected').reason;
  }
  target.bots = bots;
  feedback();
  return true;
}

async function addTurn(target) {
  const game = target.game;
  const active = game.activeSides();
  if (active.length && !target.bots && !await openLocalBots(target)) return;
  await Promise.all(active.map(async side => {
    const bot = target.bots[side];
    const {first_think_ms, think_ms} = game.rules;
    const budget = game.players[side].pieces_placed ? think_ms : first_think_ms;
    try {
      let response = await bot.decide(game.observe(side), budget, 0);
      if (target !== session) return;
      if (response.type === 'HOLD') {
        if (!game.hold(side)) return;
        response = await bot.decide(game.observe(side), budget, 0);
        if (target !== session) return;
      }
      if (response.type !== 'PLAY') throw new Error('Bot 没有提交完整走法');
      game.submit(side, response.commands, 0);
    } catch (error) {
      if (target === session && !(side in game.failures)) game.fail(side, String(error.message || error));
    }
  }));
  if (target !== session) return;
  const decision = structuredClone({
    holds: game.players.map(player => player.hold_used),
    actions: game.actions, failures: game.failures, think_frames: game.think_frames,
  });
  const event = game.settle();
  const record = {type: 'TURN', decision, event, state: game.snapshot()};
  target.turns.push(record);
  target.frames.push(colorFrame(makeFrame(game, event), target.frames.at(-1), record));
  if (game.finished) {
    target.result = summarize(game, event);
    target.bots?.forEach(bot => bot.close());
    target.bots = null;
  }
}

async function advance() {
  if (busy || advancing === session || !canAdvance()) return false;
  const target = session;
  if (viewIndex < target.frames.length - 1) {
    viewIndex++;
  } else {
    advancing = target;
    const previousIndex = viewIndex;
    try {
      $('step-match').disabled = true;
      if (!running) $('pause-match').disabled = true;
      await addTurn(target);
      if (target !== session) return false;
      if (viewIndex === previousIndex) viewIndex = target.frames.length - 1;
    } finally {
      if (advancing === target) advancing = null;
    }
  }
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
  if (!running || advancing === session) return;
  const token = generation;
  const speed = Number($('speed').value || 1);
  let delayMs = 650 / speed;
  if (isRealtimeSession() && session) {
    const curFrame = session.frames[viewIndex]?.event?.frame ?? session.frames[viewIndex]?.state?.frame ?? 0;
    const nextEntry = session.frames[viewIndex + 1];
    const nextFrame = nextEntry ? (nextEntry.event?.frame ?? nextEntry.state?.frame ?? curFrame + 12) : (curFrame + 12);
    const deltaFrames = Math.max(2, nextFrame - curFrame);
    delayMs = Math.max(16, (deltaFrames * 1000 / (session.header.rules.fps || 60)) / speed);
  }
  if (!reducedMotion.matches && session.frames[viewIndex]?.event?.players.some(player => spinTitle(player.lock))) {
    delayMs = Math.max(delayMs, SPIN_DURATION + 30);
  }
  timer = setTimeout(async () => {
    if (!running || token !== generation) return;
    try { await advance(); if (running && token === generation) tick(); }
    catch (error) { if (token !== generation) return; paused(); feedback(`本局暂停：${error.message}`, true); render(); }
  }, delayMs);
}

function resume() {
  if (busy || advancing === session || !canAdvance()) return;
  paused(); running = true; feedback(); render(); tick();
}

async function newMatch(seed, modes, {autoplay = true, files = [null, null]} = {}) {
  const token = ++generation;
  setOfficialActive();
  paused(); busy = true; feedback(); render();
  try {
    const game = await createDuel(seed);
    if (token !== generation) return;
    const names = modes.map((mode, side) => files[side]?.name ?? NAMES[mode]);
    const header = createHeader(game, modes);
    header.bots = names.map(name => ['browser-wasm', name]);
    replaceSession({
      kind: 'live', seed, modes, files, names, game, header,
      frames: [colorFrame(makeFrame(game))], turns: [], result: null, bots: null,
    });
    viewIndex = 0; busy = false; render();
    if (autoplay) resume();
  } catch (error) {
    if (token === generation) { busy = false; feedback(`无法开始比赛：${error.message}`, true); render(); }
  }
}

function readSettings() {
  const raw = $('seed').value.trim();
  if (!/^[+-]?\d+$/.test(raw) || !Number.isSafeInteger(Number(raw))) throw new Error('请输入有效的整数种子，范围为 ±9007199254740991。');
  const modes = SEAT_INPUTS.map(seat => $('bot-' + seat).value);
  const files = modes.map((mode, side) => {
    if (Object.hasOwn(NAMES, mode)) return null;
    const file = $('local-bot-' + SEAT_INPUTS[side]).files[0];
    if (!file) throw new Error('请为自选 Bot 座位选择一个 WASM 文件。');
    if (!/\.wasm$/i.test(file.name) || file.size > 16 * 1024 * 1024) {
      throw new Error('请选择不超过 16 MiB 的 Bot WASM 文件。');
    }
    return file;
  });
  return {seed: Number(raw), modes, files};
}

for (const seat of SEAT_INPUTS) {
  const select = $('bot-' + seat);
  const picker = $('local-file-' + seat);
  select.addEventListener('change', () => { picker.hidden = select.value !== 'custom'; });
}

$('start-match').addEventListener('click', () => {
  try {
    const {seed, modes, files} = readSettings();
    try { localStorage.setItem('tetra-arena-settings', JSON.stringify({seed, modes})); } catch { /* optional browser persistence */ }
    void newMatch(seed, modes, {files});
  } catch (error) { feedback(error.message, true); }
});

if ($('random-seed')) {
  $('random-seed').addEventListener('click', () => {
    $('seed').value = String(Math.floor(Math.random() * 999999) + 1);
  });
}

$('pause-match').addEventListener('click', () => { if (running) { paused(); render(); } else resume(); });
$('step-match').addEventListener('click', async () => {
  paused(); feedback();
  try { await advance(); }
  catch (error) { feedback(error.message, true); render(); }
});
if ($('step-back')) {
  $('step-back').addEventListener('click', () => { feedback(); rewind(); });
}
$('restart-match').addEventListener('click', () => {
  if (!session || busy) return;
  if (session.kind === 'replay') { paused(); viewIndex = 0; feedback(); render(); }
  else void newMatch(session.seed, session.modes, {autoplay: false, files: session.files});
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
  replaceSession({
    ...replay, kind: 'replay', seed: replay.header.seed, modes,
    names: names || modes.map((mode, side) => {
      const argv = replay.header.bots?.[side];
      return (argv?.[0] === 'browser-wasm' ? argv[1] : NAMES[mode])
        || `外部 AI ${side ? 'B' : 'A'}`;
    }),
  });
  viewIndex = 0; busy = false;
  feedback(replay.turns.length ? `已校验 ${replay.turns.length} 回合${replay.result ? '完整' : '阶段'}回放。` : '已校验初始棋盘，此文件尚未记录任何回合。');
  render();
}

async function loadLadderReplay(matchId) {
  if (!/^[a-f0-9]{32}$/.test(matchId)) { feedback('天梯对局编号无效，请从排行榜重新打开。', true); return; }
  const token = ++generation;
  setOfficialActive();
  paused(); busy = true; feedback('正在下载并重算天梯对局…'); render();
  try {
    const [metadataResponse, replayResponse] = await Promise.all([
      apiFetch(`/api/matches/${matchId}`),
      apiFetch(`/api/matches/${matchId}/replay`),
    ]);
    if (!metadataResponse.ok || !replayResponse.ok) throw new Error('无法获取该对局，请确认天梯服务正在运行。');
    if (Number(replayResponse.headers.get('content-length')) > MAX_SERVER_REPLAY_BYTES) throw new Error('服务器回放超过 32 MiB。');
    const metadata = await metadataResponse.json();
    const replay = await validateReplay(await replayResponse.text(), {maxBytes: MAX_SERVER_REPLAY_BYTES, currentOnly: true});
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
    officialReplays ??= fetch(new URL('./examples/official-replays.json?v=0.5.3-cpp', import.meta.url)).then(response => {
      if (!response.ok) throw new Error('官方回放暂不可用。');
      return response.json();
    }).catch(error => { officialReplays = null; throw error; });
    const match = (await officialReplays).find(entry => entry.id === id);
    if (!match) throw new Error('没有找到这组官方 Bot 回放。');
    if (match.header.engine_version !== CURRENT_ENGINE_VERSION) throw new Error('这组回放已归档。');
    const replay = await playOfficialReplay(match);
    if (token !== generation) return;
    acceptReplay(replay, id.split('-').map(tier => NAMES[tier]));
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
  paused(); busy = true; feedback('正在重新计算并校验回放…'); render();
  try {
    const replay = await validateReplay(await file.text(), {currentOnly: true});
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
    await newMatch(7, DEFAULT_BOTS, {autoplay: false});
    feedback('当前无法访问此自定义回放，请登录发起对局的账号后重新打开。', true);
  }
  document.body.classList.remove('checking-practice-access');
}

for (const [side, id] of ['spin-a', 'spin-b'].entries()) {
  $(id).addEventListener('click', () => {
    if (busy || !session) return;
    paused();
    render();
    renderBoards(session.frames[viewIndex], side);
  });
}

window.addEventListener('pagehide', concealPracticeReplay);
window.addEventListener('pageshow', () => void restorePracticeAccess());
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    if (running) { paused(); render(); }
    concealPracticeReplay();
  } else void restorePracticeAccess();
});

let settings = {seed: 7, modes: DEFAULT_BOTS};
try {
  const stored = JSON.parse(localStorage.getItem('tetra-arena-settings') || 'null');
  if (stored && Number.isSafeInteger(stored.seed) && Array.isArray(stored.modes)
      && stored.modes.length === 2 && stored.modes.every(mode => Object.hasOwn(NAMES, mode))) {
    settings = stored;
  }
} catch { /* default selectors stay usable */ }
$('seed').value = settings.seed;
$('bot-a').value = settings.modes[0];
$('bot-b').value = settings.modes[1];
const requestedMatch = new URLSearchParams(location.search).get('match');
await newMatch(settings.seed, settings.modes, {autoplay: false});
if (requestedMatch) await loadLadderReplay(requestedMatch);
