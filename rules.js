import { createDuel, DEFAULT_RULES, geometry } from './engine.mjs?v=0.5.3-local';
import './api.mjs';

const { cells, pivots, kicks_90: kicks90, kicks_180: kicks180 } = geometry();
const pieces = ['I', 'O', 'T', 'S', 'Z', 'J', 'L'];
const colors = {
  I: '#4fd8f5', O: '#f2cf5b', T: '#b78bfa', S: '#4ade80',
  Z: '#fb7185', J: '#60a5fa', L: '#fb923c', normal: '#647aa3',
  garbage: '#64748b', arriving: '#7dd3fc', solid: '#a59169', cancelled: '#3f4e6d', overflow: '#fb7185'
};
const byId = id => document.getElementById(id);
const pair = ([x, y]) => `(${x}, ${y})`;
const pairs = points => points.map(pair).join(' ');
const translated = (points, [x, y]) => points.map(([cx, cy]) => [x + cx, y + cy]);
const line = (y, keep = () => true) =>
  Array.from({ length: DEFAULT_RULES.width }, (_, x) => [x, y]).filter(([x]) => keep(x));

function text(x, y, value, size = 15, color = '#b8cbe7', anchor = 'middle') {
  return `<text x="${x}" y="${y}" text-anchor="${anchor}" fill="${color}" font-size="${size}" font-weight="650">${value}</text>`;
}

function svg(width, height, contents, description, extraClass = '') {
  const access = description ? `role="img" aria-label="${description}"` : 'aria-hidden="true"';
  return `<svg xmlns="http://www.w3.org/2000/svg" class="${extraClass}" viewBox="0 0 ${width} ${height}" ${access} focusable="false">${contents}</svg>`;
}

function block(x, y, size, type, outline = false) {
  const color = colors[type];
  if (outline) {
    return `<rect x="${x + 2}" y="${y + 2}" width="${size - 4}" height="${size - 4}" rx="3" fill="${color}" fill-opacity=".23" stroke="${color}" stroke-width="1.5" stroke-dasharray="3 2"/>`;
  }
  return `<rect x="${x + 1}" y="${y + 1}" width="${size - 2}" height="${size - 2}" rx="2.5" fill="${color}"/><path d="M${x + 3} ${y + 3}h${size - 6}" stroke="#ffffff55" stroke-linecap="round"/>`;
}

function grid(x, y, cols, rows, size, shadeRows = 0) {
  const width = cols * size;
  const height = rows * size;
  const paths = [];
  for (let column = 1; column < cols; column++) paths.push(`M${x + column * size} ${y}v${height}`);
  for (let row = 1; row < rows; row++) paths.push(`M${x} ${y + row * size}h${width}`);
  return [
    `<rect x="${x}" y="${y}" width="${width}" height="${height}" fill="#080e1d"/>`,
    shadeRows ? `<rect x="${x}" y="${y}" width="${width}" height="${shadeRows * size}" fill="#322854"/>` : '',
    `<path d="${paths.join('')}" stroke="#233352" fill="none"/>`,
    `<rect x="${x}" y="${y}" width="${width}" height="${height}" fill="none" stroke="#455f8a"/>`
  ].join('');
}

function board({ x, y, rows, size, base = 0, blocks = [], shadeRows = 0, markers = [] }) {
  const coords = (column, row) => [x + column * size, y + (rows - 1 - row + base) * size];
  return grid(x, y, DEFAULT_RULES.width, rows, size, shadeRows) +
    blocks.map(([column, row, type = 'normal', outline]) =>
      block(...coords(column, row), size, type, outline)).join('') +
    markers.map(([column, row]) => {
      const [left, top] = coords(column, row);
      const centerX = left + size / 2;
      const centerY = top + size / 2;
      return `<path d="M${centerX - 4} ${centerY - 4}l8 8m-8 0 8-8" stroke="#fecdd3" stroke-width="2" stroke-linecap="round"/>`;
    }).join('');
}

function colored(points, type, outline = false) {
  return points.map(([x, y]) => [x, y, type, outline]);
}

function figure(id, width, height, drawing, description) {
  byId(id).innerHTML = svg(width, height, drawing, description);
}

