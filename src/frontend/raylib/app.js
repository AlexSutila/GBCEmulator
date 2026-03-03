function hasTouchUI() {
  return (
    (navigator.maxTouchPoints && navigator.maxTouchPoints > 0) ||
    window.matchMedia("(pointer: coarse)").matches ||
    "ontouchstart" in window
  );
}

// Keep CSS layout variables in sync with the *actual* rendered chrome sizes
// This avoids the canvas/touch controls being clipped on small screens
function setupDynamicChromeVars() {
  const root = document.documentElement;
  const topbar = document.querySelector(".topbar");
  const touch = document.getElementById("touch-controls");

  const update = () => {
    try {
      const topH = topbar ? Math.ceil(topbar.getBoundingClientRect().height) : 0;
      root.style.setProperty("--topbar-h", `${topH}px`);

      const touchVisible = touch && getComputedStyle(touch).display !== "none";
      const touchH = touchVisible ? Math.ceil(touch.getBoundingClientRect().height) : 0;
      root.style.setProperty("--touchbar-h", `${touchH}px`);
    } catch {
      /* ignore */
    }
  };

  update();
  window.addEventListener("resize", update, {passive: true});
  window.addEventListener("orientationchange", update, {passive: true});
  document.addEventListener("fullscreenchange", update);
  document.addEventListener("gesturestart", e => e.preventDefault());
  document.addEventListener("gesturechange", e => e.preventDefault());
  document.addEventListener("gestureend", e => e.preventDefault());

  if (window.ResizeObserver) {
    const ro = new ResizeObserver(update);
    if (topbar) ro.observe(topbar);
    if (touch) ro.observe(touch);
  }
}

// Run as early as possible (this script is at the end of <body>, so DOM is ready)
setupDynamicChromeVars();

function setupCanvasFocus(canvas) {
  canvas.tabIndex = 0;
  canvas.focus();

  window.addEventListener(
    "keydown",
    (e) => {
      const block = new Set(["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space", "Enter", "Backspace"]);
      if (block.has(e.code)) e.preventDefault();
    },
    {passive: false}
  );

  window.addEventListener("pointerdown", () => canvas.focus());
}

const ACTIONS = [
  {id: 0, name: "Right", def: "ArrowRight"},
  {id: 1, name: "Left", def: "ArrowLeft"},
  {id: 2, name: "Up", def: "ArrowUp"},
  {id: 3, name: "Down", def: "ArrowDown"},
  {id: 4, name: "A", def: "KeyZ"},
  {id: 5, name: "B", def: "KeyX"},
  {id: 6, name: "Select", def: "Backspace"},
  {id: 7, name: "Start", def: "Enter"},
];

const STATE_HOTKEY_ACTIONS = [
  {id: "quicksave", name: "Quick Save", def: "F4"},
  {id: "quickload", name: "Quick Load", def: "F8"},
];

const KEYMAP_KEY = "gbc_keymap_v1";
const STATE_HOTKEYS_KEY = "gbc_state_hotkeys_v1";
const VOLUME_KEY = "gbc_volume_v1";
const SRAM_KEY_PREFIX = "gbc_save_v1:";
const STATE_INDEX_KEY_PREFIX = "gbc_state_index_v1:";
const STATE_ENTRY_KEY_PREFIX = "gbc_state_entry_v1:";
const STATE_MAX_ENTRIES = 32;
const STATE_MAX_QUICK_ENTRIES = 10;
const saveRuntimeState = {
  activeRomId: "",
  activeRomName: "",
  pendingSaveRequest: null,
  pendingLoadStateId: "",
  pendingStateThumb: "",
};

function bytesToHex(bytes) {
  let out = "";
  for (let i = 0; i < bytes.length; i++) {
    out += bytes[i].toString(16).padStart(2, "0");
  }
  return out;
}

function bytesToBase64(bytes) {
  let binary = "";
  const chunkSize = 0x8000;
  for (let i = 0; i < bytes.length; i += chunkSize) {
    const end = Math.min(bytes.length, i + chunkSize);
    let chunk = "";
    for (let j = i; j < end; j++) chunk += String.fromCharCode(bytes[j]);
    binary += chunk;
  }
  return btoa(binary);
}

function base64ToBytes(b64) {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i) & 0xff;
  return out;
}

function fnv1a64Hex(bytes) {
  let h = 0xcbf29ce484222325n;
  const prime = 0x100000001b3n;
  const mask = 0xffffffffffffffffn;
  for (let i = 0; i < bytes.length; i++) {
    h ^= BigInt(bytes[i]);
    h = (h * prime) & mask;
  }
  return h.toString(16).padStart(16, "0");
}

async function computeRomContentId(bytes) {
  try {
    if (globalThis.crypto?.subtle?.digest) {
      const digest = await globalThis.crypto.subtle.digest("SHA-256", bytes);
      return `sha256:${bytesToHex(new Uint8Array(digest))}`;
    }
  } catch (err) {
    console.warn("SHA-256 unavailable, falling back to FNV-1a:", err);
  }
  return `fnv1a64:${fnv1a64Hex(bytes)}`;
}

function getActiveSramStorageKey() {
  if (!saveRuntimeState.activeRomId) return "";
  return `${SRAM_KEY_PREFIX}${saveRuntimeState.activeRomId}`;
}

function getStateIndexStorageKey() {
  if (!saveRuntimeState.activeRomId) return "";
  return `${STATE_INDEX_KEY_PREFIX}${saveRuntimeState.activeRomId}`;
}

function getStateEntryStorageKey(id) {
  if (!saveRuntimeState.activeRomId) return "";
  return `${STATE_ENTRY_KEY_PREFIX}${saveRuntimeState.activeRomId}:${String(id || "")}`;
}

