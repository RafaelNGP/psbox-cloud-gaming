let isInitializing = true;
const padFunctions = [
  "A (Sul / Cruz)", "B (Leste / Círculo)", "X (Oeste / Quadrado)", "Y (Norte / Triângulo)",
  "LB (L1)", "RB (R1)", "LT (L2)", "RT (R2)", "LS (L3)", "RS (R3)",
  "View (Touchpad / Share)", "Menu (Options)", "D-pad Cima", "D-pad Baixo", "D-pad Esquerda", "D-pad Direita"
];
const padSources = [
  {id: 0, name: "Cruz (South)"}, {id: 1, name: "Círculo (East)"}, {id: 2, name: "Quadrado (West)"}, {id: 3, name: "Triângulo (North)"},
  {id: 4, name: "L1 (LB)"}, {id: 5, name: "R1 (RB)"}, {id: 6, name: "L2 (LT)"}, {id: 7, name: "R2 (RT)"},
  {id: 8, name: "L3 (LS)"}, {id: 9, name: "R3 (RS)"}, {id: 10, name: "Touchpad (View)"}, {id: 11, name: "Options (Menu)"},
  {id: 12, name: "D-pad Cima"}, {id: 13, name: "D-pad Baixo"}, {id: 14, name: "D-pad Esquerda"}, {id: 15, name: "D-pad Direita"}
];

const touchpadTriggers = [
  {key: "swipeUp", label: "Deslizar ↑ (Cima)"},
  {key: "swipeDown", label: "Deslizar ↓ (Baixo)"},
  {key: "swipeLeft", label: "Deslizar ← (Esquerda)"},
  {key: "swipeRight", label: "Deslizar → (Direita)"},
  {key: "clickLeft", label: "Clique Esquerdo"},
  {key: "clickRight", label: "Clique Direito"}
];

const touchpadActions = [
  {id: "none", name: "Desativado"},
  {id: "game_menu", name: "Menu do Jogo"},
  {id: "xbox_button", name: "Botão Xbox (Guia)"},
  {id: "toggle_stats", name: "Alternar Telemetria"},
  {id: "toggle_mic_mute", name: "Silenciar Microfone"},
  {id: "cycle_gyro", name: "Alternar Giroscópio"},
  {id: "cycle_trigger_profile", name: "Alternar Perfil de Gatilho"},
  {id: "view", name: "View"},
  {id: "menu", name: "Menu"},
  {id: "l3", name: "LS (L3)"},
  {id: "r3", name: "RS (R3)"},
  {id: "a", name: "A (Cruz)"},
  {id: "b", name: "B (Círculo)"},
  {id: "x", name: "X (Quadrado)"},
  {id: "y", name: "Y (Triângulo)"},
  {id: "lb", name: "LB (L1)"},
  {id: "rb", name: "RB (R1)"},
  {id: "dpad_up", name: "D-pad Cima"},
  {id: "dpad_down", name: "D-pad Baixo"},
  {id: "dpad_left", name: "D-pad Esquerda"},
  {id: "dpad_right", name: "D-pad Direita"}
];

const defaultTouchpadShortcuts = {
  swipeUp: "xbox_button",
  swipeDown: "game_menu",
  swipeLeft: "game_menu",
  swipeRight: "xbox_button",
  clickLeft: "view",
  clickRight: "view"
};

const keyActionNames = [
  "Analógico Esq. Cima", "Analógico Esq. Baixo", "Analógico Esq. Esquerda", "Analógico Esq. Direita",
  "D-pad Cima", "D-pad Baixo", "D-pad Esquerda", "D-pad Direita",
  "A (Cruz)", "B (Círculo)", "X (Quadrado)", "Y (Triângulo)",
  "LB (L1)", "RB (R1)", "LT (L2)", "RT (R2)", "LS (L3)", "RS (R3)",
  "View (Share / Touch)", "Menu (Options)"
];

const keyList = [
  {id: 0, name: "(Nenhuma)"},
  {id: 87, name: "W"}, {id: 65, name: "A"}, {id: 83, name: "S"}, {id: 68, name: "D"},
  {id: 81, name: "Q"}, {id: 69, name: "E"}, {id: 82, name: "R"}, {id: 70, name: "F"},
  {id: 67, name: "C"}, {id: 86, name: "V"}, {id: 90, name: "Z"}, {id: 88, name: "X"},
  {id: 66, name: "B"}, {id: 71, name: "G"}, {id: 84, name: "T"}, {id: 89, name: "Y"},
  {id: 72, name: "H"}, {id: 74, name: "J"}, {id: 75, name: "K"}, {id: 76, name: "L"},
  {id: 77, name: "M"}, {id: 78, name: "N"}, {id: 79, name: "O"}, {id: 80, name: "P"},
  {id: 85, name: "U"}, {id: 73, name: "I"},
  {id: 48, name: "0"}, {id: 49, name: "1"}, {id: 50, name: "2"}, {id: 51, name: "3"},
  {id: 52, name: "4"}, {id: 53, name: "5"}, {id: 54, name: "6"}, {id: 55, name: "7"},
  {id: 56, name: "8"}, {id: 57, name: "9"},
  {id: 32, name: "Espaço (Space)"},
  {id: 16, name: "Shift"}, {id: 17, name: "Ctrl"}, {id: 18, name: "Alt"},
  {id: 9, name: "Tab"}, {id: 27, name: "Esc"}, {id: 13, name: "Enter"}, {id: 8, name: "Backspace"},
  {id: 38, name: "Seta Cima"}, {id: 40, name: "Seta Baixo"},
  {id: 37, name: "Seta Esquerda"}, {id: 39, name: "Seta Direita"},
  {id: 241, name: "Mouse L (Esq)"}, {id: 242, name: "Mouse R (Dir)"},
  {id: 243, name: "Mouse M (Meio)"}, {id: 244, name: "Mouse 4"}, {id: 245, name: "Mouse 5"}
];

const defaultKeyMap = [
  87, 0,  83, 0,  65, 0,  68, 0,
  38, 0,  40, 0,  37, 0,  39, 0,
  32, 0,  67, 0,  82, 0,  69, 0,
  81, 0,  70, 0, 242, 0, 241, 0,
  16, 0,  86, 243, 9, 0,  27, 0
];

let currentSettings = {
  padMap: [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],
  keyMap: [...defaultKeyMap],
  touchpadShortcuts: {...defaultTouchpadShortcuts}
};
let lastSettingsVersion = 0;

let currentSearchTab = 'gamepass';
let currentFilters = {
  plan: 0,
  consoleGP: 0,
  consoleLib: 0,
  modeGP: 0,
  modeLib: 0,
  langGP: 0,
  langLib: 0,
  mouseGP: false,
  mouseLib: false,
  wishlist: false,
  free: false,
  cheapest: false
};

function switchTab(name) {
  document.querySelectorAll('.tab-pane').forEach(el => {
    el.style.display = 'none';
    el.classList.remove('active');
  });
  document.querySelectorAll('.nav-tab').forEach(el => el.classList.remove('active'));
  const pane = document.getElementById('tab-' + name);
  if (pane) {
    pane.style.display = 'block';
    pane.classList.add('active');
  }
  const tabBtn = document.getElementById('navTab-' + name);
  if (tabBtn) tabBtn.classList.add('active');
  if (name === 'search') pollStreamingState();
  if (name === 'gamepad') {
    sendPadState(true);
    updateGamepadScale();
    setTimeout(updateGamepadScale, 10);
  }
  if (name === 'stream') {
    loadStreamMenuState();
    pollStats();
  }
}

function switchRemapSub(mode) {
  document.getElementById('subRemapPad').style.display = mode === 'pad' ? 'block' : 'none';
  document.getElementById('subRemapKey').style.display = mode === 'key' ? 'block' : 'none';
  const subTouch = document.getElementById('subRemapTouch');
  if (subTouch) subTouch.style.display = mode === 'touch' ? 'block' : 'none';
  document.getElementById('btnSubPad').classList.toggle('active', mode === 'pad');
  document.getElementById('btnSubKey').classList.toggle('active', mode === 'key');
  const btnTouch = document.getElementById('btnSubTouch');
  if (btnTouch) btnTouch.classList.toggle('active', mode === 'touch');
  document.getElementById('subRemapGyro').style.display = mode === 'gyro' ? 'block' : 'none';
  document.getElementById('btnSubGyro').classList.toggle('active', mode === 'gyro');
}

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2500);
}

const searchInput = document.getElementById('searchInput');
let searchDebounce = null;

function applySearchTabUI(tab) {
  currentSearchTab = tab;
  document.getElementById('tabBtnGamePass').classList.toggle('active', tab === 'gamepass');
  document.getElementById('tabBtnLibrary').classList.toggle('active', tab === 'library');
  document.getElementById('filtersGamePass').style.display = tab === 'gamepass' ? 'flex' : 'none';
  document.getElementById('filtersLibrary').style.display = tab === 'library' ? 'flex' : 'none';
}

function setSearchTab(tab) {
  applySearchTabUI(tab);
  sendSearch(true);
}

