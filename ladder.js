import {apiFetch, rememberSessionToken} from "./api.mjs";

const byId = id => document.getElementById(id);
const accountDialog = byId("account-dialog");
const uploadForm = byId("upload-form");
const ownSelect = byId("challenge-bot");
const opponentSelect = byId("challenge-opponent");
const historyList = byId("challenge-list");
const rankingRows = byId("ranking-rows");
const numberFormat = new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 2 });
const signedFormat = new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 2, signDisplay: "exceptZero" });
const dateFormat = new Intl.DateTimeFormat("zh-CN", { dateStyle: "medium", timeStyle: "short" });
const statusLabels = { queued: "排队中", running: "对战中", completed: "已完成", failed: "未结算" };
const offlineHint = "暂时无法连接比赛服务。仍可下载示例 Bot、阅读规则或观看官方回放。";
const legacyLanguages = [{name: "Python 3", extension: ".py"}, {name: "JavaScript", extension: ".js"}];

const state = {
  session: null,
  leaderboard: null,
  myBots: null,
  challenges: null,
  health: null,
  errors: {},
  pending: null,
};

let activeRefresh = null;
let sessionRequest = null;
let authMode = "login";
let dialogTrigger = null;
let rankingKey = null;
let historyKey = null;
let historyFilter = "all";
let pendingOpponentId = null;

class ApiError extends Error {
  constructor(message, status = 0) {
    super(message);
    this.status = status;
  }
}

/* Workbench Tabs */
const tabMap = {
  upload: {tab: "tab-upload", pane: "upload"},
  challenge: {tab: "tab-challenge", pane: "challenge-panel"},
  practice: {tab: "tab-practice", pane: "custom-match"},
};

function switchTab(name, {focusPane = false} = {}) {
  const target = tabMap[name] ? name : "upload";
  for (const [key, ids] of Object.entries(tabMap)) {
    const active = key === target;
    const tabBtn = byId(ids.tab);
    const pane = byId(ids.pane);
    if (tabBtn) tabBtn.setAttribute("aria-selected", String(active));
    if (pane) pane.hidden = !active;
  }
  if (focusPane) {
    const pane = byId(tabMap[target].pane);
    pane?.scrollIntoView({behavior: "smooth", block: "nearest"});
  }
}

for (const button of document.querySelectorAll("[data-tab]")) {
  button.addEventListener("click", () => switchTab(button.dataset.tab));
}

for (const link of document.querySelectorAll("[data-open-tab]")) {
  link.addEventListener("click", event => {
    event.preventDefault();
    const tab = link.dataset.openTab;
    switchTab(tab, {focusPane: true});
    if (tab === "upload") byId("bot-name")?.focus();
  });
}

function syncTabFromHash() {
  if (location.hash === "#custom-match") switchTab("practice");
  else if (location.hash === "#challenge-panel") switchTab("challenge");
  else if (location.hash === "#upload") switchTab("upload");
}
window.addEventListener("hashchange", syncTabFromHash);
syncTabFromHash();

