const configured = document.querySelector('meta[name="tetra-api-base"]')?.content.trim();
const base = new URL(configured || '/', location.href);
const separateFrontend = base.origin !== location.origin;
const storageKey = `tetra-session:${base.href}`;
let memoryToken = null;

export function apiUrl(path) {
  return new URL(path.slice(1), base);
}

export function rememberSessionToken(token) {
  if (!separateFrontend || !/^[A-Za-z0-9_-]{43}$/.test(token ?? '')) return;
  memoryToken = token;
  try {
    sessionStorage.setItem(storageKey, token);
  } catch { /* In-memory login remains available when tab storage is disabled. */ }
}

export function apiFetch(path, options = {}) {
  if (!separateFrontend) return fetch(apiUrl(path), {credentials: 'same-origin', ...options});
  let token = memoryToken;
  try {
    token = sessionStorage.getItem(storageKey) || token;
  } catch { /* The same page can still keep its session in memory. */ }
  const headers = new Headers(options.headers);
  if (token) headers.set('Authorization', `Bearer ${token}`);
  return fetch(apiUrl(path), {...options, headers, credentials: 'omit'});
}

for (const link of document.querySelectorAll('[data-arena-referee]')) {
  link.href = apiUrl('/api/referee/download');
}