function parseJson(raw) {
  if (!raw || typeof raw !== "string") return null;
  try {
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

function normalizeStateKind(kind) {
  return kind === "manual" ? "manual" : "quick";
}

function sanitizeStateLabel(label, maxLen = 48) {
  const text = String(label || "").trim().replace(/\s+/g, " ");
  if (!text) return "";
  return text.slice(0, maxLen);
}

function makeStateId() {
  const t = Date.now().toString(36);
  const r = Math.random().toString(36).slice(2, 8);
  return `${t}${r}`;
}

function normalizeStateMeta(entry) {
  if (!entry || typeof entry !== "object") return null;
  const id = String(entry.id || "");
  if (!id) return null;
  const kind = normalizeStateKind(entry.kind);
  const createdAt = Number(entry.createdAt);
  const len = Number(entry.len);
  return {
    id,
    kind,
    label: sanitizeStateLabel(entry.label),
    createdAt: Number.isFinite(createdAt) ? createdAt : Date.now(),
    len: Number.isFinite(len) && len > 0 ? len : 0,
    thumb: typeof entry.thumb === "string" ? entry.thumb : "",
  };
}

function stateDisplayLabel(entry) {
  if (entry?.label) return entry.label;
  return entry?.kind === "manual" ? "Manual Save" : "Quick Save";
}

function listStateEntriesFromIndex(index) {
  const arr = Array.isArray(index?.entries) ? index.entries : [];
  const out = [];
  for (const raw of arr) {
    const meta = normalizeStateMeta(raw);
    if (meta) out.push(meta);
  }
  out.sort((a, b) => b.createdAt - a.createdAt);
  return out;
}

function readStateIndex() {
  const key = getStateIndexStorageKey();
  if (!key) return {v: 2, entries: []};
  const obj = parseJson(localStorage.getItem(key));
  if (!obj || typeof obj !== "object") return {v: 2, entries: []};
  return {v: 2, entries: listStateEntriesFromIndex(obj)};
}

function writeStateIndex(index) {
  const key = getStateIndexStorageKey();
  if (!key) return false;
  try {
    const payload = {
      v: 2,
      entries: listStateEntriesFromIndex(index),
    };
    localStorage.setItem(key, JSON.stringify(payload));
    return true;
  } catch (err) {
    console.warn("Failed to write savestate index:", err);
    return false;
  }
}

function readStatePayloadById(id) {
  const key = getStateEntryStorageKey(id);
  if (!key) return null;
  const obj = parseJson(localStorage.getItem(key));
  if (!obj || typeof obj !== "object" || typeof obj.b64 !== "string" || !obj.b64) {
    return null;
  }
  try {
    return base64ToBytes(obj.b64);
  } catch {
    return null;
  }
}

function writeStatePayloadById(id, bytes) {
  const key = getStateEntryStorageKey(id);
  if (!key || !(bytes instanceof Uint8Array) || bytes.length === 0) return false;
  try {
    localStorage.setItem(key, JSON.stringify({
      v: 2,
      len: bytes.length,
      b64: bytesToBase64(bytes),
    }));
    return true;
  } catch (err) {
    console.warn("Failed to write savestate payload:", err);
    try {
      localStorage.removeItem(key);
    } catch {
    }
    return false;
  }
}

function deleteStatePayloadById(id) {
  const key = getStateEntryStorageKey(id);
  if (!key) return;
  try {
    localStorage.removeItem(key);
  } catch {
  }
}

function pruneStateIndex(index) {
  const out = [];
  let quickCount = 0;
  for (const entry of listStateEntriesFromIndex(index)) {
    const isQuick = entry.kind === "quick";
    if (isQuick && quickCount >= STATE_MAX_QUICK_ENTRIES) {
      deleteStatePayloadById(entry.id);
      continue;
    }
    if (out.length >= STATE_MAX_ENTRIES) {
      deleteStatePayloadById(entry.id);
      continue;
    }
    if (isQuick) quickCount += 1;
    out.push(entry);
  }
  return {v: 2, entries: out};
}

function captureStateThumbnailDataUrl() {
  const src = globalThis.Module?.canvas;
  if (!src) return "";
  try {
    const w = 80;
    const h = 72;
    if (typeof src.toDataURL === "function") {
      const c = document.createElement("canvas");
      c.width = w;
      c.height = h;
      const ctx = c.getContext("2d");
      if (!ctx) return "";
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(src, 0, 0, w, h);
      return c.toDataURL("image/webp", 0.72);
    }
  } catch {
    return "";
  }
  return "";
}

function rgbaThumbToDataUrl(bytes, w, h) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== w * h * 4) return "";
  try {
    const c = document.createElement("canvas");
    c.width = w;
    c.height = h;
    const ctx = c.getContext("2d");
    if (!ctx) return "";
    const image = ctx.createImageData(w, h);
    image.data.set(bytes);
    ctx.putImageData(image, 0, 0);
    return c.toDataURL("image/webp", 0.72);
  } catch {
    return "";
  }
}

function setActiveRomSaveIdentity(romId, romName) {
  saveRuntimeState.activeRomId = String(romId || "");
  saveRuntimeState.activeRomName = String(romName || "");
  saveRuntimeState.pendingSaveRequest = null;
  saveRuntimeState.pendingLoadStateId = "";
  saveRuntimeState.pendingStateThumb = "";
}