function toggleChip(id) {
  const el = document.getElementById(id);
  el.classList.toggle('active');
  const active = el.classList.contains('active');
  if (id === 'chipWishlist') currentFilters.wishlist = active;
  if (id === 'chipFree') currentFilters.free = active;
  if (id === 'chipCheapest') currentFilters.cheapest = active;
  if (id === 'chipMouseGP') currentFilters.mouseGP = active;
  if (id === 'chipMouseLib') currentFilters.mouseLib = active;
  sendSearch(true);
}

function onFilterChange() {
  currentFilters.plan = parseInt(document.getElementById('filterPlan').value, 10);
  currentFilters.consoleGP = parseInt(document.getElementById('filterConsoleGP').value, 10);
  currentFilters.consoleLib = parseInt(document.getElementById('filterConsoleLib').value, 10);
  currentFilters.modeGP = parseInt(document.getElementById('filterModeGP').value, 10);
  currentFilters.modeLib = parseInt(document.getElementById('filterModeLib').value, 10);
  currentFilters.langGP = parseInt(document.getElementById('filterLangGP').value, 10);
  currentFilters.langLib = parseInt(document.getElementById('filterLangLib').value, 10);

  document.getElementById('chipSelectPlan').classList.toggle('active', currentFilters.plan > 0);
  document.getElementById('chipSelectConsoleGP').classList.toggle('active', currentFilters.consoleGP > 0);
  document.getElementById('chipSelectConsoleLib').classList.toggle('active', currentFilters.consoleLib > 0);
  document.getElementById('chipSelectModeGP').classList.toggle('active', currentFilters.modeGP > 0);
  document.getElementById('chipSelectModeLib').classList.toggle('active', currentFilters.modeLib > 0);
  document.getElementById('chipSelectLangGP').classList.toggle('active', currentFilters.langGP > 0);
  document.getElementById('chipSelectLangLib').classList.toggle('active', currentFilters.langLib > 0);
  sendSearch(true);
}

function updateSearchStatusText() {
  const q = searchInput.value;
  const isLib = currentSearchTab === 'library';
  let status = isLib ? 'Meus jogos' : 'Game Pass';
  if (q) status += ' • "' + q + '"';
  const filterNames = [];
  if (isLib) {
    if (currentFilters.wishlist) filterNames.push('Lista de desejos');
    if (currentFilters.free) filterNames.push('Grátis');
    if (currentFilters.cheapest) filterNames.push('Menor preço');
    if (currentFilters.mouseLib) filterNames.push('Teclado e mouse');
    if (currentFilters.consoleLib > 0) {
      const cNames = ['Todos', 'Series X|S', 'Xbox One', 'Xbox 360'];
      filterNames.push(cNames[currentFilters.consoleLib]);
    }
    if (currentFilters.modeLib > 0) {
      const mNames = ['Todos', 'Um jogador', 'Multijogador online', 'Cooperativo online', 'Multijogador local'];
      filterNames.push(mNames[currentFilters.modeLib]);
    }
    if (currentFilters.langLib > 0) {
      const lNames = ['Qualquer', 'Legendas', 'Dublado'];
      filterNames.push(lNames[currentFilters.langLib]);
    }
  } else {
    if (currentFilters.plan > 0) {
      const pNames = ['Meu plano', 'Ultimate', 'Premium', 'Essential', 'Todos os planos'];
      filterNames.push(pNames[currentFilters.plan]);
    }
    if (currentFilters.mouseGP) filterNames.push('Teclado e mouse');
    if (currentFilters.consoleGP > 0) {
      const cNames = ['Todos', 'Series X|S', 'Xbox One', 'Xbox 360'];
      filterNames.push(cNames[currentFilters.consoleGP]);
    }
    if (currentFilters.modeGP > 0) {
      const mNames = ['Todos', 'Um jogador', 'Multijogador online', 'Cooperativo online', 'Multijogador local'];
      filterNames.push(mNames[currentFilters.modeGP]);
    }
    if (currentFilters.langGP > 0) {
      const lNames = ['Qualquer', 'Legendas', 'Dublado'];
      filterNames.push(lNames[currentFilters.langGP]);
    }
  }
  if (filterNames.length > 0) {
    status += ' [' + filterNames.join(', ') + ']';
  }
  document.getElementById('searchStatus').textContent = status;
}

function sendSearch(immediate = false) {
  clearTimeout(searchDebounce);
  const doFetch = () => {
    const q = searchInput.value;
    const isLib = currentSearchTab === 'library';
    const activeFilters = {
      plan: isLib ? 0 : currentFilters.plan,
      console: isLib ? currentFilters.consoleLib : currentFilters.consoleGP,
      mode: isLib ? currentFilters.modeLib : currentFilters.modeGP,
      language: isLib ? currentFilters.langLib : currentFilters.langGP,
      mouseKeyboard: isLib ? currentFilters.mouseLib : currentFilters.mouseGP,
      wishlist: isLib ? currentFilters.wishlist : false,
      free: isLib ? currentFilters.free : false,
      cheapest: isLib ? currentFilters.cheapest : false
    };

    updateSearchStatusText();

    fetch('/api/search', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({
        query: q,
        tab: currentSearchTab,
        filters: activeFilters
      })
    }).catch(e => console.error(e));
  };

  if (immediate) {
    doFetch();
  } else {
    searchDebounce = setTimeout(doFetch, 120);
  }
}

searchInput.addEventListener('input', () => {
  sendSearch(false);
});

function clearSearch() {
  searchInput.value = '';
  sendSearch(true);
}

function renderTouchpadShortcutsGrid(containerId, idPrefix, tsObj, onChangeFn) {
  const grid = document.getElementById(containerId);
  if (!grid) return;
  grid.innerHTML = '';
  const ts = Object.assign({}, defaultTouchpadShortcuts, tsObj || {});
  touchpadTriggers.forEach(trig => {
    const item = document.createElement('div');
    item.className = 'map-item';
    const label = document.createElement('label');
    label.textContent = trig.label;
    const select = document.createElement('select');
    select.id = idPrefix + '_' + trig.key;
    touchpadActions.forEach((act, idx) => {
      const opt = document.createElement('option');
      opt.value = act.id;
      opt.textContent = act.name;
      if (ts[trig.key] === act.id || ts[trig.key] === idx) opt.selected = true;
      select.appendChild(opt);
    });
    select.addEventListener('change', () => {
      currentSettings.touchpadShortcuts = currentSettings.touchpadShortcuts || {...defaultTouchpadShortcuts};
      currentSettings.touchpadShortcuts[trig.key] = select.value;
      syncTouchpadSelects(trig.key, select.value);
      if (onChangeFn) onChangeFn(trig.key, select.value);
    });
    item.appendChild(label);
    item.appendChild(select);
    grid.appendChild(item);
  });
}

function syncTouchpadSelects(trigKey, val) {
  ['tp_remap_'].forEach(prefix => {
    const el = document.getElementById(prefix + trigKey);
    if (el && el.value !== String(val)) el.value = val;
  });
}

function readTouchpadShortcutsFromPrefix(prefix) {
  const out = {...defaultTouchpadShortcuts};
  touchpadTriggers.forEach(trig => {
    const el = document.getElementById(prefix + '_' + trig.key);
    if (el) out[trig.key] = el.value;
  });
  return out;
}

function renderAllTouchpadGrids(tsObj) {
  const ts = Object.assign({}, defaultTouchpadShortcuts, tsObj || currentSettings.touchpadShortcuts || {});
  currentSettings.touchpadShortcuts = ts;
  renderTouchpadShortcutsGrid('remapTouchpadShortcutsGrid', 'tp_remap', ts, null);
}

function resetTouchpadShortcuts() {
  currentSettings.touchpadShortcuts = {...defaultTouchpadShortcuts};
  renderAllTouchpadGrids(currentSettings.touchpadShortcuts);
  saveSettings();
}

function renderPadMap() {
  const grid = document.getElementById('padGrid');
  grid.innerHTML = '';
  const map = Array.isArray(currentSettings.padMap) ? currentSettings.padMap
            : (currentSettings.padMap && Array.isArray(currentSettings.padMap.map)) ? currentSettings.padMap.map
            : [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15];
  padFunctions.forEach((fnName, idx) => {
    const item = document.createElement('div');
    item.className = 'map-item';
    const label = document.createElement('label');
    label.textContent = fnName;
    const select = document.createElement('select');
    select.id = 'pad_fn_' + idx;
    padSources.forEach(s => {
      const opt = document.createElement('option');
      opt.value = s.id;
      opt.textContent = s.name;
      if (map[idx] === s.id) opt.selected = true;
      select.appendChild(opt);
    });
    item.appendChild(label);
    item.appendChild(select);
    grid.appendChild(item);
  });
}