{
  const size = 22;
  const heroBoard = grid(15, 13, 14, 10, size, 2);
  const shapes = [
    ['T', 0, [2, 6]], ['I', 1, [8, 4]], ['S', 0, [5, 3]],
    ['J', 2, [1, 0]], ['O', 0, [4, -1]], ['Z', 0, [8, -1]], ['L', 3, [11, 0]]
  ];
  const tiled = shapes.map(([piece, rotation, origin]) =>
    translated(cells[piece][rotation], origin).map(([x, y]) =>
      block(15 + x * size, 13 + (9 - y) * size, size, piece)).join('')).join('');
  byId('hero-art').innerHTML = svg(338, 246,
    `<rect x="2" y="1" width="334" height="244" rx="14" fill="#0a1222" stroke="#3b5178"/>${heroBoard}${tiled}`);
}

{
  const { height, main_height: mainHeight } = DEFAULT_RULES;
  const shown = colored(translated(cells.T[0], DEFAULT_RULES.spawn_origins.T), 'T');
  const size = 13;
  const x = 30;
  const y = 28;
  const labels = [24, 21, 20, 15, 10, 5, 1].map(row =>
    text(x - 5, y + (height - row + .73) * size, row, 10.2, '#9eb6dc', 'end')).join('');
  const columns = line(0).map(([col]) => text(x + (col + .5) * size, y - 6, col + 1, 9.3)).join('');
  figure('board-figure', 230, 352,
    board({ x, y, rows: height, size, shadeRows: height - mainHeight, blocks: shown }) +
    labels + columns +
    text(170, 53, '出生缓冲区', 11, '#d4c0ff', 'start') +
    text(170, 176, '主区', 11, '#9fc2ee', 'start'),
    '完整 10 列 24 行棋盘。第 1 至 20 行是主区，第 21 至 24 行是出生缓冲区。T 块的横排位于第 21 行第 4 至 6 列，凸起位于第 22 行第 5 列。');
}

function comparison(id, before, after, description, labels = ['锁定后 · 清行前', '清行后'], rows = 5) {
  const size = 18;
  const drawPane = (x, pane, label) => board({ x, y: 34, rows, size, ...pane }) +
    text(x + 90, 20, label, 16);
  figure(id, 440, 45 + rows * size, drawPane(12, before, labels[0]) + drawPane(248, after, labels[1]) +
    text(221, 41 + rows * size / 2, '→', 26, '#5ed0ff'), description);
}

{
  const gap = 5;
  const tower = Array.from({ length: 4 }, (_, y) => line(y, x => x !== gap)).flat();
  const verticalI = colored(translated(cells.I[1], [3, 0]), 'I');
  comparison('four-figure',
    { blocks: [...tower, [0, 4], [1, 4], ...verticalI] },
    { blocks: [[0, 0], [1, 0]] },
    '左图竖直 I 块补满底部四行的第六列。右图四行清除后，原第五行左侧两个方块落在底行。');
}

{
  const filled = [0, 1].flatMap(y => line(y, x => x !== 4 && x !== 5));
  const square = colored(translated(cells.O[0], [3, -1]), 'O');
  comparison('pc-figure', { blocks: [...filled, ...square] }, {},
    '左图 O 块补齐底部两行中央的缺口。右图底部两行清除后没有残留方块，达成全消。');
}

{
  const bars = (x, y, count, color, faded = 0) => Array.from({ length: count }, (_, i) =>
    block(x + i * 23, y, 21, i < faded ? 'cancelled' : color)).join('');
  const pane = (x, title, rows) =>
    `<rect x="${x}" y="7" width="220" height="146" rx="9" fill="#0b1426" stroke="#3c5275"/>` +
    text(x + 13, 33, title, 17, '#e4edff', 'start') +
    rows.map(([label, count, type, faded = 0], i) =>
      text(x + 13, 66 + i * 33, label, 15, '#b8cbe7', 'start') +
      bars(x + 111, 50 + i * 33, count, type, faded)).join('');
  figure('warning-figure', 482, 160,
    pane(6, '同帧锁定 · 对冲前', [['A 抵旧 3', 3, 'cancelled'], ['A 剩 1', 1, 'T'], ['B 剩 2', 2, 'J']]) +
    pane(256, '交叉抵消后', [['双方抵 1', 1, 'cancelled'], ['B 净发 1', 1, 'J']]) +
    text(241, 92, '→', 22, '#5ed0ff'),
    'A 先抵消自己的三行旧预警，余一行。B 有两行新攻击。双方同帧锁定，各抵一行，B 向 A 净发一行。');
}