globalThis.IroGBSaves = {
  setActiveRomSaveIdentity,
  getActiveSramKey() {
    return getActiveSramStorageKey();
  },
  getActiveStateIndexKey() {
    return getStateIndexStorageKey();
  },
  setPendingStateSaveRequest(kind, label = "") {
    saveRuntimeState.pendingSaveRequest = {
      kind: normalizeStateKind(kind),
      label: sanitizeStateLabel(label),
    };
  },
  setPendingStateThumbnailRgba(bytes, width, height) {
    const w = Number(width);
    const h = Number(height);
    if (!Number.isFinite(w) || !Number.isFinite(h) || w <= 0 || h <= 0) {
      return false;
    }
    saveRuntimeState.pendingStateThumb = rgbaThumbToDataUrl(bytes, w | 0, h | 0);
    return saveRuntimeState.pendingStateThumb.length > 0;
  },
  requestLoadStateById(id) {
    saveRuntimeState.pendingLoadStateId = String(id || "");
  },
  listActiveStates() {
    return listStateEntriesFromIndex(readStateIndex());
  },
  deleteStateById(id) {
    const key = String(id || "");
    if (!key) return false;
    const index = readStateIndex();
    const before = listStateEntriesFromIndex(index);
    const after = before.filter((e) => e.id !== key);
    if (after.length === before.length) return false;
    deleteStatePayloadById(key);
    return writeStateIndex({v: 2, entries: after});
  },
  loadActiveSram() {
    try {
      const key = getActiveSramStorageKey();
      if (!key) return null;

      const raw = localStorage.getItem(key);
      if (!raw) return null;

      let payload = null;
      try {
        payload = JSON.parse(raw);
      } catch {
        // Accept legacy/plain values if needed.
      }

      if (payload && typeof payload.b64 === "string") {
        return base64ToBytes(payload.b64);
      }
      if (typeof raw === "string" && raw) {
        return base64ToBytes(raw);
      }
      return null;
    } catch (err) {
      console.warn("Failed to read SRAM from localStorage:", err);
      return null;
    }
  },
  saveActiveSram(bytes) {
    try {
      if (!(bytes instanceof Uint8Array)) return false;
      const key = getActiveSramStorageKey();
      if (!key) return false;

      const payload = {
        v: 1,
        romId: saveRuntimeState.activeRomId,
        romName: saveRuntimeState.activeRomName,
        len: bytes.length,
        updatedAt: Date.now(),
        b64: bytesToBase64(bytes),
      };
      localStorage.setItem(key, JSON.stringify(payload));
      return true;
    } catch (err) {
      console.warn("Failed to write SRAM to localStorage:", err);
      return false;
    }
  },
  loadActiveState() {
    const list = listStateEntriesFromIndex(readStateIndex());
    const requestedId = saveRuntimeState.pendingLoadStateId;
    saveRuntimeState.pendingLoadStateId = "";

    if (requestedId) {
      const payload = readStatePayloadById(requestedId);
      if (payload instanceof Uint8Array && payload.length > 0) {
        return payload;
      }
    }

    for (const entry of list) {
      const payload = readStatePayloadById(entry.id);
      if (payload instanceof Uint8Array && payload.length > 0) {
        return payload;
      }
    }
    return null;
  },
  saveActiveState(bytes) {
    if (!(bytes instanceof Uint8Array) || bytes.length === 0) return false;
    if (!saveRuntimeState.activeRomId) return false;

    const now = Date.now();
    const req = saveRuntimeState.pendingSaveRequest || {kind: "quick", label: ""};
    saveRuntimeState.pendingSaveRequest = null;

    const kind = normalizeStateKind(req.kind);
    const rawLabel = sanitizeStateLabel(req.label);
    const label = rawLabel || (kind === "manual" ? "Manual Save" : "Quick Save");
    const id = makeStateId();
    const pendingThumb = saveRuntimeState.pendingStateThumb || captureStateThumbnailDataUrl();
    saveRuntimeState.pendingStateThumb = "";

    if (!writeStatePayloadById(id, bytes)) {
      return false;
    }

    const index = readStateIndex();
    index.entries.unshift({
      id,
      kind,
      label,
      createdAt: now,
      len: bytes.length,
      thumb: pendingThumb,
    });
    const pruned = pruneStateIndex(index);
    if (!writeStateIndex(pruned)) {
      deleteStatePayloadById(id);
      return false;
    }
    return true;
  },
};

function defaultKeymap() {
  const m = {};
  for (const a of ACTIONS) m[a.id] = a.def;
  return m;
}

function defaultStateHotkeys() {
  const m = {};
  for (const a of STATE_HOTKEY_ACTIONS) m[a.id] = a.def;
  return m;
}

function loadKeymap() {
  try {
    const obj = JSON.parse(localStorage.getItem(KEYMAP_KEY) || "{}");
    const d = defaultKeymap();
    for (const a of ACTIONS) {
      const v = obj?.[String(a.id)] || obj?.[a.id];
      d[a.id] = (typeof v === "string" && v) ? v : d[a.id];
    }
    return d;
  } catch {
    return defaultKeymap();
  }
}

function saveKeymap(map) {
  try { localStorage.setItem(KEYMAP_KEY, JSON.stringify(map)); } catch {}
}

function loadStateHotkeys() {
  try {
    const obj = JSON.parse(localStorage.getItem(STATE_HOTKEYS_KEY) || "{}");
    const d = defaultStateHotkeys();
    for (const a of STATE_HOTKEY_ACTIONS) {
      const v = obj?.[String(a.id)] || obj?.[a.id];
      d[a.id] = (typeof v === "string" && v) ? v : d[a.id];
    }
    return d;
  } catch {
    return defaultStateHotkeys();
  }
}

function saveStateHotkeys(map) {
  try { localStorage.setItem(STATE_HOTKEYS_KEY, JSON.stringify(map)); } catch {}
}

function loadVolume() {
  try {
    const v = Number(localStorage.getItem(VOLUME_KEY));
    if (Number.isFinite(v)) return Math.min(1, Math.max(0, v));
    return 1.0;
  } catch {
    return 1.0;
  }
}

function saveVolume(v) {
  try { localStorage.setItem(VOLUME_KEY, String(v)); } catch {}
}

function prettyKey(code) {
  const s = String(code || "");
  if (!s) return "(none)";
  if (s.startsWith("Key")) return s.slice(3);
  if (s.startsWith("Digit")) return s.slice(5);
  if (s === "ArrowUp") return "↑";
  if (s === "ArrowDown") return "↓";
  if (s === "ArrowLeft") return "←";
  if (s === "ArrowRight") return "→";
  if (s === "Space") return "Space";
  if (s === "Backspace") return "Backspace";
  if (s === "Enter") return "Enter";
  if (s === "Escape") return "Escape";
  if (s.startsWith("Numpad")) return s.replace("Numpad", "Num ");
  return s;
}

function isTextyTarget(t) {
  const el = t && (t.nodeType === 1 ? t : null);
  if (!el) return false;
  const tag = el.tagName?.toLowerCase?.() || "";
  if (tag === "input" || tag === "textarea" || tag === "select") return true;
  return !!el.isContentEditable;
}

function updateHintFromKeymap(keymap, stateHotkeys) {
  const hint = document.getElementById("hint");
  if (!hint) return;
  const get = (id) => `<code>${prettyKey(keymap[id])}</code>`;
  const qsave = `<code>${prettyKey(stateHotkeys?.quicksave || "F4")}</code>`;
  const qload = `<code>${prettyKey(stateHotkeys?.quickload || "F8")}</code>`;
  hint.innerHTML =
    `Keyboard: ${get(2)} ${get(3)} ${get(1)} ${get(0)} ` +
    `${get(4)}=A ${get(5)}=B ${get(7)}=Start ${get(6)}=Select ` +
    `${qsave}=Quicksave ${qload}=Quickload`;
}