function renderKeyMap() {
  const grid = document.getElementById('keyGrid');
  grid.innerHTML = '';
  const kmap = (currentSettings.keyMap && currentSettings.keyMap.length === 40) ? currentSettings.keyMap : defaultKeyMap;
  keyActionNames.forEach((actName, idx) => {
    const item = document.createElement('div');
    item.className = 'map-item';
    const label = document.createElement('label');
    label.textContent = actName;
    const selectsWrap = document.createElement('div');
    selectsWrap.className = 'selects-wrap';
    for (let slot = 0; slot < 2; slot++) {
      const select = document.createElement('select');
      select.id = 'key_act_' + idx + '_' + slot;
      const code = kmap[idx * 2 + slot];
      let hasMatch = false;
      keyList.forEach(k => {
        const opt = document.createElement('option');
        opt.value = k.id;
        opt.textContent = k.name;
        if (code === k.id) { opt.selected = true; hasMatch = true; }
        select.appendChild(opt);
      });
      if (!hasMatch && code > 0) {
        const opt = document.createElement('option');
        opt.value = code;
        opt.textContent = "Key #" + code;
        opt.selected = true;
        select.appendChild(opt);
      }
      selectsWrap.appendChild(select);
    }
    item.appendChild(label);
    item.appendChild(selectsWrap);
    grid.appendChild(item);
  });
}

function resetPadMap() {
  currentSettings.padMap = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15];
  renderPadMap();
  saveSettings();
}

function resetKeyMap() {
  currentSettings.keyMap = [...defaultKeyMap];
  renderKeyMap();
  saveSettings();
}

function saveSettings() {
  if (isInitializing) return;
  const padMap = [];
  for (let i = 0; i < 16; i++) {
    const sel = document.getElementById('pad_fn_' + i);
    padMap.push(sel ? parseInt(sel.value, 10) : i);
  }
  const keyMap = [];
  for (let i = 0; i < 20; i++) {
    const s0 = document.getElementById('key_act_' + i + '_0');
    const s1 = document.getElementById('key_act_' + i + '_1');
    keyMap.push(s0 ? parseInt(s0.value, 10) : 0);
    keyMap.push(s1 ? parseInt(s1.value, 10) : 0);
  }
  const touchpadShortcuts = readTouchpadShortcutsFromPrefix('tp_remap');
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ padMap: padMap, keyMap: keyMap, touchpadShortcuts: touchpadShortcuts })
  }).then(r => r.json()).then(res => {
    if (res.ok) {
      showToast('Configurações salvas no console!');
    }
  }).catch(e => showToast('Erro ao salvar configurações'));
}

function onBitrateInput(val) {
  const v = parseInt(val, 10);
  const el = document.getElementById('valBitrate');
  if (el) {
    if (v === 0) el.textContent = 'Auto';
    else if (v === 55 || v === -1 || v >= 100) el.textContent = 'Sem Limite';
    else if (v > 15) el.textContent = v + ' Mbps*';
    else el.textContent = v + ' Mbps';
  }
}

function onLightBarModeChange() {
  const sel = document.getElementById('selectLightBarMode');
  const colorInput = document.getElementById('inputLightBarColor');
  if (sel && colorInput) {
    colorInput.style.display = (sel.value === '1') ? 'inline-block' : 'none';
  }
}

function markSettingsChanged() {
  if (isInitializing) return;
}

function postSettings(payload) {
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload)
  }).then(r => r.json()).then(res => {
    showToast(res.ok ? 'Configurações salvas no console!' : 'Erro ao salvar configurações');
  }).catch(() => showToast('Erro ao salvar configurações'));
}

function saveGeneralSettings() {
  if (isInitializing) return;
  const rawBitrate = parseInt(document.getElementById('inputBitrate').value, 10);
  const fpsEl = document.getElementById('selectFpsLimit');
  postSettings({
    videoDecoder: parseInt(document.getElementById('selectVideoDecoder').value, 10),
    resolution: document.getElementById('selectResolution').value,
    upscaler: parseInt(document.getElementById('selectUpscaler').value, 10),
    sharpness: parseInt(document.getElementById('inputCas').value, 10),
    deband: parseInt(document.getElementById('selectDeband').value, 10),
    highRefresh: document.getElementById('check120Hz').checked,
    fpsLimit: fpsEl ? parseInt(fpsEl.value, 10) : 0,
    maxBitrateMbps: (rawBitrate === 55) ? -1 : rawBitrate,
    queueReadyAlert: document.getElementById('checkQueueAlert').checked,
    voiceRouting: parseInt(document.getElementById('selectVoiceRouting').value, 10),
    uiScale: parseInt(document.getElementById('selectUiScale').value, 10),
    storeCountry: document.getElementById('selectStoreCountry').value
  });
}

// Mapeamento > DualSense: the controller's own settings.
function saveControllerSettings() {
  if (isInitializing) return;
  const rowUsb = document.getElementById('rowUsbControllers');
  const payload = {
    deadzoneLeft: parseInt(document.getElementById('inputDeadzoneL').value, 10),
    deadzoneRight: parseInt(document.getElementById('inputDeadzoneR').value, 10),
    triggerStrength: parseInt(document.getElementById('inputTriggerStrength').value, 10),
    triggerProfile: parseInt(document.getElementById('selectSmartTriggers').value, 10),
    lightBarMode: parseInt(document.getElementById('selectLightBarMode').value, 10),
    lightBarColour: document.getElementById('inputLightBarColor').value,
    padSpeakerVolume: parseInt(document.getElementById('inputPadSpeaker').value, 10)
  };
  if (rowUsb && rowUsb.style.display !== 'none') {
    payload.usbControllers = document.getElementById('checkUsbControllers').checked;
  }
  const rowStrum = document.getElementById('rowGuitarStrum');
  if (rowStrum && rowStrum.style.display !== 'none') {
    payload.guitarStrum = document.getElementById('checkGuitarStrum').checked;
  }
  postSettings(payload);
}

function updateCasLabel(val) {
  const labels = ['Desativada', 'Baixa', 'Média', 'Alta'];
  const el = document.getElementById('valCas');
  if (el) el.textContent = labels[val] !== undefined ? labels[val] : 'Média';
}

function sendStreamAction(action) {
  fetch('/api/stream', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ action: action })
  }).then(r => r.json()).then(res => {
    if (action === 'xbox_guide') showToast('Guia Xbox acionado');
    if (action === 'end_session') showToast('Sessão encerrada');
  }).catch(() => {});
}

// The game's stick response gyro aiming works through (Mapeamento > Giroscópio).
function applyGyroResponse(s) {
  if (s.gyroCurveStart !== undefined) {
    const el = document.getElementById('inputGyroCurveStart');
    if (el) { el.value = s.gyroCurveStart; document.getElementById('valGyroCurveStart').textContent = s.gyroCurveStart + '%'; }
  }
  if (s.gyroCurveExp !== undefined) {
    const el = document.getElementById('inputGyroCurveExp');
    if (el) { el.value = s.gyroCurveExp; document.getElementById('valGyroCurveExp').textContent = (s.gyroCurveExp / 10).toFixed(1); }
  }
  if (s.gyroInvertX !== undefined) {
    const el = document.getElementById('checkGyroInvertX');
    if (el) el.checked = !!s.gyroInvertX;
  }
  if (s.gyroInvertY !== undefined) {
    const el = document.getElementById('checkGyroInvertY');
    if (el) el.checked = !!s.gyroInvertY;
  }
}

function saveGyroSettings() {
  if (isInitializing) return;
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      gyroCurveStart: parseInt(document.getElementById('inputGyroCurveStart').value, 10),
      gyroCurveExp: parseInt(document.getElementById('inputGyroCurveExp').value, 10),
      gyroInvertX: document.getElementById('checkGyroInvertX').checked,
      gyroInvertY: document.getElementById('checkGyroInvertY').checked
    })
  }).then(r => r.json()).then(res => {
    if (res.ok) showToast('Configurações salvas no console!');
  }).catch(e => showToast('Erro ao salvar configurações'));
}

function resetGyroResponse() {
  applyGyroResponse({gyroCurveStart: 12, gyroCurveExp: 10, gyroInvertX: false, gyroInvertY: false});
  saveGyroSettings();
}

function loadStreamMenuState() {
  fetch('/api/stream').then(r => r.json()).then(s => {
    if (s.touchpadShortcuts) {
      renderAllTouchpadGrids(s.touchpadShortcuts);
    }
  }).catch(() => {});
}