{
  const segments = [...colored(line(2, x => x !== 2), 'garbage'),
    ...colored([0, 1].flatMap(y => line(y, x => x !== 7)), 'garbage'), [0, 3], [1, 3]];
  figure('garbage-figure', 218, 101,
    board({ x: 9, y: 9, rows: 4, size: 20, blocks: segments }) +
    '<path d="M9 49h200" stroke="#d5bcc7" stroke-dasharray="3 3"/>',
    '无实心底层时的棋盘局部。先升起的一行在上方，缺口位于第 3 列；后升起的两行在底部，缺口都位于第 8 列。虚线分隔两段。');
}

{
  const floor = colored([...line(0), ...line(1)], 'solid');
  const original = [...colored(line(2, x => x !== 2), 'garbage'), [0, 3], [1, 3]];
  const arriving = colored([2, 3].flatMap(y => line(y, x => x !== 7)), 'arriving');
  const lifted = original.map(([x, y, type]) => [x, y + 2, type]);
  comparison('solid-garbage-figure',
    { blocks: [...floor, ...original] },
    { blocks: [...floor, ...arriving, ...lifted] },
    '左右两盘的第 1 至 2 行均为金色实心底层。右盘第 3 至 4 行是新升起的浅蓝垃圾，两行都在第 8 列留洞。原第 3 行的灰色垃圾和第 4 行的普通格被抬到第 5 至 6 行。',
    ['垃圾升起前', '两行垃圾升起后'], 6);
}

{
  const solid = board({ x: 8, y: 52, rows: 4, size: 15,
    blocks: [...colored([...line(0), ...line(1)], 'solid'),
      ...colored(line(2, x => x !== 7), 'garbage'), [1, 3], [2, 3]] });
  const overflow = board({ x: 184, y: 37, rows: 5, size: 15, base: 20,
    blocks: [[0, 24, 'overflow', true]], markers: [[0, 24]] });
  const spawn = colored(translated(cells.T[0], DEFAULT_RULES.spawn_origins.T), 'T', true);
  const blocked = board({ x: 360, y: 52, rows: 4, size: 15, base: 20, shadeRows: 4,
    blocks: [[4, 21], ...spawn], markers: [[4, 21]] });
  figure('overtime-figure', 518, 140, solid + overflow + blocked +
    '<path d="M184 52h150" stroke="#fda4af" stroke-width="2" stroke-dasharray="4 3"/>' +
    text(83, 26, '实心底层 +1', 18, '#fde68a') +
    text(259, 26, '越过第 24 行', 18, '#fecdd3') +
    text(435, 26, '下一块出生受阻', 18, '#ddd6fe'),
    '左图最底部新增一行实心格，已有实心层和上方垃圾均被顶高一格。中图第 24 行上方出现越界方块，虚线标示顶边。右图虚线轮廓是下一块 T，已锁定格与其第 22 行第 5 列出生格重叠。');
}

function shapeIcon(piece, points) {
  const size = 15;
  const [px, py] = pivots[piece];
  const pivotX = 1 + (px + .5) * size;
  const pivotY = 1 + (3.5 - py) * size;
  const tiles = points.map(([x, y]) => block(1 + x * size, 1 + (3 - y) * size, size, piece)).join('');
  return svg(62, 62, grid(1, 1, 4, 4, size) + tiles +
    `<path d="M${pivotX - 4} ${pivotY}h8m-4 -4v8" stroke="#0b1526" stroke-width="4" stroke-linecap="round"/><path d="M${pivotX - 4} ${pivotY}h8m-4 -4v8" stroke="#fff4dd" stroke-width="1.5" stroke-linecap="round"/>`,
    null, 'shape-icon');
}