function bindKeyboard(Module, getKeymap, opts) {
  const callSetBtn = (btn, pressed) => {
    if (typeof Module._emscripten_set_button === "function") {
      Module._emscripten_set_button(btn, pressed ? 1 : 0);
    }
  };
  const clearAll = () => {
    if (typeof Module._emscripten_clear_buttons === "function") {
      Module._emscripten_clear_buttons();
    }
  };

  const down = new Set();
  const codeToAction = () => {
    const map = getKeymap();
    const rev = new Map();
    for (const a of ACTIONS) rev.set(map[a.id], a.id);
    return rev;
  };

  window.addEventListener("keydown", (e) => {
    if (opts?.isCapturing?.()) return;
    if (isTextyTarget(e.target)) return;
    if (e.repeat) return;

    const rev = codeToAction();
    const btn = rev.get(e.code);
    if (btn == null) return;

    e.preventDefault();
    if (down.has(e.code)) return;
    down.add(e.code);
    callSetBtn(btn, true);
  }, {passive: false});

  window.addEventListener("keyup", (e) => {
    if (opts?.isCapturing?.()) return;

    const rev = codeToAction();
    const btn = rev.get(e.code);
    if (btn == null) return;

    e.preventDefault();
    down.delete(e.code);
    callSetBtn(btn, false);
  }, {passive: false});

  window.addEventListener("blur", () => { down.clear(); clearAll(); });
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) { down.clear(); clearAll(); }
  });

  return {clearAll};
}

function bindTouchButtons(Module) {
  const callSetBtn = (btn, pressed) => {
    if (typeof Module._emscripten_set_button === "function") {
      Module._emscripten_set_button(btn, pressed ? 1 : 0);
    }
  };

  const clearAll = () => {
    if (typeof Module._emscripten_clear_buttons === "function") {
      Module._emscripten_clear_buttons();
    }
  };

  const buttons = document.querySelectorAll("[data-btn]");
  for (const el of buttons) {
    const btn = Number(el.dataset.btn);

    const down = (e) => {
      e.preventDefault();
      el.setPointerCapture?.(e.pointerId);
      callSetBtn(btn, true);
    };

    const up = (e) => {
      e.preventDefault();
      callSetBtn(btn, false);
    };

    el.addEventListener("pointerdown", down, {passive: false});
    el.addEventListener("pointerup", up, {passive: false});
    el.addEventListener("pointercancel", up, {passive: false});
    el.addEventListener("contextmenu", (e) => e.preventDefault());
  }

  window.addEventListener("blur", clearAll);
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) clearAll();
  });

  return {clearAll};
}

function bindSavestateHotkeys(Module, opts) {
  const callIfAvailable = (name) => {
    try {
      if (typeof Module[name] === "function") Module[name]();
    } catch (err) {
      console.warn(`Failed to call ${name}:`, err);
    }
  };

  window.addEventListener(
    "keydown",
    (e) => {
      if (opts?.isCapturing?.()) return;
      if (isTextyTarget(e.target)) return;
      if (e.repeat) return;

      const hotkeys = opts?.getHotkeys?.() || defaultStateHotkeys();
      const quicksaveKey = hotkeys.quicksave || "F4";
      const quickloadKey = hotkeys.quickload || "F8";

      if (e.code === quicksaveKey) {
        e.preventDefault();
        if (typeof opts?.onQuickSave === "function") {
          opts.onQuickSave();
        } else {
          callIfAvailable("_emscripten_request_quicksave");
        }
      } else if (e.code === quickloadKey) {
        e.preventDefault();
        if (typeof opts?.onQuickLoad === "function") {
          opts.onQuickLoad();
        } else {
          callIfAvailable("_emscripten_request_quickload");
        }
      }
    },
    {passive: false}
  );
}

function setupFullscreen(canvas) {
  const btn = document.getElementById("fsButton");
  const card = document.getElementById("canvasCard");

  const request = async () => {
    try {
      if (!document.fullscreenElement) {
        await (card.requestFullscreen?.() || canvas.requestFullscreen?.());
      } else {
        await document.exitFullscreen?.();
      }
    } catch (err) {
      console.warn("Fullscreen failed:", err);
    }
  };

  btn.addEventListener("click", request);
  document.addEventListener("fullscreenchange", () => {
    btn.textContent = document.fullscreenElement ? "Exit" : "Fullscreen";
  });
}

// ---- Added helpers: remote fetch + zip unzip + ROM picking ----

const ROM_RE = /\.(gb|gbc)$/i;
const ZIP_RE = /\.zip$/i;

function basename(path) {
  const s = String(path).replace(/\\/g, "/");
  const parts = s.split("/");
  return parts[parts.length - 1] || s;
}