/* Drag-and-Drop File Inputs */
function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024).toFixed(1)} KiB`;
}

function syncFileDisplay(inputId) {
  const input = byId(inputId);
  const status = byId(`${inputId}-status`);
  const nameEl = byId(`${inputId}-name`);
  if (!input || !status || !nameEl) return;
  const file = input.files?.[0];
  if (file) {
    nameEl.textContent = `${file.name} (${formatBytes(file.size)})`;
    status.hidden = false;
  } else {
    nameEl.textContent = "";
    status.hidden = true;
  }
}

for (const zone of document.querySelectorAll("[data-dropzone]")) {
  const inputId = zone.dataset.dropzone;
  const input = byId(inputId);
  if (!input) continue;
  input.addEventListener("change", () => syncFileDisplay(inputId));
  zone.addEventListener("dragover", event => {
    event.preventDefault();
    zone.classList.add("is-dragover");
  });
  zone.addEventListener("dragleave", () => zone.classList.remove("is-dragover"));
  zone.addEventListener("drop", event => {
    event.preventDefault();
    zone.classList.remove("is-dragover");
    const file = event.dataTransfer?.files?.[0];
    if (!file) return;
    const transfer = new DataTransfer();
    transfer.items.add(file);
    input.files = transfer.files;
    input.dispatchEvent(new Event("change", {bubbles: true}));
    if (inputId === "bot-source" && !byId("bot-name").value.trim()) {
      const stem = file.name.replace(/\.[^.]+$/, "").slice(0, 40);
      if (stem) byId("bot-name").value = stem;
    }
  });
}

for (const clearBtn of document.querySelectorAll("[data-clear-file]")) {
  clearBtn.addEventListener("click", () => {
    const inputId = clearBtn.dataset.clearFile;
    const input = byId(inputId);
    if (!input) return;
    input.value = "";
    syncFileDisplay(inputId);
  });
}

function uploadLanguages() {
  return Array.isArray(state.health?.languages) ? state.health.languages : legacyLanguages;
}

function sourceSizeLimit() {
  return state.health?.limits?.source_bytes ?? 256 * 1024;
}

function sourceExtension(filename) {
  return /\.[a-z]+$/i.exec(filename)?.[0].toLowerCase() ?? "";
}

function validSource(file) {
  return file && file.size > 0 && file.size <= sourceSizeLimit() &&
    uploadLanguages().some(({extension}) => extension === sourceExtension(file.name));
}

function sourceHelp() {
  return `请选择一个受支持语言的非空 UTF-8 文件，大小不超过 ${numberFormat.format(sourceSizeLimit() / 1024)} KiB。`;
}

async function readSource(file) {
  try {
    return new TextDecoder("utf-8", {fatal: true}).decode(await file.arrayBuffer());
  } catch {
    throw new ApiError("无法读取源码，请选择 UTF-8 编码的文件。");
  }
}

async function request(path, body) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 12_000);
  const options = { cache: "no-store", signal: controller.signal };
  if (body !== undefined) {
    options.method = "POST";
    options.headers = { "Content-Type": "application/json", "X-CSRF-Token": state.session.csrf_token };
    options.body = JSON.stringify(body);
  }

  try {
    const response = await apiFetch(path, options);
    const data = await response.json().catch(() => null);
    if (!response.ok) {
      const message = data?.error?.message;
      const fallback = response.status === 503 ? "天梯服务暂不可用，请稍后刷新。" : "天梯请求失败，请稍后重试。";
      throw new ApiError(typeof message === "string" && message ? message : fallback, response.status);
    }
    if (!data || typeof data !== "object" || Array.isArray(data)) {
      throw new ApiError("当前页面未连接天梯 API。", response.status);
    }
    rememberSessionToken(data.session_token);
    return data;
  } catch (error) {
    if (error instanceof ApiError) throw error;
    const message = controller.signal.aborted ? "连接天梯服务超时，请稍后重试。" : "无法连接天梯服务。";
    throw new ApiError(message);
  } finally {
    clearTimeout(timeout);
  }
}

function loadSession() {
  if (!sessionRequest) {
    sessionRequest = request("/api/session").then(session => {
      if (typeof session.csrf_token !== "string" || !session.csrf_token) {
        throw new ApiError("天梯服务未返回有效会话。");
      }
      return session;
    }).finally(() => { sessionRequest = null; });
  }
  return sessionRequest;
}

function useSession(session) {
  if (state.session?.user?.id !== session.user?.id) {
    state.myBots = null;
    state.challenges = state.challenges?.filter(challenge => !challenge.practice) ?? null;
  }
  state.session = session;
}

async function refresh() {
  if (document.hidden || activeRefresh || state.pending) return;
  const refreshMarker = {};
  activeRefresh = refreshMarker;
  byId("refresh").setAttribute("aria-disabled", "true");

  const sessionTask = loadSession();
  const [health, session, leaderboard, challenges, mine] = await Promise.allSettled([
    request("/api/health"),
    sessionTask,
    request("/api/leaderboard"),
    request("/api/challenges"),
    sessionTask.then(current => current.user ? request("/api/bots/mine") : null),
  ]);

  if (activeRefresh !== refreshMarker) return;
  activeRefresh = null;
  byId("refresh").removeAttribute("aria-disabled");

  if (health.status === "fulfilled" && health.value.ok) {
    state.health = health.value;
    state.errors.health = null;
  } else {
    state.health = null;
    state.errors.health = health.reason ?? new ApiError("天梯服务暂不可用。", 503);
  }
  if (session.status === "fulfilled") useSession(session.value);

  recordResponse("leaderboard", leaderboard, result => result.bots, result => { state.leaderboard = result; });
  if (leaderboard.status === "fulfilled" && Array.isArray(leaderboard.value.bots)) {
    const season = leaderboard.value.season;
    setMessage("season", season ? `赛季 ${season}` : "");
  }
  recordResponse("challenges", challenges, result => result.challenges, result => { state.challenges = result; });
  if (state.session?.user) {
    recordResponse("mine", mine, result => result?.bots, result => { state.myBots = result; });
  }
  render();
}

function recordResponse(key, response, select, save) {
  const records = response.status === "fulfilled" ? select(response.value) : null;
  if (Array.isArray(records)) {
    save(records);
    state.errors[key] = null;
  } else {
    state.errors[key] = response.reason ?? new ApiError("天梯服务返回的数据不完整。");
  }
}

function setText(node, value) {
  if (node && node.textContent !== value) node.textContent = value;
}

function setMessage(id, message = "", tone = "") {
  const node = byId(id);
  if (!node) return;
  setText(node, message);
  if (!node.classList.contains("form-feedback")) node.hidden = !message;
  node.dataset.tone = tone;
}

function make(tag, className = "", content = "") {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (content) node.textContent = content;
  return node;
}

function updateHealth() {
  const healthError = state.errors.health;
  const service = byId("service-status");
  const runnerLine = byId("runner-status");
  const runner = state.health?.runner;
  service.dataset.state = state.health ? "online" : healthError ? "offline" : "pending";
  runnerLine.dataset.state = runner ? (runner.available ? "online" : "offline") : "pending";
  setText(byId("service-state"), state.health ? "已连接" : healthError?.status === 503 ? "暂不可用" : healthError ? "未连接" : "连接中");
  setText(byId("runner-state"), runner ? (runner.available ? "可用" : "不可用") : "待检测");

  let hint = "正在获取排行榜与对局。";
  if (healthError) {
    hint = healthError.status === 503 ? `${healthError.message} ${offlineHint}` : offlineHint;
  } else if (runner && !runner.available) {
    hint = `${runner.reason ? `${runner.reason} ` : ""}仍可上传，涉及上传或官方练习 Bot 的对局将等待 Docker 沙箱。`;
  } else if (state.health) {
    hint = "排行榜与挑战记录每 15 秒自动刷新。";
  }
  setText(byId("health-hint"), hint);
}

function render() {
  updateHealth();
  renderControls();
  renderLeaderboard();
  renderHistory();
}

function versionLabel(bot) {
  if (bot.version == null || bot.version === "") return "";
  const version = String(bot.version);
  return /^[vV]/.test(version) ? version : `v${version}`;
}

function botLabel(bot) {
  const version = versionLabel(bot);
  return version ? `${bot.name} · ${version}` : bot.name;
}

function challengeOpponentQuick(bot) {
  pendingOpponentId = String(bot.id);
  switchTab("challenge", {focusPane: true});
  if (state.session?.user) {
    if (opponentSelect.querySelector(`option[value="${CSS.escape(pendingOpponentId)}"]`)) {
      opponentSelect.value = pendingOpponentId;
    }
    if (!ownSelect.disabled) ownSelect.focus();
    else setMessage("challenge-feedback", "请先上传至少一个 Bot 版本后再发起挑战。", "");
  } else {
    setMessage("challenge-feedback", `已为你预选对手 ${botLabel(bot)}，登录后即可发起挑战。`, "");
    openAuth(byId("challenge-submit"));
  }
}

function leaderboardRow(bot) {
  const user = state.session?.user;
  const isMine = Boolean(user && bot.owner_id === user.id);
  const row = make("tr");
  const ranked = bot.games > 0 && bot.rank != null;
  const rank = make("td", "rank-value");
  if (ranked && bot.rank <= 3) {
    const badge = make("span", "rank-badge", String(bot.rank));
    badge.dataset.rank = String(bot.rank);
    rank.append(badge);
  } else {
    rank.textContent = ranked ? String(bot.rank) : "—";
  }
  if (!ranked) rank.setAttribute("aria-label", "未定级");

  const competitor = make("td");
  const tile = make("div", "bot-tile");
  const icon = make("span", "bot-avatar", Array.from(bot.name || "?")[0].toUpperCase());
  icon.dataset.kind = bot.kind;
  icon.setAttribute("aria-hidden", "true");
  const labels = make("div", "bot-labels");
  const title = make("div");
  title.append(make("strong", "", bot.name));
  if (bot.kind === "builtin") title.append(make("span", "official-badge", "官方"));
  if (isMine) title.append(make("span", "mine-badge", "我的"));
  const details = [bot.kind === "builtin" ? "" : bot.owner, versionLabel(bot)].filter(Boolean);
  labels.append(title, make("div", "bot-meta", details.join(" · ")));
  tile.append(icon, labels);
  competitor.append(tile);

  const rating = make("td", "numeric");
  rating.append(make("span", "rating-value", numberFormat.format(bot.rating)));
  if (!ranked) rating.append(make("div", "bot-unranked", "未定级"));

  const battles = make("td", "numeric");
  battles.append(make("span", "record-value", `${bot.wins} / ${bot.losses} / ${bot.draws}`));
  const winRate = bot.games > 0 ? ` · ${Math.round((bot.wins / bot.games) * 100)}% 胜率` : "";
  battles.append(make("span", "record-meta", `${bot.games} 局${winRate}`));

  const action = make("td", "numeric");
  if (isMine) {
    action.append(make("span", "row-own-tag", "己方"));
  } else {
    const btn = make("button", "row-challenge-btn", "挑战");
    btn.type = "button";
    btn.setAttribute("aria-label", `挑战 ${botLabel(bot)}`);
    btn.addEventListener("click", () => challengeOpponentQuick(bot));
    action.append(btn);
  }

  row.append(rank, competitor, rating, battles, action);
  return row;
}

function renderLeaderboard() {
  const filteringMine = byId("mine-filter").checked;
  const needle = byId("bot-search").value.trim().toLowerCase();
  const bots = filteringMine ? state.myBots : state.leaderboard;
  const error = filteringMine ? state.errors.mine : state.errors.leaderboard;
  let empty = "排行榜载入中…";
  let matches = [];
  let message = "";

  if (filteringMine && !state.session?.user) {
    empty = "登录后可查看你的 Bot。";
  } else if (error && !bots) {
    empty = filteringMine ? "暂时无法加载你的 Bot。" : "接通天梯服务后，排行榜会在这里显示。";
    if (state.health || error.status === 503) message = error.message;
  } else if (bots) {
    matches = bots.filter(bot => `${bot.name} ${bot.owner} ${versionLabel(bot)}`.toLowerCase().includes(needle));
    empty = needle ? "没有找到匹配的 Bot。" : filteringMine ? "你还没有上传 Bot。" : "暂无 Bot。";
    if (error) message = `${error.message} 当前显示最近一次加载的数据。`;
  }

  setText(byId("bot-count"), bots ? `${matches.length} / ${bots.length}` : "");
  setMessage("board-status", message);
  const key = JSON.stringify({user: state.session?.user?.id ?? null, matches, empty});
  if (key === rankingKey) return;
  rankingKey = key;
  if (matches.length) {
    rankingRows.replaceChildren(...matches.map(leaderboardRow));
  } else {
    const row = make("tr");
    const cell = make("td");
    cell.colSpan = 5;
    cell.append(make("p", "empty-state", empty));
    row.append(cell);
    rankingRows.replaceChildren(row);
  }
}

function replayLinks(challenge) {
  const games = make("ul", "game-links");
  challenge.games.forEach((game, index) => {
    const participants = [challenge.a, challenge.b];
    const winner = participants.find(bot => bot.id === game.winner_id);
    const seats = [game.a_id, game.b_id].map(id => botLabel(participants.find(bot => bot.id === id)));
    const winnerSeat = game.winner_seat === 0 ? "A 座" : game.winner_seat === 1 ? "B 座" : "";
    const result = winner ? `${winnerSeat} ${winner.name} 胜`.trim() : game.winner_id == null ? "平局" : "已结束";
    const item = make("li");
    const link = make("a");
    link.href = `./index.html?match=${encodeURIComponent(game.id)}`;
    const info = make("span", "game-result", `第 ${index + 1} 局 · ${result} · ${game.turns} 回合`);
    info.append(make("span", "game-seats", `A 座 ${seats[0]} · B 座 ${seats[1]}`));
    link.append(info);
    link.append(make("span", "game-action", "观看回放 →"));
    item.append(link);
    games.append(item);
  });
  return games;
}

function challengeItem(challenge) {
  const item = make("li", "challenge-item");
  const head = make("div", "challenge-item-head");
  const names = make("div", "challenge-names");
  names.append(make("b", "", botLabel(challenge.a)), make("span", "versus-label", "VS"), make("b", "", botLabel(challenge.b)));
  const badge = make("span", "status-badge", statusLabels[challenge.status] ?? "状态未知");
  badge.dataset.state = challenge.status;
  head.append(names, badge);

  const meta = make("div", "challenge-meta");
  if (challenge.practice) meta.append(make("span", "practice-tag", "自定义对战"));
  const date = new Date(challenge.created_at);
  if (challenge.created_at && !Number.isNaN(date.getTime())) {
    const time = make("time", "", dateFormat.format(date));
    time.dateTime = date.toISOString();
    meta.append(time);
  }
  if (Number.isFinite(challenge.seed)) meta.append(make("span", "", `种子 ${challenge.seed}`));
  if (Number.isFinite(challenge.score_a)) {
    const points = challenge.score_a;
    meta.append(make("strong", "series-score", `${numberFormat.format(points)} : ${numberFormat.format(2 - points)}`));
  }
  if (challenge.rated && Number.isFinite(challenge.rating_delta_a)) {
    const delta = challenge.rating_delta_a;
    for (const [bot, change] of [[challenge.a, delta], [challenge.b, -delta]]) {
      const amount = make("span", "rating-change", `${botLabel(bot)} ${signedFormat.format(change || 0)}`);
      amount.dataset.sign = change > 0 ? "positive" : change < 0 ? "negative" : "neutral";
      meta.append(amount);
    }
  }
  if (!challenge.practice && !challenge.rated && challenge.status === "completed") meta.append(make("span", "", "不计分"));
  item.append(head, meta);

  const error = challenge.error?.message ?? challenge.error;
  if (typeof error === "string" && error) item.append(make("p", "challenge-error", error));
  if (challenge.games?.length) item.append(replayLinks(challenge));
  return item;
}

function matchesHistoryFilter(challenge) {
  if (historyFilter === "all") return true;
  if (historyFilter === "active") return challenge.status === "queued" || challenge.status === "running";
  if (historyFilter === "completed") return challenge.status === "completed";
  if (historyFilter === "failed") return challenge.status === "failed";
  return true;
}

for (const pill of document.querySelectorAll("[data-history-filter]")) {
  pill.addEventListener("click", () => {
    historyFilter = pill.dataset.historyFilter;
    for (const btn of document.querySelectorAll("[data-history-filter]")) {
      btn.setAttribute("aria-pressed", String(btn === pill));
    }
    historyKey = null;
    renderHistory();
  });
}

function renderHistory() {
  const records = state.challenges;
  const error = state.errors.challenges;
  const filtered = records ? records.filter(matchesHistoryFilter) : [];
  let empty = "挑战记录载入中…";
  if (records) {
    empty = records.length ? "当前筛选条件下没有挑战记录。" : "暂无挑战。发起天梯挑战后可在这里查看进度。";
  }
  if (error && !records) empty = "接通天梯服务后，挑战与回放会在这里显示。";
  const message = error && records ? `${error.message} 当前显示最近一次加载的数据。` : error && state.health ? error.message : "";
  setMessage("history-status", message);

  const key = filtered.length ? JSON.stringify(filtered) : `${historyFilter}:${empty}`;
  if (key === historyKey || historyList.contains(document.activeElement)) return;
  historyKey = key;
  setMessage("history-empty", filtered.length ? "" : empty);
  historyList.hidden = !filtered.length;
  historyList.replaceChildren(...filtered.map(challengeItem));
}

function setOptions(select, placeholder, bots, label = botLabel) {
  const options = bots.map(bot => [String(bot.id), label(bot)]);
  options.unshift(["", placeholder]);
  const key = JSON.stringify(options);
  if (select.dataset.options === key || document.activeElement === select) return;
  select.dataset.options = key;
  const previous = select.value;
  const nodes = options.map(([value, text]) => new Option(text, value));
  select.replaceChildren(...nodes);
  if (nodes.some(option => option.value === previous)) select.value = previous;
}

function renderControls() {
  const user = state.session?.user;
  const accountName = byId("account-name");
  const accountPill = byId("account-pill");
  byId("account-open").hidden = Boolean(user);
  byId("account-logout").hidden = !user;
  byId("account-logout").disabled = Boolean(state.pending);
  accountName.hidden = !user;
  if (accountPill) accountPill.hidden = !user;
  setText(accountName, user?.username ?? "");

  const bots = user ? (state.myBots ?? []) : [];
  const opponents = user ? (state.leaderboard ?? []).filter(bot => bot.owner_id !== user.id) : [];
  let ownPlaceholder = "登录后选择 Bot";
  if (user) ownPlaceholder = bots.length ? "选择 Bot 版本" : state.errors.mine ? "暂时无法加载 Bot" : state.myBots ? "请先上传 Bot" : "正在加载 Bot…";
  setOptions(ownSelect, ownPlaceholder, bots);
  setOptions(opponentSelect, "自动配对（推荐）", opponents, bot => `${botLabel(bot)} · ${bot.kind === "builtin" ? "官方" : bot.owner}`);
  if (pendingOpponentId && opponentSelect.querySelector(`option[value="${CSS.escape(pendingOpponentId)}"]`)) {
    opponentSelect.value = pendingOpponentId;
    pendingOpponentId = null;
  }
  ownSelect.disabled = !bots.length;
  opponentSelect.disabled = !bots.length;

  const upload = byId("upload-submit");
  const challenge = byId("challenge-submit");
  upload.disabled = Boolean(state.pending);
  challenge.disabled = Boolean(state.pending) || Boolean(user && !bots.length);
  for (const control of [upload, challenge]) {
    if (user) control.removeAttribute("aria-haspopup");
    else control.setAttribute("aria-haspopup", "dialog");
  }
  setText(upload, state.pending === "upload" ? "正在上传…" : user ? "上传并自动排队" : "登录后上传");
  setText(challenge, state.pending === "challenge" ? "正在提交…" : user ? "发起挑战" : "登录后挑战");
  const languages = uploadLanguages();
  const accept = languages.map(({extension}) => extension).join(",");
  byId("bot-source").accept = accept;
  setText(byId("source-help"), sourceHelp());
  if (state.health) setText(byId("source-languages"), languages.map(({name, extension}) => `${name} (${extension})`).join("、"));
}

function updateAuthMode(mode) {
  authMode = mode;
  const registering = mode === "register";
  setText(byId("account-title"), registering ? "注册天梯账号" : "登录天梯");
  setText(byId("account-description"), registering ? "创建账号后可管理你上传的 Bot 版本。" : "登录后可上传 Bot 和发起挑战。");
  setText(byId("account-submit"), registering ? "创建账号并登录" : "登录");
  setText(byId("account-switch"), registering ? "已有账号？切换到登录" : "没有账号？立即注册");
  byId("account-password").autocomplete = registering ? "new-password" : "current-password";
  for (const tab of document.querySelectorAll("[data-auth-mode]")) {
    tab.setAttribute("aria-selected", String(tab.dataset.authMode === mode));
  }
  setMessage("account-feedback");
}

function openAuth(trigger) {
  if (accountDialog.open || state.pending) return;
  dialogTrigger = trigger;
  updateAuthMode("login");
  accountDialog.showModal();
  byId("account-username").focus();
}

async function mutate(kind, feedback, work) {
  if (state.pending) return;
  state.pending = kind;
  activeRefresh = null;
  byId("refresh").removeAttribute("aria-disabled");
  renderControls();
  setMessage(feedback);
  try {
    if (!state.session?.csrf_token) useSession(await loadSession());
    await work();
  } catch (error) {
    setMessage(feedback, error.message, "error");
    if (error.status === 401 || error.status === 403) {
      try { useSession(await loadSession()); } catch { /* Retain the last known session while offline. */ }
    }
  } finally {
    state.pending = null;
    render();
    void refresh();
  }
}

function cacheNewChallenge(challenge) {
  state.challenges = [challenge, ...(state.challenges ?? []).filter(record => record.id !== challenge.id)];
}

byId("account-open").addEventListener("click", event => openAuth(event.currentTarget));
byId("account-close").addEventListener("click", () => accountDialog.close());
byId("account-switch").addEventListener("click", () => {
  updateAuthMode(authMode === "login" ? "register" : "login");
  byId("account-username").focus();
});
for (const tab of document.querySelectorAll("[data-auth-mode]")) {
  tab.addEventListener("click", () => {
    updateAuthMode(tab.dataset.authMode);
    byId("account-username").focus();
  });
}

// Click outside account dialog backdrop to close
accountDialog.addEventListener("click", event => {
  if (event.target === accountDialog) {
    const r = accountDialog.getBoundingClientRect();
    if (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom) {
      accountDialog.close();
    }
  }
});

// Password visibility toggle
if (byId("password-toggle")) {
  byId("password-toggle").addEventListener("click", () => {
    const input = byId("account-password");
    const showing = input.type === "text";
    input.type = showing ? "password" : "text";
    byId("password-toggle").textContent = showing ? "显示" : "隐藏";
    byId("password-toggle").setAttribute("aria-label", showing ? "显示密码" : "隐藏密码");
  });
}

accountDialog.addEventListener("close", () => {
  const pw = byId("account-password");
  pw.value = "";
  pw.type = "password";
  if (byId("password-toggle")) byId("password-toggle").textContent = "显示";
  const target = dialogTrigger?.hidden ? byId("account-logout") : dialogTrigger;
  target?.focus();
});

byId("account-form").addEventListener("submit", async event => {
  event.preventDefault();
  const mode = authMode;
  const submit = byId("account-submit");
  const changeMode = byId("account-switch");
  if (state.pending) return;
  submit.disabled = true;
  changeMode.disabled = true;
  setText(submit, mode === "register" ? "正在注册…" : "正在登录…");
  await mutate("auth", "account-feedback", async () => {
    const session = await request(mode === "register" ? "/api/register" : "/api/login", {
      username: byId("account-username").value.trim(),
      password: byId("account-password").value,
    });
    useSession(session);
    setMessage("page-feedback");
    accountDialog.close();
  });
  setText(submit, mode === "register" ? "创建账号并登录" : "登录");
  submit.disabled = false;
  changeMode.disabled = false;
});

byId("account-logout").addEventListener("click", async () => {
  await mutate("logout", "page-feedback", async () => {
    useSession(await request("/api/logout", {}));
  });
  if (!state.session?.user) byId("account-open").focus();
});

byId("upload-submit").addEventListener("click", event => {
  if (state.session?.user) return;
  event.preventDefault();
  openAuth(event.currentTarget);
});

uploadForm.addEventListener("submit", event => {
  event.preventDefault();
  if (!state.session?.user) {
    openAuth(byId("upload-submit"));
    return;
  }
  const nameInput = byId("bot-name");
  const fileInput = byId("bot-source");
  const draftName = nameInput.value;
  const name = draftName.trim();
  const file = fileInput.files[0];
  if (!name) {
    setMessage("upload-feedback", "请填写 Bot 名称。", "error");
    nameInput.focus();
    return;
  }
  if (!validSource(file)) {
    setMessage("upload-feedback", sourceHelp(), "error");
    fileInput.focus();
    return;
  }

  void mutate("upload", "upload-feedback", async () => {
    const source = await readSource(file);
    const result = await request("/api/bots", { name, filename: file.name, source });
    state.myBots = [result.bot, ...(state.myBots ?? []).filter(bot => bot.id !== result.bot.id)];
    if (state.leaderboard && !state.leaderboard.some(bot => bot.id === result.bot.id)) state.leaderboard.push(result.bot);
    if (result.challenge) cacheNewChallenge(result.challenge);
    if (nameInput.value === draftName && fileInput.files[0] === file) {
      uploadForm.reset();
      syncFileDisplay("bot-source");
    }
    const label = botLabel(result.bot);
    const message = result.challenge ? `已保存 ${label}，首组挑战已排队。` : result.warning || `已保存 ${label}。你可以切换到“发起挑战”指定对手。`;
    setMessage("upload-feedback", message, result.challenge ? "success" : "");
  });
});

byId("challenge-form").addEventListener("submit", event => {
  event.preventDefault();
  if (!state.session?.user) {
    openAuth(byId("challenge-submit"));
    return;
  }
  if (!ownSelect.value) {
    setMessage("challenge-feedback", "请选择要参赛的 Bot 版本。", "error");
    ownSelect.focus();
    return;
  }
  const requestBody = { bot_id: ownSelect.value };
  if (opponentSelect.value) requestBody.opponent_id = opponentSelect.value;
  void mutate("challenge", "challenge-feedback", async () => {
    const { challenge } = await request("/api/challenges", requestBody);
    cacheNewChallenge(challenge);
    setMessage("challenge-feedback", `已排队，将对战 ${botLabel(challenge.b)}。`, "success");
  });
});

byId("bot-search").addEventListener("input", renderLeaderboard);
byId("mine-filter").addEventListener("change", renderLeaderboard);
byId("refresh").addEventListener("click", refresh);
for (const select of [ownSelect, opponentSelect]) select.addEventListener("blur", () => queueMicrotask(renderControls));
historyList.addEventListener("focusout", () => queueMicrotask(renderHistory));
document.addEventListener("visibilitychange", refresh);
window.addEventListener("online", refresh);
setInterval(refresh, 15_000);
renderControls();
void refresh();