function geometryRows() {
  const orientations = pieces.map(piece => {
    const values = cells[piece].map(points => {
      const columns = points.map(pair);
      return `<td>${shapeIcon(piece, points)}<code class="coordinates">${columns.slice(0, 2).join(' ')}<br>${columns.slice(2).join(' ')}</code></td>`;
    }).join('');
    return `<tr><th scope="row" class="piece-${piece}">${piece}<code class="pivot">${pair(pivots[piece])}</code></th>${values}</tr>`;
  });
  const spawns = pieces.map(piece => {
    const origin = DEFAULT_RULES.spawn_origins[piece];
    return `<tr><th scope="row">${piece}</th><td><code>${pair(origin)}</code></td><td><code>${pairs(translated(cells[piece][0], origin))}</code></td></tr>`;
  });
  const ninety = Object.keys(kicks90.T).map(key => {
    const offsets = ['T', 'I', 'O'].map(piece => `<td><code>${pairs(kicks90[piece][key])}</code></td>`).join('');
    return `<tr><th scope="row">${key.replace(':', ' → ')}</th>${offsets}</tr>`;
  });
  const half = [0, 1, 2, 3].map(old => {
    const next = (old + 2) % 4;
    const list = Array.isArray(kicks180) ? kicks180 : kicks180[`${old}:${next}`];
    const offsets = list.map(offset => `<td><code>${pair(offset)}</code></td>`).join('');
    return `<tr><th scope="row">${old} → ${next}</th>${offsets}</tr>`;
  });

  for (const [id, rows] of [['piece-table', orientations], ['spawn-table', spawns], ['kicks-90', ninety], ['kicks-180', half]]) {
    byId(id).tBodies[0].innerHTML = rows.join('');
  }
}

geometryRows();

/* Live Scrollspy + Hash sync for TOC */
const topics = Array.from(document.querySelectorAll('.topics a'));
function setActiveTopic(hash) {
  for (const link of topics) {
    if (link.hash === hash) link.setAttribute('aria-current', 'location');
    else link.removeAttribute('aria-current');
  }
}

function updateTopicFromHash() {
  setActiveTopic(window.location.hash || '#board');
}
updateTopicFromHash();
window.addEventListener('hashchange', updateTopicFromHash);

const sections = topics.map(link => document.querySelector(link.hash)).filter(Boolean);
if ('IntersectionObserver' in window && sections.length) {
  const observer = new IntersectionObserver(entries => {
    const visible = entries.filter(e => e.isIntersecting).sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top);
    if (visible.length) setActiveTopic(`#${visible[0].target.id}`);
  }, { rootMargin: '-88px 0px -65% 0px', threshold: 0 });
  sections.forEach(sec => observer.observe(sec));
}

/* One-click Copy buttons on Protocol JSONL <pre> blocks */
for (const pre of document.querySelectorAll('.message pre')) {
  const wrap = document.createElement('div');
  wrap.className = 'code-block-wrap';
  pre.parentNode.insertBefore(wrap, pre);
  wrap.append(pre);

  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'copy-code-btn';
  btn.textContent = '复制';
  btn.setAttribute('aria-label', '复制样例代码');
  btn.addEventListener('click', async () => {
    const content = pre.textContent.trim();
    try {
      await navigator.clipboard.writeText(content);
      btn.textContent = '已复制 ✓';
      btn.dataset.copied = 'true';
      setTimeout(() => {
        btn.textContent = '复制';
        delete btn.dataset.copied;
      }, 1600);
    } catch {
      btn.textContent = '请手动复制';
      setTimeout(() => { btn.textContent = '复制'; }, 1600);
    }
  });
  wrap.append(btn);
}

async function showObservations() {
  try {
    const duel = await createDuel(0);
    const bankMs = duel.rules.time_bank_ms;
    const firstBudgetMs = duel.rules.first_think_ms + bankMs;
    const holdExampleMs = 140;
    const observationLine = (requestId, remainingMs) => JSON.stringify({
      type: 'OBSERVE', request_id: requestId, remaining_ms: remainingMs, bank_ms: bankMs, observation: duel.observe(0)
    }) + '\n';
    byId('observe-example').textContent = observationLine('1', firstBudgetMs);
    duel.hold(0);
    byId('observe-after-hold').textContent = observationLine('2', firstBudgetMs - holdExampleMs);
  } catch {
    byId('observe-example').textContent = '无法生成样例。请通过 HTTPS 或 localhost 打开页面。';
    byId('observe-after-hold').textContent = '无法生成样例。请通过 HTTPS 或 localhost 打开页面。';
  }
}

showObservations();