function prettyBytes(n) {
  if (!Number.isFinite(n)) return "";
  const units = ["B", "KB", "MB", "GB"];
  let i = 0, v = n;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  return `${v.toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
}

function extractRomTitle(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 0x144) return "";
  const start = 0x134;
  const end = Math.min(bytes.length, start + 16);
  let out = "";
  for (let i = start; i < end; i++) {
    const b = bytes[i];
    if (b === 0x00) break;
    if (b >= 0x20 && b <= 0x7e) out += String.fromCharCode(b);
  }
  return out.replace(/\s+/g, " ").trim();
}

function guessNameFromUrl(u) {
  try {
    const url = new URL(u, window.location.href);
    const name = decodeURIComponent(url.pathname.split("/").pop() || "");
    return name || "remote";
  } catch {
    return "remote";
  }
}

async function ensureFflateLoaded() {
  if (window.fflate?.unzip) return;

  await new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = "https://unpkg.com/fflate/umd/index.js";
    s.async = true;
    s.onload = resolve;
    s.onerror = () => reject(new Error("Failed to load unzip library (fflate)."));
    document.head.appendChild(s);
  });

  if (!window.fflate?.unzip) {
    throw new Error("Unzip library loaded, but API missing.");
  }
}

function pickRomFromMany(entries) {
  const dlg = document.getElementById("romPicker");
  const supportsDialog = !!dlg?.showModal;

  // Default to largest file
  let defaultIdx = 0;
  for (let i = 1; i < entries.length; i++) {
    if (entries[i].size > entries[defaultIdx].size) defaultIdx = i;
  }

  if (!supportsDialog) {
    const list = entries.map((e, i) => `${i}: ${e.name} (${prettyBytes(e.size)})`).join("\n");
    const ans = window.prompt(`Multiple ROMs found.\n\n${list}\n\nEnter the number to load:`, String(defaultIdx));
    if (ans == null) throw new Error("Cancelled.");
    const idx = Number(ans);
    if (!Number.isInteger(idx) || idx < 0 || idx >= entries.length) throw new Error("Invalid selection.");
    return Promise.resolve(entries[idx]);
  }

  const listEl = document.getElementById("romList");
  const countEl = document.getElementById("romPickerCount");

  countEl.textContent = `${entries.length} ROMs found`;
  listEl.innerHTML = "";

  entries.forEach((e, i) => {
    const id = `romradio_${i}`;
    const label = document.createElement("label");
    label.className = "rom-item";
    label.htmlFor = id;

    const radio = document.createElement("input");
    radio.type = "radio";
    radio.name = "rompick";
    radio.value = String(i);
    radio.id = id;
    radio.checked = (i === defaultIdx);

    const name = document.createElement("div");
    name.className = "rom-name";
    name.textContent = e.name;

    const size = document.createElement("div");
    size.className = "rom-size";
    size.textContent = prettyBytes(e.size);

    label.appendChild(radio);
    label.appendChild(name);
    label.appendChild(size);

    // double-click / double-tap loads immediately
    label.addEventListener("dblclick", () => {
      dlg.returnValue = "ok";
      dlg.close();
    });

    listEl.appendChild(label);
  });

  dlg.showModal();

  return new Promise((resolve, reject) => {
    dlg.addEventListener("close", () => {
      if (dlg.returnValue === "cancel") return reject(new Error("Cancelled."));
      const chosen = dlg.querySelector('input[name="rompick"]:checked');
      const idx = Number(chosen?.value ?? defaultIdx);
      resolve(entries[idx]);
    }, {once: true});
  });
}

async function unzipAndSelectRom(zipBytes) {
  await ensureFflateLoaded();

  const files = await new Promise((resolve, reject) => {
    window.fflate.unzip(zipBytes, (err, out) => {
      if (err) reject(err);
      else resolve(out);
    });
  });

  const roms = [];
  for (const [path, bytes] of Object.entries(files)) {
    if (!bytes || !bytes.length) continue;
    if (path.endsWith("/")) continue;
    if (!ROM_RE.test(path)) continue;
    roms.push({name: basename(path), bytes, size: bytes.length});
  }

  if (roms.length === 0) {
    throw new Error("No .gb/.gbc files found in the ZIP.");
  }
  if (roms.length === 1) return roms[0];
  return await pickRomFromMany(roms);
}

function isEmscriptenUnwind(e) {
  // Emscripten can throw either a string or an Error-ish object
  if (e === "unwind") return true;
  const msg = (e && (e.message || e.toString?.())) ? String(e.message || e.toString()) : String(e);
  return msg.toLowerCase().includes("unwind");
}

function closeDialogSafe(id) {
  const dlg = document.getElementById(id);
  try {
    if (dlg?.open) dlg.close();
  } catch {
  }
}

const RECENT_KEY = "gbc_recent_rom_urls_v1";
const RECENT_MAX = 8;

function loadRecentUrls() {
  try {
    const arr = JSON.parse(localStorage.getItem(RECENT_KEY) || "[]");
    return Array.isArray(arr) ? arr.filter((s) => typeof s === "string") : [];
  } catch {
    return [];
  }
}

function saveRecentUrls(urls) {
  try {
    localStorage.setItem(RECENT_KEY, JSON.stringify(urls.slice(0, RECENT_MAX)));
  } catch {
  }
}

function addRecentUrl(url) {
  const u = String(url || "").trim();
  if (!u) return;
  const urls = loadRecentUrls();
  const next = [u, ...urls.filter((x) => x !== u)];
  saveRecentUrls(next);
}

function renderRecentUrls({onPick}) {
  const wrap = document.getElementById("recentWrap");
  const list = document.getElementById("recentList");
  if (!wrap || !list) return;

  const urls = loadRecentUrls();
  if (!urls.length) {
    wrap.style.display = "none";
    list.innerHTML = "";
    return;
  }

  wrap.style.display = "grid";
  list.innerHTML = "";

  for (const u of urls) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "recent-chip";

    const txt = document.createElement("span");
    txt.className = "txt";
    txt.textContent = u;

    btn.appendChild(txt);

    btn.addEventListener("click", () => onPick?.(u));
    btn.addEventListener("dblclick", () => onPick?.(u, {load: true}));

    list.appendChild(btn);
  }
}

// ---- Emscripten module ----
var Module = {
  canvas: document.getElementById("canvas"),

  printErr(text) {
    console.error(text);
  },

  onRuntimeInitialized() {
    const canvas = Module.canvas;
    const loadButton = document.getElementById("loadButton");
    const loadDlg = document.getElementById("loadDialog");
    const loadFileInput = document.getElementById("loadFileInput");
    const loadUrlInput = document.getElementById("loadUrlInput");
    const loadUrlGo = document.getElementById("loadUrlGo");
    const recentClear = document.getElementById("recentClear");
    const status = document.getElementById("status");
    const statesButton = document.getElementById("statesButton");
    const savestateDlg = document.getElementById("savestateDialog");
    const savestateSub = document.getElementById("savestateSub");
    const savestateList = document.getElementById("savestateList");
    const savestateLabelInput = document.getElementById("savestateLabelInput");
    const savestateCreateBtn = document.getElementById("savestateCreateBtn");
    const savestateLoadBtn = document.getElementById("savestateLoadBtn");
    const savestateDeleteBtn = document.getElementById("savestateDeleteBtn");
    const savestatePreview = document.getElementById("savestatePreview");
    const savestateMeta = document.getElementById("savestateMeta");

    const settingsButton = document.getElementById("settingsButton");
    const settingsDlg = document.getElementById("settingsDialog");
    const volumeSlider = document.getElementById("volumeSlider");
    const volumeValue = document.getElementById("volumeValue");
    const keybindList = document.getElementById("keybindList");
    const keybindReset = document.getElementById("keybindReset");
    const stateHotkeyList = document.getElementById("stateHotkeyList");
    const stateHotkeyReset = document.getElementById("stateHotkeyReset");
    const flushSramNow = () => {
      try {
        if (typeof Module._emscripten_flush_save === "function") {
          Module._emscripten_flush_save();
        }
      } catch (err) {
        console.warn("SRAM flush on page unload failed:", err);
      }
    };

    window.addEventListener("pagehide", flushSramNow);
    window.addEventListener("beforeunload", flushSramNow);

    setupCanvasFocus(canvas);
    setupFullscreen(canvas);
    const touch = bindTouchButtons(Module);

    let keymap = loadKeymap();
    let stateHotkeys = loadStateHotkeys();
    let capturing = null; // {kind: "pad"|"state", id: number|string}
    let romLoaded = false;
    let selectedStateId = "";
    const isCapturing = () => capturing != null;
    let statusBaseText = status?.textContent?.trim?.() || "Load a ROM to begin";
    let statusRestoreTimer = 0;

    const clearStatusRestoreTimer = () => {
      if (statusRestoreTimer) {
        clearTimeout(statusRestoreTimer);
        statusRestoreTimer = 0;
      }
    };

    const setStatusText = (text) => {
      if (!status) return;
      clearStatusRestoreTimer();
      status.textContent = String(text || "");
    };

    const setBaseStatus = (text) => {
      statusBaseText = String(text || "").trim() || "Running";
      clearStatusRestoreTimer();
      setStatusText(statusBaseText);
    };

    const setTemporaryStatus = (text, durationMs = 1200) => {
      clearStatusRestoreTimer();
      setStatusText(text);
      if (!romLoaded || durationMs <= 0) return;
      statusRestoreTimer = setTimeout(() => {
        setStatusText(statusBaseText);
        statusRestoreTimer = 0;
      }, durationMs);
    };

    const formatStateTime = (ms) => {
      if (!Number.isFinite(ms)) return "Unknown time";
      try {
        return new Date(ms).toLocaleString();
      } catch {
        return "Unknown time";
      }
    };

    const scheduleSavestateRefresh = () => {
      setTimeout(() => renderSavestateManager(), 140);
      setTimeout(() => renderSavestateManager(), 380);
    };

    const requestQuicksave = (kind = "quick", label = "") => {
      if (!romLoaded) {
        setStatusText("Load a ROM to save state");
        return;
      }
      IroGBSaves.setPendingStateSaveRequest(kind, label);
      if (typeof Module._emscripten_request_quicksave === "function") {
        Module._emscripten_request_quicksave();
        setTemporaryStatus(kind === "manual" ? "Saving manual state…" : "Saving quick state…");
        scheduleSavestateRefresh();
      }
    };

    const requestQuickload = (stateId = "") => {
      if (!romLoaded) {
        setStatusText("Load a ROM to load state");
        return;
      }
      if (stateId) IroGBSaves.requestLoadStateById(stateId);
      if (typeof Module._emscripten_request_quickload === "function") {
        Module._emscripten_request_quickload();
        setTemporaryStatus("Loading save state…");
      }
    };

    const renderSavestateDetail = (entry) => {
      if (!savestatePreview || !savestateMeta || !savestateLoadBtn || !savestateDeleteBtn) return;
      if (!entry) {
        savestatePreview.removeAttribute("src");
        savestateMeta.textContent = "No save state selected.";
        savestateLoadBtn.disabled = true;
        savestateDeleteBtn.disabled = true;
        return;
      }

      if (entry.thumb) {
        savestatePreview.src = entry.thumb;
      } else {
        savestatePreview.removeAttribute("src");
      }
      const typeName = entry.kind === "manual" ? "Manual" : "Quick";
      const info = [
        `Label: ${stateDisplayLabel(entry)}`,
        `Type: ${typeName}`,
        `Time: ${formatStateTime(entry.createdAt)}`,
        `Size: ${prettyBytes(entry.len)}`,
      ];
      savestateMeta.textContent = info.join("\n");
      savestateLoadBtn.disabled = false;
      savestateDeleteBtn.disabled = false;
    };

    function renderSavestateManager() {
      if (!savestateList || !savestateSub) return;

      if (!romLoaded) {
        savestateSub.textContent = "Load a ROM to manage save states.";
        savestateList.innerHTML = `<div class="savestate-empty">No ROM loaded.</div>`;
        selectedStateId = "";
        renderSavestateDetail(null);
        if (savestateCreateBtn) savestateCreateBtn.disabled = true;
        if (savestateLabelInput) savestateLabelInput.disabled = true;
        return;
      }

      if (savestateCreateBtn) savestateCreateBtn.disabled = false;
      if (savestateLabelInput) savestateLabelInput.disabled = false;

      const states = IroGBSaves.listActiveStates();
      savestateSub.textContent = `${states.length} save state${states.length === 1 ? "" : "s"} for ${saveRuntimeState.activeRomName || "ROM"}`;

      if (!states.length) {
        savestateList.innerHTML = `<div class="savestate-empty">No save states yet.</div>`;
        selectedStateId = "";
        renderSavestateDetail(null);
        return;
      }

      if (!selectedStateId || !states.find((s) => s.id === selectedStateId)) {
        selectedStateId = states[0].id;
      }

      savestateList.innerHTML = "";
      for (const entry of states) {
        const item = document.createElement("button");
        item.type = "button";
        item.className = "savestate-item" + (entry.id === selectedStateId ? " selected" : "");

        const row = document.createElement("div");
        row.className = "savestate-row";

        const title = document.createElement("div");
        title.className = "savestate-title";
        title.textContent = stateDisplayLabel(entry);

        const chip = document.createElement("span");
        chip.className = "savestate-chip " + entry.kind;
        chip.textContent = entry.kind === "manual" ? "Manual" : "Quick";

        const line1 = document.createElement("div");
        line1.className = "savestate-line";
        line1.textContent = formatStateTime(entry.createdAt);

        const line2 = document.createElement("div");
        line2.className = "savestate-line";
        line2.textContent = prettyBytes(entry.len);

        row.appendChild(title);
        row.appendChild(chip);
        row.appendChild(line1);
        row.appendChild(line2);

        item.appendChild(row);

        item.addEventListener("click", () => {
          selectedStateId = entry.id;
          renderSavestateManager();
        });
        item.addEventListener("dblclick", () => {
          selectedStateId = entry.id;
          requestQuickload(entry.id);
        });

        savestateList.appendChild(item);
      }

      const selected = states.find((s) => s.id === selectedStateId) || null;
      renderSavestateDetail(selected);
    }

    const kb = bindKeyboard(Module, () => keymap, { isCapturing });
    bindSavestateHotkeys(Module, {
      isCapturing,
      getHotkeys: () => stateHotkeys,
      onQuickSave: () => requestQuicksave("quick"),
      onQuickLoad: () => {
        if (!romLoaded) return;
        const states = IroGBSaves.listActiveStates();
        if (states.length) requestQuickload(states[0].id);
      },
    });
    updateHintFromKeymap(keymap, stateHotkeys);

    const applyVolume = (v01) => {
      const v = Math.min(1, Math.max(0, Number(v01)));
      if (typeof Module._emscripten_set_master_volume === "function") {
        Module._emscripten_set_master_volume(v);
      }
      saveVolume(v);
      if (volumeSlider) volumeSlider.value = String(Math.round(v * 100));
      if (volumeValue) volumeValue.textContent = `${Math.round(v * 100)}%`;
    };
    const applySavedVolume = () => {
      applyVolume(loadVolume());
    };
    applySavedVolume();

    function renderKeybinds() {
      if (!keybindList) return;
      keybindList.innerHTML = "";
      for (const a of ACTIONS) {
        const row = document.createElement("div");
        const active = capturing?.kind === "pad" && capturing.id === a.id;
        row.className = "keybind-item" + (active ? " capturing" : "");

        const act = document.createElement("div");
        act.className = "act";
        act.textContent = a.name;

        const key = document.createElement("div");
        key.className = "key";
        key.textContent = prettyKey(keymap[a.id]);

        const bind = document.createElement("button");
        bind.type = "button";
        bind.className = "btn bind";
        bind.textContent = active ? "Press a key…" : "Rebind";
        bind.addEventListener("click", () => {
          capturing = {kind: "pad", id: a.id};
          renderKeybinds();
          renderStateHotkeys();
        });

        row.appendChild(act);
        row.appendChild(key);
        row.appendChild(bind);
        keybindList.appendChild(row);
      }
    }
    function renderStateHotkeys() {
      if (!stateHotkeyList) return;
      stateHotkeyList.innerHTML = "";
      for (const a of STATE_HOTKEY_ACTIONS) {
        const row = document.createElement("div");
        const active = capturing?.kind === "state" && capturing.id === a.id;
        row.className = "keybind-item" + (active ? " capturing" : "");

        const act = document.createElement("div");
        act.className = "act";
        act.textContent = a.name;

        const key = document.createElement("div");
        key.className = "key";
        key.textContent = prettyKey(stateHotkeys[a.id]);

        const bind = document.createElement("button");
        bind.type = "button";
        bind.className = "btn bind";
        bind.textContent = active ? "Press a key…" : "Rebind";
        bind.addEventListener("click", () => {
          capturing = {kind: "state", id: a.id};
          renderStateHotkeys();
          renderKeybinds();
        });

        row.appendChild(act);
        row.appendChild(key);
        row.appendChild(bind);
        stateHotkeyList.appendChild(row);
      }
    }
    renderKeybinds();
    renderStateHotkeys();

    window.addEventListener("keydown", (e) => {
      if (!isCapturing()) return;
      if (!settingsDlg?.open) return;

      if (e.code === "Escape") {
        e.preventDefault();
        capturing = null;
        renderKeybinds();
        renderStateHotkeys();
        return;
      }

      if (!e.code) return;

      e.preventDefault();
      const newCode = e.code;
      if (capturing?.kind === "pad") {
        const action = capturing.id;
        const other = ACTIONS.find((x) => x.id !== action && keymap[x.id] === newCode)?.id;
        const prev = keymap[action];
        keymap[action] = newCode;
        if (other != null) keymap[other] = prev;
        saveKeymap(keymap);
      } else if (capturing?.kind === "state") {
        const action = capturing.id;
        const other = STATE_HOTKEY_ACTIONS.find((x) => x.id !== action && stateHotkeys[x.id] === newCode)?.id;
        const prev = stateHotkeys[action];
        stateHotkeys[action] = newCode;
        if (other != null) stateHotkeys[other] = prev;
        saveStateHotkeys(stateHotkeys);
      }

      updateHintFromKeymap(keymap, stateHotkeys);
      capturing = null;
      renderKeybinds();
      renderStateHotkeys();
    }, {passive: false});

    keybindReset?.addEventListener("click", () => {
      keymap = defaultKeymap();
      saveKeymap(keymap);
      updateHintFromKeymap(keymap, stateHotkeys);
      capturing = null;
      renderKeybinds();
      renderStateHotkeys();
    });

    stateHotkeyReset?.addEventListener("click", () => {
      stateHotkeys = defaultStateHotkeys();
      saveStateHotkeys(stateHotkeys);
      updateHintFromKeymap(keymap, stateHotkeys);
      capturing = null;
      renderStateHotkeys();
      renderKeybinds();
    });

    volumeSlider?.addEventListener("input", () => {
      const v = Math.min(100, Math.max(0, Number(volumeSlider.value))) / 100;
      applyVolume(v);
    });

    settingsButton?.addEventListener("click", () => {
      touch.clearAll();
      kb.clearAll();
      capturing = null;
      renderKeybinds();
      renderStateHotkeys();
      settingsDlg?.showModal?.();
    });

    statesButton?.addEventListener("click", () => {
      if (!romLoaded) {
        setStatusText("Load a ROM to manage save states");
        return;
      }
      touch.clearAll();
      kb.clearAll();
      renderSavestateManager();
      savestateDlg?.showModal?.();
      setTimeout(() => savestateLabelInput?.focus(), 0);
    });

    savestateCreateBtn?.addEventListener("click", () => {
      const label = sanitizeStateLabel(savestateLabelInput?.value || "");
      requestQuicksave("manual", label);
      if (savestateLabelInput) savestateLabelInput.value = "";
    });

    savestateLoadBtn?.addEventListener("click", () => {
      if (!selectedStateId) return;
      requestQuickload(selectedStateId);
    });

    savestateDeleteBtn?.addEventListener("click", () => {
      if (!selectedStateId) return;
      const ok = IroGBSaves.deleteStateById(selectedStateId);
      if (ok) {
        setTemporaryStatus("Save state deleted");
        selectedStateId = "";
        renderSavestateManager();
      }
    });

    savestateLabelInput?.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        savestateCreateBtn?.click();
      }
    });

    const root = document.documentElement;
    root.classList.toggle("touch-ui", hasTouchUI());

    const setTempDisabled = (disabled) => {
      loadButton.disabled = disabled;
      loadButton.classList.toggle("disabled", disabled);
      loadFileInput.disabled = disabled;
      loadUrlInput.disabled = disabled;
      loadUrlGo.disabled = disabled;
      if (statesButton) {
        const disableStates = disabled || !romLoaded;
        statesButton.disabled = disableStates;
        statesButton.classList.toggle("disabled", disableStates);
      }
    };

    const setLoadedUI = (romTitle) => {
      romLoaded = true;
      loadButton.disabled = true;
      loadButton.classList.add("disabled");
      loadButton.textContent = "Loaded";
      if (statesButton) {
        statesButton.disabled = false;
        statesButton.classList.remove("disabled");
      }
      setBaseStatus(romTitle || "Running");
      renderSavestateManager();
    };

    setTempDisabled(false);
    renderSavestateManager();

    const loadRomBytes = async (bytes, nameForUi) => {
      touch.clearAll();
      setStatusText("Hashing ROM…");
      const romId = await computeRomContentId(bytes);
      const headerTitle = extractRomTitle(bytes);
      const fallbackTitle = basename(nameForUi || "ROM").replace(/\.(gb|gbc)$/i, "").trim();
      const romTitle = headerTitle || fallbackTitle || "ROM";
      setActiveRomSaveIdentity(romId, romTitle);
      setLoadedUI(romTitle);
      FS.writeFile("/rom.bin", bytes);

      // Close any UI dialogs BEFORE entering wasm (prevents "unwind" from skipping close)
      closeDialogSafe("romPicker");
      closeDialogSafe("loadDialog");
      closeDialogSafe("savestateDialog");

      // Start on next tick so the close renders first
      setTimeout(() => {
        const reapplyVolumeAfterStart = () => {
          // Audio is initialized during emulator start; reapply persisted volume
          // after startup so the first run honors the saved setting.
          applySavedVolume();
          setTimeout(() => applySavedVolume(), 120);
        };
        try {
          Module._emscripten_start();
        } catch (e) {
          // Ignore Emscripten's internal unwind signal
          if (isEmscriptenUnwind(e)) {
            reapplyVolumeAfterStart();
            return;
          }
          reapplyVolumeAfterStart();
          throw e;
        }
        reapplyVolumeAfterStart();
      }, 0);
    };
    const handleBlobOrFile = async (blob, displayName) => {
      const name = displayName || blob?.name || "file";
      const bytes = new Uint8Array(await blob.arrayBuffer());

      if (ZIP_RE.test(name)) {
        setStatusText("Unzipping…");
        const picked = await unzipAndSelectRom(bytes);
        await loadRomBytes(picked.bytes, picked.name);
        return;
      }

      if (ROM_RE.test(name)) {
        await loadRomBytes(bytes, name);
        return;
      }

      throw new Error("Unsupported file type. Use .gb, .gbc, or .zip");
    };

    const fetchRemote = async (url) => {
      const name = guessNameFromUrl(url);
      setStatusText("Downloading…");

      // NOTE: Remote hosting must allow CORS for this to work.
      const resp = await fetch(url, {mode: "cors"});
      if (!resp.ok) throw new Error(`Download failed: HTTP ${resp.status}`);

      const ab = await resp.arrayBuffer();
      const bytes = new Uint8Array(ab);

      // Zip detection: by extension OR content-type
      const ct = resp.headers.get("content-type") || "";
      const looksZip = ZIP_RE.test(name) || ct.includes("zip");

      if (looksZip) {
        setStatusText("Unzipping…");
        const picked = await unzipAndSelectRom(bytes);
        addRecentUrl(url);
        await loadRomBytes(picked.bytes, picked.name);
        return;
      }

      if (ROM_RE.test(name)) {
        addRecentUrl(url);
        await loadRomBytes(bytes, name);
        return;
      }

      throw new Error("Remote file must be .gb/.gbc or .zip");
    };

    loadButton.addEventListener("click", () => {
      if (loadButton.disabled) return;
      loadFileInput.value = "";
      loadUrlInput.value = "";

      renderRecentUrls({
        onPick: (u, opts) => {
          loadUrlInput.value = u;
          loadUrlInput.focus();
          if (opts?.load) loadUrlGo.click();
        }
      });

      loadDlg.showModal?.();
      setTimeout(() => loadUrlInput.focus(), 0);
    });

    recentClear?.addEventListener("click", () => {
      saveRecentUrls([]);
      renderRecentUrls({
        onPick: (u) => {
          loadUrlInput.value = u;
          loadUrlInput.focus();
        }
      });
    });

    // Load local file immediately when selected
    loadFileInput.addEventListener("change", async (e) => {
      const file = e.target.files?.[0];
      if (!file) return;

      try {
        setTempDisabled(true);
        setStatusText("Loading…");
        await handleBlobOrFile(file, file.name);
      } catch (err) {
        console.warn(err);
        setStatusText((err && err.message) ? err.message : "Failed to load file");
        setTempDisabled(false);
      }
    });

    // Load URL when pressing the dialog Load button
    loadUrlGo.addEventListener("click", async () => {
      const url = (loadUrlInput.value || "").trim();
      if (!url) return;

      try {
        setTempDisabled(true);
        await fetchRemote(url);
      } catch (err) {
        console.warn(err);
        setStatusText((err && err.message) ? err.message : "Failed to load URL");
        setTempDisabled(false);
      }
    });

    // Enter key in URL box triggers Load
    loadUrlInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        loadUrlGo.click();
      }
    });

    // Drag/drop: accept .gb/.gbc/.zip
    window.addEventListener("dragover", (e) => e.preventDefault());
    window.addEventListener("drop", async (e) => {
      e.preventDefault();
      if (!e.dataTransfer?.files?.length || loadButton.disabled) return;

      const file = e.dataTransfer.files[0];
      if (!file) return;

      if (!ROM_RE.test(file.name) && !ZIP_RE.test(file.name)) return;

      try {
        setTempDisabled(true);
        await handleBlobOrFile(file, file.name);
      } catch (err) {
        console.warn(err);
        setStatusText((err && err.message) ? err.message : "Failed to load file");
        setTempDisabled(false);
      }
    });

    // auto-load via ?rom=URL
    try {
      const params = new URLSearchParams(window.location.search);
      const auto = params.get("rom");
      if (auto) {
        (async () => {
          try {
            setTempDisabled(true);
            await fetchRemote(auto);
          } catch (err) {
            console.warn(err);
            setStatusText((err && err.message) ? err.message : "Failed to auto-load URL");
            setTempDisabled(false);
          }
        })();
      }
    } catch {
    }
  }
};