function renderTelemetry(data) {
  if (!data) return;
  const num = v => (typeof v === 'number' && Number.isFinite(v)) ? v : null;
  const put = (id, text, level) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.classList.toggle('warn', level === 1);
    el.classList.toggle('bad', level === 2);
  };
  const lvl = (v, warn, bad) => v === null ? 0 : (v >= bad ? 2 : (v >= warn ? 1 : 0));
  const pair = (a, b, digits) => (a === null ? '--' : a.toFixed(digits)) + ' / ' + (b === null ? '--' : b.toFixed(digits));
  const fps = num(data.fps), rtt = num(data.rttMs), best = num(data.bestRttMs), mbps = num(data.mbps), loss = num(data.lossPct), qp = num(data.qp);
  const live = fps !== null && fps > 0;
  const state = document.getElementById('telState');
  if (state) {
    state.textContent = !live ? 'Sem transmissão.' : (data.degraded ? 'Conexão degradada.' : 'Conexão estável.');
    state.classList.toggle('ok', live && !data.degraded);
    state.classList.toggle('bad', live && !!data.degraded);
  }
  put('telRes', (num(data.width) && num(data.height)) ? data.width + 'x' + data.height : '--');
  put('telFps', fps === null ? '--' : String(Math.round(fps)));
  put('telMbps', (mbps === null ? '--' : mbps.toFixed(1)) + ' Mbps');
  put('telQp', (qp === null || qp < 0) ? '--' : String(Math.round(qp)), lvl(qp, 32, 38));
  const hw = data.isHwDecode !== undefined ? (data.isHwDecode ? ' (HW)' : ' (SW)') : '';
  put('telDec', pair(num(data.decodeMs), num(data.decodePeakMs), 1) + ' ms' + hw, lvl(num(data.decodePeakMs), 12, 16));
  put('telDraw', pair(num(data.drawMs), num(data.drawPeakMs), 1) + ' ms', lvl(num(data.drawPeakMs), 14, 17));
  put('telOnScreen', pair(num(data.onScreenMs), num(data.onScreenPeakMs), 1) + ' ms');
  put('telGlass', pair(num(data.glassMs), num(data.glassPeakMs), 1) + ' ms');
  const skipNow = num(data.skippedNow);
  put('telSkipped', (skipNow === null ? '--' : skipNow) + ' / ' + (num(data.skippedTotal) === null ? '--' : data.skippedTotal), lvl(skipNow, 1, 5));
  put('telRegion', data.region ? String(data.region) : '--');
  put('telRtt', ((rtt === null || rtt < 0) ? '--' : Math.round(rtt)) + ' / ' + ((best === null || best < 0) ? '--' : Math.round(best)) + ' ms', lvl(rtt, 60, 100));
  put('telLoss', (loss === null ? '--' : loss.toFixed(1)) + '%', lvl(loss, 0.5, 2));
  const gap = num(data.gapMs), burst = num(data.burst);
  put('telGap', (gap === null ? '--' : gap) + ' ms', lvl(gap, 60, 100));
  put('telBurst', burst === null ? '--' : String(burst), lvl(burst, 3, 6));
  put('telFrameLate', (num(data.frameLateMs) === null ? '--' : data.frameLateMs) + ' ms', lvl(num(data.frameLateMs), 50, 100));
  put('telLost', (num(data.rtpLost) === null ? '--' : data.rtpLost) + ' / ' + (num(data.rtpRecovered) === null ? '--' : data.rtpRecovered));
  put('telNacks', num(data.rtpNacks) === null ? '--' : String(data.rtpNacks));
  put('telNetDropped', num(data.rtpDroppedFrames) === null ? '--' : String(data.rtpDroppedFrames), lvl(num(data.rtpDroppedFrames), 1, 10));
  put('telKf', num(data.keyframeRequests) === null ? '--' : String(data.keyframeRequests));
  put('telRemb', (num(data.rembKbps) === null ? '--' : (data.rembKbps / 1000).toFixed(1)) + ' Mbps');
  put('telReconnects', num(data.reconnects) === null ? '--' : String(data.reconnects), lvl(num(data.reconnects), 1, 3));
  const secs = num(data.sessionSeconds);
  put('telTime', secs === null ? '--' : Math.floor(secs / 60) + ' min ' + (secs % 60) + ' s');
  put('telBad', num(data.badSeconds) === null ? '--' : String(data.badSeconds), lvl(num(data.badSeconds), 1, 30));
  put('telLate', (num(data.lateFrames) === null ? '--' : data.lateFrames) + ' / ' + (num(data.decodeFailures) === null ? '--' : data.decodeFailures));
}

let ws = null;
function initWs() {
  try {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(proto + '//' + location.host + '/ws');
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'telemetry' && msg.stats) {
          renderTelemetry(msg.stats);
        } else if (msg.fps !== undefined) {
          renderTelemetry(msg);
        }
      } catch (_) {}
    };
    ws.onclose = () => {
      ws = null;
      setTimeout(initWs, 2000);
    };
    ws.onerror = () => {
      try { ws.close(); } catch(_) {}
    };
  } catch (_) {
    ws = null;
  }
}
initWs();

function pollStats() {
  if (ws && ws.readyState === WebSocket.OPEN) return;
  const streamTab = document.getElementById('tab-stream');
  if (!streamTab || streamTab.style.display === 'none') return;
  fetch('/api/stats').then(r => r.json()).then(renderTelemetry).catch(() => {});
}
setInterval(pollStats, 1000);

function loadLogPreview() {
  const p = document.getElementById('logPreview');
  p.textContent = 'Carregando log...';
  fetch('/api/log').then(r => r.text()).then(text => {
    const lines = text.split('\n');
    p.textContent = lines.slice(-50).join('\n') || 'Log vazio.';
  }).catch(e => {
    p.textContent = 'Erro ao carregar log: ' + e;
  });
}

function getReportParams() {
  const title = (document.getElementById('reportTitle')?.value || '').trim();
  const what = (document.getElementById('reportWhat')?.value || '').trim();
  return { title, what };
}

function openGitHubReport() {
  const { title, what } = getReportParams();
  const btn = document.getElementById('btnOpenReport');
  btn.disabled = true;
  btn.textContent = 'Gerando link...';
  fetch('/api/report-issue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title, what })
  }).then(r => r.json()).then(data => {
    btn.disabled = false;
    btn.textContent = '🚀 Abrir Relatório no GitHub';
    if (data && data.url) {
      window.open(data.url, '_blank');
      showToast('Formulário do GitHub aberto em nova aba!');
    } else {
      showToast('Erro ao obter link do relatório.');
    }
  }).catch(e => {
    btn.disabled = false;
    btn.textContent = '🚀 Abrir Relatório no GitHub';
    showToast('Falha na requisição: ' + e);
  });
}

function fallbackCopyText(text) {
  return new Promise((resolve, reject) => {
    try {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.top = '0';
      ta.style.left = '0';
      ta.style.width = '2em';
      ta.style.height = '2em';
      ta.style.padding = '0';
      ta.style.border = 'none';
      ta.style.outline = 'none';
      ta.style.boxShadow = 'none';
      ta.style.background = 'transparent';
      ta.style.opacity = '0.01';
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      if (typeof ta.setSelectionRange === 'function') {
        ta.setSelectionRange(0, text.length);
      }
      let ok = false;
      try {
        ok = document.execCommand('copy');
      } catch (_) {
        ok = false;
      }
      document.body.removeChild(ta);
      if (ok) resolve(); else reject(new Error('execCommand copy failed'));
    } catch (e) { reject(e); }
  });
}

function copyTextToClipboard(text) {
  if (navigator.clipboard && window.isSecureContext && navigator.clipboard.writeText) {
    return navigator.clipboard.writeText(text).catch(() => fallbackCopyText(text));
  }
  return fallbackCopyText(text);
}

function copySanitizedReport() {
  const { title, what } = getReportParams();
  const btn = document.getElementById('btnCopyReport');
  btn.disabled = true;
  fetch('/api/report-issue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title, what })
  }).then(r => r.json()).then(data => {
    btn.disabled = false;
    if (data && data.excerpt) {
      const markdown = `### ${data.title || 'Relatório de Erro'}\n\n**Plataforma:** ${data.platform || 'PlayStation 5'}\n**Versão:** v${data.release || '{{XC_APP_VERSION}}'}\n**Firmware:** ${data.firmware || 'N/A'}\n\n**O que aconteceu:**\n${what || 'Relatório enviado via WebUI'}\n\n\`\`\`\n${data.excerpt}\n\`\`\``;
      copyTextToClipboard(markdown).then(() => {
        showToast('Relatório copiado para a área de transferência!');
      }).catch(() => {
        const p = document.getElementById('logPreview');
        if (p) {
          p.textContent = markdown;
          p.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }
        try {
          window.prompt('Copie o relatório abaixo (Ctrl+C / Toque longo):', markdown);
          showToast('Relatório preparado!');
        } catch (_) {
          showToast('Relatório preparado no campo abaixo para cópia!');
        }
      });
    } else {
      showToast('Erro ao gerar relatório.');
    }
  }).catch(e => {
    btn.disabled = false;
    showToast('Falha ao copiar: ' + e);
  });
}

function previewReport() {
  const { title, what } = getReportParams();
  const p = document.getElementById('logPreview');
  p.textContent = 'Carregando resumo do relatório sanitizado...';
  fetch('/api/report-issue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title, what })
  }).then(r => r.json()).then(data => {
    if (data && data.excerpt) {
      p.textContent = `=== RELATÓRIO PREPARADO ===\nTítulo: ${data.title}\nPlataforma: ${data.platform}\nVersão: v${data.release}\nFirmware: ${data.firmware}\n\n${data.excerpt}`;
    } else {
      p.textContent = 'Nenhum dado retornado.';
    }
  }).catch(e => {
    p.textContent = 'Erro ao carregar pré-visualização: ' + e;
  });
}

