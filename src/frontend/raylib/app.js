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

const KEYMAP_KEY = "gbc_keymap_v1";
const VOLUME_KEY = "gbc_volume_v1";
const SRAM_KEY_PREFIX = "gbc_save_v1:";
const saveRuntimeState = {
  activeRomId: "",
  activeRomName: "",
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

function setActiveRomSaveIdentity(romId, romName) {
  saveRuntimeState.activeRomId = String(romId || "");
  saveRuntimeState.activeRomName = String(romName || "");
}

globalThis.IroGBSaves = {
  setActiveRomSaveIdentity,
  getActiveSramKey() {
    return getActiveSramStorageKey();
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
};

function defaultKeymap() {
  const m = {};
  for (const a of ACTIONS) m[a.id] = a.def;
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

function updateHintFromKeymap(keymap) {
  const hint = document.getElementById("hint");
  if (!hint) return;
  const get = (id) => `<code>${prettyKey(keymap[id])}</code>`;
  hint.innerHTML =
    `Keyboard: ${get(2)} ${get(3)} ${get(1)} ${get(0)} ` +
    `${get(4)}=A ${get(5)}=B ${get(7)}=Start ${get(6)}=Select`;
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

    const settingsButton = document.getElementById("settingsButton");
    const settingsDlg = document.getElementById("settingsDialog");
    const volumeSlider = document.getElementById("volumeSlider");
    const volumeValue = document.getElementById("volumeValue");
    const keybindList = document.getElementById("keybindList");
    const keybindReset = document.getElementById("keybindReset");
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
    let capturing = null;
    const isCapturing = () => capturing != null;

    const kb = bindKeyboard(Module, () => keymap, { isCapturing });
    updateHintFromKeymap(keymap);

    const applyVolume = (v01) => {
      const v = Math.min(1, Math.max(0, Number(v01)));
      if (typeof Module._emscripten_set_master_volume === "function") {
        Module._emscripten_set_master_volume(v);
      }
      saveVolume(v);
      if (volumeSlider) volumeSlider.value = String(Math.round(v * 100));
      if (volumeValue) volumeValue.textContent = `${Math.round(v * 100)}%`;
    };
    applyVolume(loadVolume());

    function renderKeybinds() {
      if (!keybindList) return;
      keybindList.innerHTML = "";
      for (const a of ACTIONS) {
        const row = document.createElement("div");
        row.className = "keybind-item" + (capturing === a.id ? " capturing" : "");

        const act = document.createElement("div");
        act.className = "act";
        act.textContent = a.name;

        const key = document.createElement("div");
        key.className = "key";
        key.textContent = prettyKey(keymap[a.id]);

        const bind = document.createElement("button");
        bind.type = "button";
        bind.className = "btn bind";
        bind.textContent = capturing === a.id ? "Press a key…" : "Rebind";
        bind.addEventListener("click", () => {
          capturing = a.id;
          renderKeybinds();
        });

        row.appendChild(act);
        row.appendChild(key);
        row.appendChild(bind);
        keybindList.appendChild(row);
      }
    }
    renderKeybinds();

    window.addEventListener("keydown", (e) => {
      if (!isCapturing()) return;
      if (!settingsDlg?.open) return;

      if (e.code === "Escape") {
        e.preventDefault();
        capturing = null;
        renderKeybinds();
        return;
      }

      if (!e.code) return;

      e.preventDefault();
      const newCode = e.code;
      const action = capturing;

      const other = ACTIONS.find((x) => x.id !== action && keymap[x.id] === newCode)?.id;
      const prev = keymap[action];
      keymap[action] = newCode;
      if (other != null) keymap[other] = prev;

      saveKeymap(keymap);
      updateHintFromKeymap(keymap);
      capturing = null;
      renderKeybinds();
    }, {passive: false});

    keybindReset?.addEventListener("click", () => {
      keymap = defaultKeymap();
      saveKeymap(keymap);
      updateHintFromKeymap(keymap);
      capturing = null;
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
      settingsDlg?.showModal?.();
    });

    const root = document.documentElement;
    root.classList.toggle("touch-ui", hasTouchUI());

    const setTempDisabled = (disabled) => {
      loadButton.disabled = disabled;
      loadButton.classList.toggle("disabled", disabled);
      loadFileInput.disabled = disabled;
      loadUrlInput.disabled = disabled;
      loadUrlGo.disabled = disabled;
    };

    const setLoadedUI = (fileName) => {
      setTempDisabled(true);
      loadButton.textContent = "Loaded";
      status.textContent = fileName ? fileName : "Running";
    };

    const loadRomBytes = async (bytes, nameForUi) => {
      touch.clearAll();
      status.textContent = "Hashing ROM…";
      const romId = await computeRomContentId(bytes);
      setActiveRomSaveIdentity(romId, nameForUi || "ROM");
      setLoadedUI(nameForUi);
      FS.writeFile("/rom.bin", bytes);

      // Close any UI dialogs BEFORE entering wasm (prevents "unwind" from skipping close)
      closeDialogSafe("romPicker");
      closeDialogSafe("loadDialog");

      // Start on next tick so the close renders first
      setTimeout(() => {
        try {
          Module._emscripten_start();
        } catch (e) {
          // Ignore Emscripten's internal unwind signal
          if (isEmscriptenUnwind(e)) return;
          throw e;
        }
      }, 0);
    };
    const handleBlobOrFile = async (blob, displayName) => {
      const name = displayName || blob?.name || "file";
      const bytes = new Uint8Array(await blob.arrayBuffer());

      if (ZIP_RE.test(name)) {
        status.textContent = "Unzipping…";
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
      status.textContent = "Downloading…";

      // NOTE: Remote hosting must allow CORS for this to work.
      const resp = await fetch(url, {mode: "cors"});
      if (!resp.ok) throw new Error(`Download failed: HTTP ${resp.status}`);

      const ab = await resp.arrayBuffer();
      const bytes = new Uint8Array(ab);

      // Zip detection: by extension OR content-type
      const ct = resp.headers.get("content-type") || "";
      const looksZip = ZIP_RE.test(name) || ct.includes("zip");

      if (looksZip) {
        status.textContent = "Unzipping…";
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
        status.textContent = "Loading…";
        await handleBlobOrFile(file, file.name);
      } catch (err) {
        console.warn(err);
        status.textContent = (err && err.message) ? err.message : "Failed to load file";
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
        status.textContent = (err && err.message) ? err.message : "Failed to load URL";
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
        status.textContent = (err && err.message) ? err.message : "Failed to load file";
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
            status.textContent = (err && err.message) ? err.message : "Failed to auto-load URL";
            setTempDisabled(false);
          }
        })();
      }
    } catch {
    }
  }
};
