import WASI from './vendor/browser-wasi-shim/wasi.js';
import {ConsoleStdout, File as WasiFile, OpenFile} from './vendor/browser-wasi-shim/fs_mem.js';

const BUILT_IN_BOTS = new Set(['weak', 'medium', 'strong', 'neno', 'northstar']);
const MAX_BOT_BYTES = 16 * 1024 * 1024;
const encoder = new TextEncoder();
const stdout = [];
let bot;

async function loadBotBytes(id, wasmFile) {
  if (wasmFile !== null) {
    const size = wasmFile instanceof ArrayBuffer ? wasmFile.byteLength : wasmFile instanceof Blob ? wasmFile.size : 0;
    if (!size || size > MAX_BOT_BYTES) throw new Error('请选择不超过 16 MiB 的 WebAssembly Bot 文件。');
    return wasmFile instanceof ArrayBuffer ? wasmFile : wasmFile.arrayBuffer();
  }
  if (!BUILT_IN_BOTS.has(id)) throw new Error('找不到这个本地 Bot。');
  const response = await fetch(new URL(`./bots/${id}.wasm`, import.meta.url));
  if (!response.ok) throw new Error(`无法下载本地 Bot（HTTP ${response.status}）。`);
  return response.arrayBuffer();
}

async function openBot(id, wasmFile) {
  const bytes = await loadBotBytes(id, wasmFile);
  const wasi = new WASI([], [], [
    new OpenFile(new WasiFile([])),
    ConsoleStdout.lineBuffered(line => stdout.push(line)),
    ConsoleStdout.lineBuffered(() => {}),
  ]);
  const {instance} = await WebAssembly.instantiate(bytes, {wasi_snapshot_preview1: wasi.wasiImport});
  const {memory, _initialize, tetra_feed, malloc, free} = instance.exports;
  if (!(memory instanceof WebAssembly.Memory) || ![_initialize, tetra_feed, malloc, free].every(value => typeof value === 'function')) {
    throw new Error('这个 WebAssembly 文件不符合本地 Bot 接口。');
  }
  wasi.initialize(instance);
  bot = {memory, tetra_feed, malloc, free};
}

function decide({token, observation, remainingMs, bankMs}) {
  if (!bot) throw new Error('本地 Bot 尚未加载。');
  const requestId = String(token);
  const request = {
    type: 'OBSERVE',
    request_id: requestId,
    remaining_ms: Number.isFinite(remainingMs) ? Math.max(0, Math.floor(remainingMs)) : 100,
    observation,
  };
  if (Number.isFinite(bankMs)) request.bank_ms = Math.max(0, Math.floor(bankMs));
  const bytes = encoder.encode(JSON.stringify(request));
  const ptr = bot.malloc(bytes.length);
  if (!ptr) throw new Error('本地 Bot 内存不足。');
  stdout.length = 0;
  try {
    new Uint8Array(bot.memory.buffer, ptr, bytes.length).set(bytes);
    bot.tetra_feed(ptr, bytes.length);
  } finally {
    bot.free(ptr);
  }
  if (stdout.length !== 1) throw new Error('本地 Bot 应输出一条 JSONL 回复。');
  let result;
  try {
    result = JSON.parse(stdout[0]);
  } catch {
    throw new Error('本地 Bot 返回了无效的 JSON。');
  }
  if (result?.request_id !== requestId ||
      (result.type !== 'HOLD' && (result.type !== 'PLAY' || !Array.isArray(result.commands)))) {
    throw new Error('本地 Bot 的回复不符合对局协议。');
  }
  return result;
}

self.onmessage = async ({data}) => {
  try {
    let result;
    if (data.type === 'open') await openBot(data.id, data.wasmFile);
    else if (data.type === 'decide') result = decide(data);
    else throw new Error('无法识别本地 Bot 请求。');
    self.postMessage({token: data.token, result});
  } catch (error) {
    self.postMessage({token: data.token, error: error instanceof Error ? error.message : String(error)});
  }
};