function applyPlatformAdaptation(s) {
  const sub = document.getElementById('brandSubtitle');
  if (sub && s.platform) {
    sub.textContent = 'Smartglass WebUI \u00B7 ' + s.platform;
  }
  const rowUsb = document.getElementById('rowUsbControllers');
  if (rowUsb && s.isPS4 !== undefined) {
    rowUsb.style.display = s.isPS4 ? 'flex' : 'none';
  }
  const padName = (s.isPS4 && !s.isPS5) ? 'DualShock 4' : 'DualSense';
  const lblLightBar = document.getElementById('lblLightBar');
  if (lblLightBar) lblLightBar.textContent = 'Barra de Luz ' + padName;
  const optVoiceAuto = document.getElementById('optVoiceAuto');
  if (optVoiceAuto) optVoiceAuto.textContent = 'Auto (' + padName + ')';
  const lblPadSpeaker = document.getElementById('lblPadSpeaker');
  if (lblPadSpeaker) lblPadSpeaker.textContent = 'Alto-falante ' + padName;
  const btnSubPad = document.getElementById('btnSubPad');
  if (btnSubPad) btnSubPad.textContent = '\uD83C\uDFAE ' + padName;
  const h2RemapPad = document.getElementById('h2RemapPad');
  if (h2RemapPad) h2RemapPad.textContent = '\uD83C\uDFAE Remapeamento de Botões (' + padName + ')';
  const h2RemapTouch = document.getElementById('h2RemapTouch');
  if (h2RemapTouch) h2RemapTouch.textContent = '\uD83D\uDC46 Atalhos do Touchpad (' + padName + ')';
}

function applyLoadedSettings(s) {
  currentSettings = Object.assign(currentSettings, s);
  applyPlatformAdaptation(s);
  if (s.settingsVersion !== undefined) lastSettingsVersion = s.settingsVersion;
  if (s.query !== undefined && document.activeElement !== searchInput) searchInput.value = s.query;
  if (s.tab) {
    currentSearchTab = s.tab;
  }
  if (s.filters) {
    if (s.filters.wishlist !== undefined) {
      currentFilters.wishlist = !!s.filters.wishlist;
      const el = document.getElementById('chipWishlist');
      if (el) el.classList.toggle('active', currentFilters.wishlist);
    }
    if (s.filters.free !== undefined) {
      currentFilters.free = !!s.filters.free;
      const el = document.getElementById('chipFree');
      if (el) el.classList.toggle('active', currentFilters.free);
    }
    if (s.filters.cheapest !== undefined) {
      currentFilters.cheapest = !!s.filters.cheapest;
      const el = document.getElementById('chipCheapest');
      if (el) el.classList.toggle('active', currentFilters.cheapest);
    }
    if (s.filters.mouseKeyboard !== undefined) {
      currentFilters.mouseGP = !!s.filters.mouseKeyboard;
      currentFilters.mouseLib = !!s.filters.mouseKeyboard;
      const elGP = document.getElementById('chipMouseGP');
      if (elGP) elGP.classList.toggle('active', currentFilters.mouseGP);
      const elLib = document.getElementById('chipMouseLib');
      if (elLib) elLib.classList.toggle('active', currentFilters.mouseLib);
    }
    if (s.filters.plan !== undefined) {
      currentFilters.plan = s.filters.plan;
      const sel = document.getElementById('filterPlan');
      if (sel) sel.value = s.filters.plan;
      const wrap = document.getElementById('chipSelectPlan');
      if (wrap) wrap.classList.toggle('active', s.filters.plan > 0);
    }
    if (s.filters.console !== undefined) {
      currentFilters.consoleGP = s.filters.console;
      currentFilters.consoleLib = s.filters.console;
      const selGP = document.getElementById('filterConsoleGP');
      if (selGP) selGP.value = s.filters.console;
      const selLib = document.getElementById('filterConsoleLib');
      if (selLib) selLib.value = s.filters.console;
      const wrapGP = document.getElementById('chipSelectConsoleGP');
      if (wrapGP) wrapGP.classList.toggle('active', s.filters.console > 0);
      const wrapLib = document.getElementById('chipSelectConsoleLib');
      if (wrapLib) wrapLib.classList.toggle('active', s.filters.console > 0);
    }
    if (s.filters.mode !== undefined) {
      currentFilters.modeGP = s.filters.mode;
      currentFilters.modeLib = s.filters.mode;
      const selGP = document.getElementById('filterModeGP');
      if (selGP) selGP.value = s.filters.mode;
      const selLib = document.getElementById('filterModeLib');
      if (selLib) selLib.value = s.filters.mode;
      const wrapGP = document.getElementById('chipSelectModeGP');
      if (wrapGP) wrapGP.classList.toggle('active', s.filters.mode > 0);
      const wrapLib = document.getElementById('chipSelectModeLib');
      if (wrapLib) wrapLib.classList.toggle('active', s.filters.mode > 0);
    }
    if (s.filters.language !== undefined) {
      currentFilters.langGP = s.filters.language;
      currentFilters.langLib = s.filters.language;
      const selGP = document.getElementById('filterLangGP');
      if (selGP) selGP.value = s.filters.language;
      const selLib = document.getElementById('filterLangLib');
      if (selLib) selLib.value = s.filters.language;
      const wrapGP = document.getElementById('chipSelectLangGP');
      if (wrapGP) wrapGP.classList.toggle('active', s.filters.language > 0);
      const wrapLib = document.getElementById('chipSelectLangLib');
      if (wrapLib) wrapLib.classList.toggle('active', s.filters.language > 0);
    }
  }
  if (s.maxBitrateMbps !== undefined) {
    const el = document.getElementById('inputBitrate');
    if (el) {
      const sliderVal = (s.maxBitrateMbps === -1 || s.maxBitrateMbps >= 100) ? 55 : s.maxBitrateMbps;
      el.value = sliderVal;
      onBitrateInput(sliderVal);
    }
  }
  if (s.fpsLimit !== undefined) {
    const el = document.getElementById('selectFpsLimit');
    if (el) el.value = s.fpsLimit;
  }
  if (s.highRefresh !== undefined) {
    const el = document.getElementById('check120Hz');
    if (el) el.checked = !!s.highRefresh;
  }
  applyGyroResponse(s);
  if (s.resolution !== undefined) {
    const opt1440 = document.getElementById('optRes1440');
    if (opt1440) opt1440.hidden = opt1440.disabled = (s.allow1440 === false && s.resolution !== '1440p');
    const el = document.getElementById('selectResolution');
    if (el) el.value = s.resolution;
  }
  if (s.sharpness !== undefined) {
    const el = document.getElementById('inputCas');
    if (el) { el.value = s.sharpness; updateCasLabel(s.sharpness); }
  }
  if (s.upscaler !== undefined) {
    const el = document.getElementById('selectUpscaler');
    if (el) el.value = s.upscaler;
  }
  if (s.deband !== undefined) {
    const el = document.getElementById('selectDeband');
    if (el) el.value = s.deband;
  }
  if (s.padSpeakerVolume !== undefined) {
    const el = document.getElementById('inputPadSpeaker');
    if (el) { el.value = s.padSpeakerVolume; document.getElementById('valPadSpeaker').textContent = s.padSpeakerVolume + '%'; }
  }
  if (s.triggerProfile !== undefined) {
    const el = document.getElementById('selectSmartTriggers');
    if (el) el.value = s.triggerProfile;
  }
  if (s.deadzoneLeft !== undefined) {
    const el = document.getElementById('inputDeadzoneL');
    if (el) { el.value = s.deadzoneLeft; document.getElementById('valDeadzoneL').textContent = s.deadzoneLeft + '%'; }
  }
  if (s.deadzoneRight !== undefined) {
    const el = document.getElementById('inputDeadzoneR');
    if (el) { el.value = s.deadzoneRight; document.getElementById('valDeadzoneR').textContent = s.deadzoneRight + '%'; }
  }
  if (s.triggerStrength !== undefined) {
    const el = document.getElementById('inputTriggerStrength');
    if (el) { el.value = s.triggerStrength; document.getElementById('valTriggerStrength').textContent = s.triggerStrength; }
  }
  if (s.lightBarMode !== undefined) {
    const el = document.getElementById('selectLightBarMode');
    if (el) el.value = s.lightBarMode;
    onLightBarModeChange();
  }
  if (s.lightBarColour) {
    const el = document.getElementById('inputLightBarColor');
    if (el) el.value = s.lightBarColour;
  }
  if (s.queueReadyAlert !== undefined) {
    const el = document.getElementById('checkQueueAlert');
    if (el) el.checked = !!s.queueReadyAlert;
  }
  if (s.usbControllers !== undefined) {
    const el = document.getElementById('checkUsbControllers');
    if (el) el.checked = !!s.usbControllers;
  }
  {
    const row = document.getElementById('rowGuitarStrum');
    if (row) row.style.display = s.guitarConnected ? 'flex' : 'none';
    const el = document.getElementById('checkGuitarStrum');
    if (el && s.guitarStrum !== undefined) el.checked = !!s.guitarStrum;
  }
  if (s.videoDecoder !== undefined) {
    const el = document.getElementById('selectVideoDecoder');
    if (el) el.value = s.videoDecoder;
  }
  if (s.uiScale !== undefined) {
    const el = document.getElementById('selectUiScale');
    if (el) el.value = String(s.uiScale);
  }
  if (s.storeCountry !== undefined) {
    const el = document.getElementById('selectStoreCountry');
    if (el) el.value = s.storeCountry;
  }
  if (s.voiceRouting !== undefined) {
    const el = document.getElementById('selectVoiceRouting');
    if (el) el.value = s.voiceRouting;
  }
  applySearchTabUI(currentSearchTab);
  updateSearchStatusText();
  renderPadMap();
  renderKeyMap();
  renderAllTouchpadGrids(s.touchpadShortcuts);
}

