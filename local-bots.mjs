const MAX_BOT_BYTES = 16 * 1024 * 1024;
const WORKER_TIMEOUT_MS = 30_000;

export class LocalBot {
  #worker;
  #pending = new Map();
  #nextId = 0;
  #closed = false;

  static async open(id, wasmFile = null) {
    if (wasmFile !== null) {
      const size = wasmFile instanceof ArrayBuffer ? wasmFile.byteLength : wasmFile instanceof Blob ? wasmFile.size : 0;
      if (!size || size > MAX_BOT_BYTES) throw new Error('请选择不超过 16 MiB 的 WebAssembly Bot 文件。');
    }
    const bot = new LocalBot();
    try {
      await bot.#send('open', {id, wasmFile}, wasmFile !== null);
      return bot;
    } catch (error) {
      bot.close();
      throw error;
    }
  }

  constructor() {
    this.#worker = new Worker(new URL('./local-bot-worker.js', import.meta.url), {type: 'module'});
    this.#worker.onmessage = ({data}) => {
      const pending = this.#pending.get(data.token);
      if (!pending) return;
      this.#pending.delete(data.token);
      clearTimeout(pending.timer);
      if (data.error) pending.reject(new Error(data.error));
      else pending.resolve(data.result);
    };
    this.#worker.onerror = event => this.close(event.message || '本地 Bot 无法启动。');
    this.#worker.onmessageerror = () => this.close('无法读取本地 Bot 的回复。');
  }

  decide(observation, remainingMs = 100, bankMs) {
    return this.#send('decide', {observation, remainingMs, bankMs}, true);
  }

  close(reason = '本地 Bot 已关闭。') {
    if (this.#closed) return;
    this.#closed = true;
    this.#worker.terminate();
    for (const {reject, timer} of this.#pending.values()) {
      clearTimeout(timer);
      reject(new Error(reason));
    }
    this.#pending.clear();
  }

  #send(type, payload, watchdog) {
    if (this.#closed) return Promise.reject(new Error('本地 Bot 已关闭。'));
    const token = ++this.#nextId;
    return new Promise((resolve, reject) => {
      const timer = watchdog ? setTimeout(() => this.close('本地 Bot 长时间未响应。'), WORKER_TIMEOUT_MS) : undefined;
      this.#pending.set(token, {resolve, reject, timer});
      try {
        this.#worker.postMessage({type, token, ...payload});
      } catch (error) {
        clearTimeout(timer);
        this.#pending.delete(token);
        reject(error);
      }
    });
  }
}