function syncFromHost() {
  loadAllSettingsFromServer();
}

function loadAllSettingsFromServer() {
  fetch('/api/settings').then(r => r.json()).then(s => {
    applyLoadedSettings(s);
    setTimeout(() => { isInitializing = false; }, 250);
  }).catch(() => {
    renderPadMap();
    renderKeyMap();
    renderAllTouchpadGrids(currentSettings.touchpadShortcuts);
    setTimeout(() => { isInitializing = false; }, 250);
  });
}
loadAllSettingsFromServer();

// --- Virtual Gamepad & Streaming Polling ---
let activePlayer = 1;
const padState = {
  btnA: false, btnB: false, btnX: false, btnY: false,
  dpadUp: false, dpadDown: false, dpadLeft: false, dpadRight: false,
  btnL1: false, btnR1: false, btnL3: false, btnR3: false,
  btnView: false, btnMenu: false, btnNexus: false,
  triggerL2: 0.0, triggerR2: 0.0,
  leftStickX: 0.0, leftStickY: 0.0,
  rightStickX: 0.0, rightStickY: 0.0
};
let padSendScheduled = false;

function setVirtualPlayer(p) {
  const prevSlot = activePlayer - 1;
  activePlayer = p;
  for (let i = 1; i <= 4; i++) {
    const btn = document.getElementById('btnPlayer' + i);
    if (btn) btn.classList.toggle('active', p === i);
  }
  const statusEl = document.getElementById('gamepadStatus');
  if (statusEl) {
    statusEl.textContent = 'Controle ativo para Jogador ' + p + (p === 1 ? ' (Principal)' : ' (Co-op Local)');
  }
  sendPadState(true, prevSlot);
}

function toggleGamepadFullscreen() {
  const card = document.getElementById('gamepadCard');
  if (!card) return;
  const isFs = card.classList.contains('gamepad-fullscreen') || document.fullscreenElement === card || document.webkitFullscreenElement === card;
  if (!isFs) {
    card.classList.add('gamepad-fullscreen');
    if (card.requestFullscreen) {
      card.requestFullscreen().catch(() => {});
    } else if (card.webkitRequestFullscreen) {
      try { card.webkitRequestFullscreen(); } catch (_) {}
    }
    if (screen.orientation && screen.orientation.unlock) {
      try { screen.orientation.unlock(); } catch (_) {}
    }
  } else {
    card.classList.remove('gamepad-fullscreen');
    if (document.fullscreenElement || document.webkitFullscreenElement) {
      if (document.exitFullscreen) document.exitFullscreen().catch(() => {});
      else if (document.webkitExitFullscreen) {
        try { document.webkitExitFullscreen(); } catch (_) {}
      }
    }
    if (screen.orientation && screen.orientation.unlock) {
      try { screen.orientation.unlock(); } catch (_) {}
    }
  }
  setTimeout(updateGamepadScale, 20);
  setTimeout(updateGamepadScale, 150);
}

document.addEventListener('fullscreenchange', () => {
  const card = document.getElementById('gamepadCard');
  if (card && !document.fullscreenElement && !document.webkitFullscreenElement) {
    card.classList.remove('gamepad-fullscreen');
  }
  setTimeout(updateGamepadScale, 30);
});
document.addEventListener('webkitfullscreenchange', () => {
  const card = document.getElementById('gamepadCard');
  if (card && !document.fullscreenElement && !document.webkitFullscreenElement) {
    card.classList.remove('gamepad-fullscreen');
  }
  setTimeout(updateGamepadScale, 30);
});

function sendPadState(activeSwitch = false, prevPlayer = -1) {
  const payload = { player: activePlayer, activeSwitch: !!activeSwitch, ...padState };
  if (prevPlayer !== null && prevPlayer !== undefined && prevPlayer >= 0 && prevPlayer < 4 && prevPlayer !== (activePlayer - 1)) {
    payload.prevPlayer = prevPlayer;
  }
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  } else {
    fetch('/api/gamepad', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    }).catch(() => {});
  }
}

function schedulePadSend() {
  if (padSendScheduled) return;
  padSendScheduled = true;
  requestAnimationFrame(() => {
    padSendScheduled = false;
    sendPadState(false);
  });
}

// --- PSBox Web Layout Studio State & Engine ---
const defaultLayoutConfig = {
  hapticMs: 14,
  stickSensitivity: 1.0,
  modules: {
    mod_stick_left: { x: 32, y: 97, scale: 1.3 },
    mod_abxy: { x: 712, y: 97, scale: 1.3 },
    mod_dpad: { x: 205, y: 242, scale: 1.05 },
    mod_stick_right: { x: 545, y: 240, scale: 1.2 },
    mod_btn_lt: { x: 10, y: 10, scale: 1.0 },
    mod_btn_lb: { x: 110, y: 11, scale: 1.0 },
    mod_btn_ls: { x: 192, y: 11, scale: 1.0 },
    mod_btn_rs: { x: 669, y: 11, scale: 1.0 },
    mod_btn_rb: { x: 729, y: 11, scale: 1.0 },
    mod_btn_rt: { x: 811, y: 10, scale: 1.0 },
    mod_center_sys: { x: 342, y: 66, scale: 1.0 },
    mod_player_bar: { x: 381, y: 12, scale: 1.0 }
  }
};

let studioLayout = loadStudioLayout();
let isStudioEditing = false;
let activeStudioModuleId = 'mod_stick_left';

function loadStudioLayout() {
  try {
    const raw = localStorage.getItem('psbox_layout_studio_v1');
    if (raw) {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') {
        return {
          hapticMs: typeof parsed.hapticMs === 'number' ? Math.max(0, Math.min(50, parsed.hapticMs)) : 14,
          stickSensitivity: typeof parsed.stickSensitivity === 'number' ? Math.max(0.5, Math.min(2.0, parsed.stickSensitivity)) : 1.0,
          modules: (parsed.modules && typeof parsed.modules === 'object') ? parsed.modules : {}
        };
      }
    }
  } catch (_) {}
  return JSON.parse(JSON.stringify(defaultLayoutConfig));
}

function saveStudioLayout() {
  try {
    localStorage.setItem('psbox_layout_studio_v1', JSON.stringify(studioLayout));
  } catch (_) {}
}

function applyStudioLayout() {
  const isLandscape = window.innerWidth > window.innerHeight;
  if (!isLandscape) {
    document.querySelectorAll('.gamepad-module').forEach(m => {
      m.style.removeProperty('left');
      m.style.removeProperty('top');
      m.style.removeProperty('transform');
    });
    return;
  }
  if (!studioLayout || !studioLayout.modules) return;
  for (const [modId, conf] of Object.entries(studioLayout.modules)) {
    const el = document.getElementById(modId);
    if (el && conf) {
      if (typeof conf.x === 'number' && typeof conf.y === 'number') {
        el.style.setProperty('left', conf.x + 'px', 'important');
        el.style.setProperty('top', conf.y + 'px', 'important');
      }
      if (typeof conf.scale === 'number') {
        el.style.setProperty('transform', 'scale(' + conf.scale + ')', 'important');
      }
    }
  }
}

function toggleLayoutStudio() {
  isStudioEditing = !isStudioEditing;
  const toolbar = document.getElementById('studioToolbar');
  const card = document.getElementById('gamepadCard');
  const btn = document.getElementById('btnToggleStudio');
  if (toolbar) toolbar.style.display = isStudioEditing ? 'block' : 'none';
  if (card) card.classList.toggle('studio-editing', isStudioEditing);
  if (btn) {
    btn.classList.toggle('btn-primary', isStudioEditing);
    btn.classList.toggle('btn-secondary', !isStudioEditing);
    btn.innerHTML = isStudioEditing ? '✓ Concluir Edição' : '🎨 Layout Studio';
  }
  if (isStudioEditing) {
    selectStudioModule(activeStudioModuleId);
    syncStudioControls();
  } else {
    saveStudioLayout();
    document.querySelectorAll('.gamepad-module').forEach(m => m.classList.remove('active-module'));
  }
}

function selectStudioModule(modId) {
  activeStudioModuleId = modId;
  const select = document.getElementById('studioModuleSelect');
  if (select && select.value !== modId) select.value = modId;
  document.querySelectorAll('.gamepad-module').forEach(m => {
    m.classList.toggle('active-module', m.id === modId);
  });
  const conf = (studioLayout.modules && studioLayout.modules[modId]) || { scale: 1.0 };
  const scale = conf.scale !== undefined ? conf.scale : 1.0;
  const slider = document.getElementById('studioScaleSlider');
  if (slider) slider.value = scale.toFixed(2);
  const lbl = document.getElementById('lblModScale');
  if (lbl) lbl.textContent = 'Escala: ' + scale.toFixed(2) + 'x';
}

function onStudioSelectModule(modId) {
  selectStudioModule(modId);
}

function onStudioScaleChange(scale) {
  scale = Math.max(0.8, Math.min(1.6, scale));
  if (!studioLayout.modules[activeStudioModuleId]) {
    studioLayout.modules[activeStudioModuleId] = {};
  }
  studioLayout.modules[activeStudioModuleId].scale = Math.round(scale * 100) / 100;
  const el = document.getElementById(activeStudioModuleId);
  if (el) el.style.setProperty('transform', 'scale(' + scale + ')', 'important');
  const lbl = document.getElementById('lblModScale');
  if (lbl) lbl.textContent = 'Escala: ' + scale.toFixed(2) + 'x';
  saveStudioLayout();
}

function onStudioSensChange(sens) {
  sens = Math.max(0.5, Math.min(2.0, sens));
  studioLayout.stickSensitivity = Math.round(sens * 10) / 10;
  const lbl = document.getElementById('lblStickSens');
  if (lbl) lbl.textContent = 'Sensibilidade: ' + studioLayout.stickSensitivity.toFixed(1) + 'x';
  saveStudioLayout();
}

function onStudioHapticChange(ms) {
  ms = Math.max(0, Math.min(50, ms));
  studioLayout.hapticMs = ms;
  const lbl = document.getElementById('lblHaptic');
  if (lbl) lbl.textContent = 'Haptic: ' + ms + 'ms';
  saveStudioLayout();
}

function testHaptic() {
  const ms = studioLayout.hapticMs !== undefined ? studioLayout.hapticMs : 14;
  if (ms > 0 && navigator.vibrate) {
    try { navigator.vibrate(ms); } catch (_) {}
  }
}

function exportStudioLayout() {
  const json = JSON.stringify(studioLayout, null, 2);
  const blob = new Blob([json], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'psbox_layout_studio_v1.json';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

function importStudioLayout(event) {
  const file = event.target.files && event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = (e) => {
    try {
      const parsed = JSON.parse(e.target.result);
      if (parsed && typeof parsed === 'object') {
        if (typeof parsed.hapticMs === 'number') studioLayout.hapticMs = Math.max(0, Math.min(50, parsed.hapticMs));
        if (typeof parsed.stickSensitivity === 'number') studioLayout.stickSensitivity = Math.max(0.5, Math.min(2.0, parsed.stickSensitivity));
        if (parsed.modules && typeof parsed.modules === 'object') {
          studioLayout.modules = parsed.modules;
        }
        applyStudioLayout();
        saveStudioLayout();
        syncStudioControls();
      }
    } catch (_) {}
  };
  reader.readAsText(file);
  event.target.value = '';
}

function resetStudioLayout() {
  try { localStorage.removeItem('psbox_layout_studio_v1'); } catch (_) {}
  studioLayout = JSON.parse(JSON.stringify(defaultLayoutConfig));
  document.querySelectorAll('.gamepad-module').forEach(m => {
    m.style.removeProperty('left');
    m.style.removeProperty('top');
    m.style.removeProperty('transform');
  });
  applyStudioLayout();
  saveStudioLayout();
  syncStudioControls();
}

function syncStudioControls() {
  const modId = activeStudioModuleId;
  const conf = (studioLayout.modules && studioLayout.modules[modId]) || { scale: 1.0 };
  const scale = conf.scale !== undefined ? conf.scale : 1.0;
  const scaleSlider = document.getElementById('studioScaleSlider');
  if (scaleSlider) scaleSlider.value = scale.toFixed(2);
  const lblScale = document.getElementById('lblModScale');
  if (lblScale) lblScale.textContent = 'Escala: ' + scale.toFixed(2) + 'x';

  const sens = studioLayout.stickSensitivity !== undefined ? studioLayout.stickSensitivity : 1.0;
  const sensSlider = document.getElementById('studioSensSlider');
  if (sensSlider) sensSlider.value = sens.toFixed(1);
  const lblSens = document.getElementById('lblStickSens');
  if (lblSens) lblSens.textContent = 'Sensibilidade: ' + sens.toFixed(1) + 'x';

  const haptic = studioLayout.hapticMs !== undefined ? studioLayout.hapticMs : 14;
  const hapticSlider = document.getElementById('studioHapticSlider');
  if (hapticSlider) hapticSlider.value = haptic;
  const lblHaptic = document.getElementById('lblHaptic');
  if (lblHaptic) lblHaptic.textContent = 'Haptic: ' + haptic + 'ms';
}

function initStudioDraggableModules() {
  const area = document.getElementById('gamepadArea');
  if (!area) return;

  document.querySelectorAll('.gamepad-module').forEach(mod => {
    const modId = mod.id;
    let startX = 0, startY = 0, origLeft = 0, origTop = 0;
    let isDragging = false;
    let dragTouchId = null;

    const onDragStart = (clientX, clientY, touchId) => {
      if (!isStudioEditing) return;
      selectStudioModule(modId);
      isDragging = true;
      dragTouchId = touchId;
      startX = clientX;
      startY = clientY;
      origLeft = parseFloat(mod.style.left) || mod.offsetLeft;
      origTop = parseFloat(mod.style.top) || mod.offsetTop;
    };

    const onDragMove = (clientX, clientY) => {
      if (!isDragging || !isStudioEditing) return;
      const rect = area.getBoundingClientRect();
      const scaleX = rect.width / (area.offsetWidth || 1);
      const scale = scaleX > 0 ? scaleX : 1.0;
      const dx = (clientX - startX) / scale;
      const dy = (clientY - startY) / scale;
      const maxW = area.offsetWidth || 915;
      const maxH = area.offsetHeight || 412;
      const minX = modId === 'mod_stick_left' ? -25 : 0;
      const newX = Math.max(minX, Math.min(maxW - 40, origLeft + dx));
      const newY = Math.max(0, Math.min(maxH - 30, origTop + dy));
      mod.style.setProperty('left', Math.round(newX) + 'px', 'important');
      mod.style.setProperty('top', Math.round(newY) + 'px', 'important');
      if (!studioLayout.modules[modId]) studioLayout.modules[modId] = {};
      studioLayout.modules[modId].x = Math.round(newX);
      studioLayout.modules[modId].y = Math.round(newY);
    };

    const onDragEnd = () => {
      if (!isDragging) return;
      isDragging = false;
      dragTouchId = null;
      saveStudioLayout();
    };

    mod.addEventListener('mousedown', (e) => {
      if (isStudioEditing) {
        e.preventDefault();
        e.stopPropagation();
        onDragStart(e.clientX, e.clientY, null);
      }
    });

    mod.addEventListener('touchstart', (e) => {
      if (isStudioEditing) {
        e.preventDefault();
        e.stopPropagation();
        if (e.changedTouches.length > 0) {
          const t = e.changedTouches[0];
          onDragStart(t.clientX, t.clientY, t.identifier);
        }
      }
    }, { passive: false });

    window.addEventListener('mousemove', (e) => {
      if (isDragging && dragTouchId === null) {
        e.preventDefault();
        onDragMove(e.clientX, e.clientY);
      }
    });

    window.addEventListener('touchmove', (e) => {
      if (isDragging && dragTouchId !== null) {
        for (let i = 0; i < e.changedTouches.length; i++) {
          if (e.changedTouches[i].identifier === dragTouchId) {
            e.preventDefault();
            onDragMove(e.changedTouches[i].clientX, e.changedTouches[i].clientY);
            break;
          }
        }
      }
    }, { passive: false });

    window.addEventListener('mouseup', () => {
      if (isDragging && dragTouchId === null) onDragEnd();
    });

    window.addEventListener('touchend', (e) => {
      if (isDragging && dragTouchId !== null) {
        for (let i = 0; i < e.changedTouches.length; i++) {
          if (e.changedTouches[i].identifier === dragTouchId) {
            onDragEnd();
            break;
          }
        }
      }
    });

    window.addEventListener('touchcancel', (e) => {
      if (isDragging && dragTouchId !== null) {
        for (let i = 0; i < e.changedTouches.length; i++) {
          if (e.changedTouches[i].identifier === dragTouchId) {
            onDragEnd();
            break;
          }
        }
      }
    });
  });
}

function bindPadButtons() {
  document.querySelectorAll('[data-btn]').forEach(el => {
    const key = el.getAttribute('data-btn');
    const isTrigger = key === 'triggerL2' || key === 'triggerR2';
    const activeTouches = new Set();

    const setDown = () => {
      if (isStudioEditing) return;
      if (isTrigger) padState[key] = 1.0;
      else padState[key] = true;
      el.classList.add('pressed');
      const haptic = (studioLayout && studioLayout.hapticMs !== undefined) ? studioLayout.hapticMs : 14;
      if (haptic > 0 && navigator.vibrate) {
        try { navigator.vibrate(haptic); } catch (_) {}
      }
      sendPadState(false);
    };

    const setUp = () => {
      if (isStudioEditing) return;
      if (isTrigger) padState[key] = 0.0;
      else padState[key] = false;
      el.classList.remove('pressed');
      sendPadState(false);
    };

    el.addEventListener('touchstart', (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        activeTouches.add(e.changedTouches[i].identifier);
      }
      setDown();
    }, { passive: false });

    const onTouchEnd = (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        activeTouches.delete(e.changedTouches[i].identifier);
      }
      if (activeTouches.size === 0) setUp();
    };

    el.addEventListener('touchend', onTouchEnd, { passive: false });
    el.addEventListener('touchcancel', onTouchEnd, { passive: false });
    el.addEventListener('mousedown', (e) => { e.preventDefault(); setDown(); });
    el.addEventListener('mouseup', (e) => { e.preventDefault(); setUp(); });
    el.addEventListener('mouseleave', () => { if (activeTouches.size === 0 && el.classList.contains('pressed')) setUp(); });
  });
}

function setupStick(zoneId, headId, stickKeyX, stickKeyY) {
  const zone = document.getElementById(zoneId);
  const head = document.getElementById(headId);
  if (!zone || !head) return;
  const modParent = zone.closest('.gamepad-module');

  let activeTouchId = null;
  let activePointerId = null;

  function updateStick(clientX, clientY) {
    if (isStudioEditing) return;
    const rect = zone.getBoundingClientRect();
    const maxR = Math.max(28, (rect.width - head.offsetWidth) / 2);
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    let dx = clientX - cx;
    let dy = clientY - cy;
    const dist = Math.hypot(dx, dy);
    if (dist > maxR) {
      dx = (dx / dist) * maxR;
      dy = (dy / dist) * maxR;
    }
    head.style.transform = `translate(${dx}px, ${dy}px)`;
    const sens = (studioLayout && studioLayout.stickSensitivity !== undefined) ? studioLayout.stickSensitivity : 1.0;
    let normX = (dx / maxR) * sens;
    let normY = (dy / maxR) * sens;
    normX = Math.max(-1.0, Math.min(1.0, normX));
    normY = Math.max(-1.0, Math.min(1.0, normY));
    padState[stickKeyX] = Math.round(normX * 100) / 100;
    padState[stickKeyY] = Math.round(normY * 100) / 100;
    schedulePadSend();
  }

  function resetStick() {
    head.style.transform = 'translate(0px, 0px)';
    padState[stickKeyX] = 0.0;
    padState[stickKeyY] = 0.0;
    activeTouchId = null;
    activePointerId = null;
    sendPadState(false);
  }

  const onTouchStart = (e) => {
    if (isStudioEditing) return;
    e.preventDefault();
    if (activeTouchId === null && e.changedTouches.length > 0) {
      activeTouchId = e.changedTouches[0].identifier;
      updateStick(e.changedTouches[0].clientX, e.changedTouches[0].clientY);
    }
  };

  zone.addEventListener('touchstart', onTouchStart, { passive: false });
  if (modParent && modParent !== zone) {
    modParent.addEventListener('touchstart', onTouchStart, { passive: false });
  }

  window.addEventListener('touchmove', (e) => {
    if (isStudioEditing || activeTouchId === null) return;
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === activeTouchId) {
        e.preventDefault();
        updateStick(e.changedTouches[i].clientX, e.changedTouches[i].clientY);
        break;
      }
    }
  }, { passive: false });

  const onTouchEnd = (e) => {
    if (activeTouchId === null) return;
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === activeTouchId) {
        resetStick();
        break;
      }
    }
  };
  window.addEventListener('touchend', onTouchEnd, { passive: false });
  window.addEventListener('touchcancel', onTouchEnd, { passive: false });

  // Pointer events with setPointerCapture for smooth dragging to x = 0 (left screen edge)
  const onPointerDown = (e) => {
    if (isStudioEditing) return;
    if (e.pointerType === 'touch') return;
    if (activePointerId === null) {
      activePointerId = e.pointerId;
      try { zone.setPointerCapture(e.pointerId); } catch (_) {}
      updateStick(e.clientX, e.clientY);
    }
  };

  zone.addEventListener('pointerdown', onPointerDown);
  if (modParent && modParent !== zone) {
    modParent.addEventListener('pointerdown', onPointerDown);
  }

  window.addEventListener('pointermove', (e) => {
    if (isStudioEditing || activePointerId === null) return;
    if (e.pointerId === activePointerId) {
      updateStick(e.clientX, e.clientY);
    }
  });

  const onPointerUp = (e) => {
    if (activePointerId !== null && e.pointerId === activePointerId) {
      resetStick();
    }
  };
  window.addEventListener('pointerup', onPointerUp);
  window.addEventListener('pointercancel', onPointerUp);

  // Mouse fallback
  let isMouseDown = false;
  zone.addEventListener('mousedown', (e) => {
    if (isStudioEditing) return;
    isMouseDown = true;
    updateStick(e.clientX, e.clientY);
  });
  window.addEventListener('mousemove', (e) => {
    if (isMouseDown) updateStick(e.clientX, e.clientY);
  });
  window.addEventListener('mouseup', () => {
    if (isMouseDown) {
      isMouseDown = false;
      resetStick();
    }
  });
}

bindPadButtons();
setupStick('leftStickZone', 'leftStickHead', 'leftStickX', 'leftStickY');
setupStick('rightStickZone', 'rightStickHead', 'rightStickX', 'rightStickY');

applyStudioLayout();
initStudioDraggableModules();
syncStudioControls();

// Periodic streaming state poll and PS5 <-> WebUI settings version sync
let isCurrentlyStreaming = false;
function pollStreamingState() {
  fetch('/api/status').then(r => r.json()).then(data => {
    const streaming = !!data.streaming;
    if (streaming !== isCurrentlyStreaming) {
      isCurrentlyStreaming = streaming;
      const overlay = document.getElementById('searchOverlay');
      if (overlay) overlay.style.display = streaming ? 'flex' : 'none';
      if (searchInput) searchInput.disabled = streaming;
    }
    if (data.settingsVersion !== undefined && data.settingsVersion !== lastSettingsVersion) {
      lastSettingsVersion = data.settingsVersion;
      applyLoadedSettings(data);
      loadStreamMenuState();
    }
  }).catch(() => {});
}
setInterval(pollStreamingState, 1500);
pollStreamingState();

// Gamepad keepalive while gamepad tab is open (300ms ensures held buttons never drop)
setInterval(() => {
  const gpPane = document.getElementById('tab-gamepad');
  if (gpPane && gpPane.style.display !== 'none') {
    sendPadState(false);
  }
}, 300);

function updateGamepadScale() {
  const container = document.getElementById('gamepadScaleContainer');
  const card = document.getElementById('gamepadCard');
  if (!container || !card) return;
  const isFs = card.classList.contains('gamepad-fullscreen') || document.fullscreenElement === card || document.webkitFullscreenElement === card;
  const isLandscape = window.innerWidth > window.innerHeight;

  if (isFs) {
    const w = window.innerWidth;
    const h = window.innerHeight;
    const availW = container.clientWidth ? Math.max(200, container.clientWidth - 8) : (w - 28);
    const availH = container.clientHeight ? Math.max(150, container.clientHeight - 8) : (h - 20);
    if (!isLandscape) {
      const scale = Math.min(1.2, Math.max(0.4, Math.min(availW / 412, availH / 680)));
      document.documentElement.style.setProperty('--gp-fs-scale', scale.toFixed(3));
      document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
    } else {
      const scale = Math.min(1.2, Math.max(0.4, Math.min(availW / 915, availH / 412)));
      document.documentElement.style.setProperty('--gp-fs-scale', scale.toFixed(3));
      document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
    }
    applyStudioLayout();
    return;
  }

  let availW = container.clientWidth;
  if (!availW) {
    const padding = window.innerWidth <= 600 ? 16 : 32;
    availW = Math.max(0, window.innerWidth - padding);
  }

  if (!isLandscape) {
    const targetW = Math.max(0, availW - 16);
    const scale = Math.min(1, Math.max(0.4, targetW / 412));
    document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
  } else {
    const targetW = Math.max(0, availW - 16);
    const availH = window.innerHeight;
    const targetH = Math.max(120, availH - 78);
    const scale = Math.min(1, Math.max(0.35, Math.min(targetW / 915, targetH / 412)));
    document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
  }
  applyStudioLayout();
}
window.addEventListener('resize', updateGamepadScale);
window.addEventListener('orientationchange', () => setTimeout(updateGamepadScale, 50));
window.addEventListener('DOMContentLoaded', updateGamepadScale);
if (window.ResizeObserver) {
  const ro = new ResizeObserver(() => updateGamepadScale());
  const cardEl = document.getElementById('gamepadCard');
  if (cardEl) ro.observe(cardEl);
}

// Hash & route navigation support: #gamepad, #gamepad-fs, #logs, #settings, /boost, /gamepad, etc.
function checkHashNav() {
  const p = (location.pathname || '').toLowerCase();
  if (p.includes('/boost')) {
    switchTab('stream');
  } else if (p.includes('/gamepad')) {
    switchTab('gamepad');
  }
  const h = (location.hash || '').replace('#', '');
  if (!h) return;
  if (h === 'gamepad-fs') {
    switchTab('gamepad');
    const card = document.getElementById('gamepadCard');
    if (card && !card.classList.contains('gamepad-fullscreen')) {
      toggleGamepadFullscreen();
    }
  } else if (document.getElementById('tab-' + h)) {
    switchTab(h);
  }
}
window.addEventListener('hashchange', checkHashNav);
checkHashNav();
