const byId = id => document.getElementById(id);
const pickerStatus = byId('picker-status');
const library = byId('library');
const search = byId('search');
const resume = byId('continue');
const refresh = byId('refresh');
const status = byId('status');
const hint = byId('hint');
const screen = byId('frame');
const back = byId('back');
const autoPlay = byId('autoplay');
const gamepad = document.querySelector('.gamepad');
const directions = gamepad.querySelector('.directions');
const buttons = [...gamepad.querySelectorAll('button')];
const pointers = new Map();
const keyboard = new Map();
const keyMap = new Map([
  ['ArrowUp', 64], ['ArrowDown', 128], ['ArrowLeft', 32], ['ArrowRight', 16],
  ['KeyX', 1], ['KeyZ', 2], ['Enter', 8], ['ShiftLeft', 4], ['KeyA', 512], ['KeyS', 256],
]);

const fragment = new URLSearchParams(location.hash.slice(1));
const token = fragment.get('pair') || sessionStorage.getItem('willow-pair');
if (fragment.has('pair')) {
  history.replaceState(null, '', '/');
  if (token) sessionStorage.setItem('willow-pair', token);
}

let socket;
let paired = false;
let attached = false;
let attaching = false;
let paused = false;
let autoPlayState;
let autoPlayPending = false;
let gba = false;
let frameURL;
let retry = 500;
let busySince = 0;
let lastKeys = 0;
// Library state from the PC; `wanted` is the game the phone is opening.
let sessionId;
let games = [];
let running = null;
let currentId = '';
let libraryKey = '';
let wanted = null;
let artBusy = false;
const art = new Map();
const artRequested = new Set();

function send(message) {
  if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify(message));
}

function showPage(page) {
  document.body.dataset.page = page;
  if (page === 'picker') {
    release();
    requestLibrary();
  }
}

function setPickerStatus(text, canRefresh = false) {
  pickerStatus.textContent = text;
  refresh.hidden = !canRefresh;
}

// ---- Game picker -------------------------------------------------------

function requestLibrary() {
  if (paired && document.body.dataset.page === 'picker') send({ type: 'library' });
}

function initials(title) {
  return title.replace(/\(.*?\)|\[.*?\]/g, '').split(/[\s\-_:]+/).filter(Boolean).slice(0, 2)
    .map(word => word[0].toUpperCase()).join('') || '?';
}

function gameCard(game) {
  const item = document.createElement('li');
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'game';
  button.dataset.id = game.id;
  button.dataset.platform = game.platform;
  const cover = document.createElement('span');
  cover.className = 'cover';
  if (art.has(game.id)) {
    const image = new Image();
    image.alt = '';
    image.src = art.get(game.id);
    cover.append(image);
  } else {
    cover.textContent = initials(game.title);
  }
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = game.title;
  const meta = document.createElement('span');
  meta.className = 'meta';
  meta.textContent = game.id === currentId && running ? `${game.platform} · Playing` : game.platform;
  button.append(cover, name, meta);
  if (game.id === currentId && running) button.classList.add('playing');
  button.onclick = () => chooseGame(game);
  item.append(button);
  item.dataset.title = game.title.toLowerCase();
  return item;
}

function filterLibrary() {
  const query = search.value.trim().toLowerCase();
  let shown = 0;
  for (const item of library.children) {
    item.hidden = Boolean(query) && !item.dataset.title.includes(query);
    if (!item.hidden) shown++;
  }
  if (games.length && !shown) setPickerStatus('No games match your search.');
  else if (games.length) setPickerStatus(running ? 'Continue the game on the PC, or pick another.' : 'Choose a game to play.');
}

function renderLibrary(message) {
  sessionId = message.session_id;
  games = message.games;
  running = message.running;
  currentId = message.current_id;
  resume.hidden = !running;
  byId('continue-title').textContent = running?.title || '';
  search.hidden = games.length < 8;

  if (!message.folder) setPickerStatus('In Willow on the PC, choose your ROM folder. Games appear here.', true);
  else if (!games.length && message.scanning) setPickerStatus('Scanning your ROM folder on the PC…');
  else if (!games.length) setPickerStatus('No games found in your ROM folder on the PC.', true);

  // Rebuild only when the list changes, so polling keeps the scroll position.
  const key = JSON.stringify([games, currentId, Boolean(running)]);
  if (key !== libraryKey) {
    libraryKey = key;
    library.replaceChildren(...games.map(gameCard));
  }
  filterLibrary();
  requestArtwork();
}

function requestArtwork() {
  if (artBusy || !sessionId) return;
  const ids = games.filter(game => game.art && !artRequested.has(game.id)).slice(0, 12).map(game => game.id);
  if (!ids.length) return;
  for (const id of ids) artRequested.add(id);
  artBusy = true;
  send({ type: 'artwork', session: sessionId, ids });
}

function showArtwork(message) {
  artBusy = false;
  for (const [id, data] of Object.entries(message.art)) {
    const bytes = Uint8Array.from(atob(data), character => character.charCodeAt(0));
    const url = URL.createObjectURL(new Blob([bytes], { type: 'image/jpeg' }));
    art.set(id, url);
    const cover = library.querySelector(`.game[data-id="${id}"] .cover`);
    if (cover) {
      const image = new Image();
      image.alt = '';
      image.src = url;
      cover.replaceChildren(image);
    }
  }
  requestArtwork();
}

function chooseGame(game) {
  if (running && game.id !== currentId) {
    setPickerStatus(`${running.title} is open on the PC. Save it there and return to the game library first.`);
    return;
  }
  wanted = { id: game.id, title: game.title };
  openPlayer(running ? `Opening ${game.title}…` : `Starting ${game.title} on the PC…`);
  send({ type: 'launch', session: sessionId, id: game.id });
}

function openPlayer(text) {
  clearGame();
  hint.textContent = text;
  status.textContent = text;
  byId('game-title').textContent = wanted?.title || running?.title || '';
  byId('game-title').hidden = !byId('game-title').textContent;
  showPage('play');
  send({ type: 'sessions' });
}

resume.onclick = () => {
  wanted = { id: currentId, title: running?.title };
  openPlayer(`Opening ${running?.title || 'the game'}…`);
};
search.oninput = filterLibrary;
refresh.onclick = () => {
  if (socket?.readyState === WebSocket.OPEN && paired) {
    setPickerStatus('Checking…');
    requestLibrary();
  } else if (socket?.readyState !== WebSocket.CONNECTING) {
    busySince = 0;
    setPickerStatus('Connecting to Willow…');
    connect();
  }
};

// ---- Player ------------------------------------------------------------

const RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128;
// Map a touch on the D-pad to directions by angle from its centre. Cardinal sectors are
// 60 degrees wide and diagonals 30, so grid-based games rarely see accidental diagonals.
function directionAt(event) {
  const box = directions.getBoundingClientRect();
  const x = (event.clientX - box.left) / box.width - .5;
  const y = (event.clientY - box.top) / box.height - .5;
  if (Math.hypot(x, y) < .1) return 0;
  const degrees = (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;
  const sectors = [[0, RIGHT], [90, DOWN], [180, LEFT], [270, UP], [360, RIGHT]];
  for (const [centre, bit] of sectors) {
    if (Math.abs(degrees - centre) <= 30) return bit;
  }
  if (degrees < 90) return RIGHT | DOWN;
  if (degrees < 180) return LEFT | DOWN;
  if (degrees < 270) return LEFT | UP;
  return RIGHT | UP;
}

function updateKeys(heartbeat = false) {
  let keys = 0;
  for (const bit of [...pointers.values(), ...keyboard.values()]) keys |= bit;
  if ((keys & 48) === 48) keys &= ~48;
  if ((keys & 192) === 192) keys &= ~192;
  for (const button of buttons) button.toggleAttribute('data-held', (keys & Number(button.dataset.key)) !== 0);
  if (attached && (keys !== lastKeys || (heartbeat && keys))) send({ type: 'keys', keys });
  lastKeys = keys;
}

function release() {
  pointers.clear();
  keyboard.clear();
  updateKeys();
}

function clearGame() {
  release();
  attached = false;
  attaching = false;
  autoPlayState = undefined;
  autoPlayPending = false;
  renderAutoPlay();
  gamepad.inert = true;
  byId('waiting').hidden = false;
  screen.hidden = true;
  screen.removeAttribute('src');
  if (frameURL) URL.revokeObjectURL(frameURL);
  frameURL = undefined;
}

function renderAutoPlay() {
  const running = Boolean(autoPlayState?.running);
  autoPlay.disabled = !attached || !autoPlayState || autoPlayPending || Boolean(autoPlayState.stopping);
  autoPlay.toggleAttribute('data-running', running);
  autoPlay.textContent = autoPlayState?.stopping ? 'Stopping…' : running ? 'Stop Auto Play' : 'Auto Play';
}

function renderPlayerStatus() {
  const connection = paused ? 'Paused on PC. Resume in mGBA to play.' : 'Connected';
  const message = autoPlayState?.message;
  const text = message ? (paused ? `${connection} ${message}` : message) : connection;
  if (status.textContent !== text) status.textContent = text;
}

function showPause(value) {
  paused = value;
  if (paused) release();
  gamepad.inert = paused;
  renderPlayerStatus();
}

function returnToPicker(text) {
  const wasAttached = attached;
  clearGame();
  wanted = null;
  if (wasAttached) send({ type: 'disconnect' });
  showPage('picker');
  if (text) setPickerStatus(text);
}

back.onclick = () => returnToPicker();
autoPlay.onclick = () => {
  if (autoPlay.disabled) return;
  autoPlayPending = true;
  renderAutoPlay();
  send({ type: 'autoplay', enabled: !autoPlayState.running });
};

function handle(message) {
  switch (message.type) {
    case 'paired':
      retry = 500;
      busySince = 0;
      paired = true;
      if (document.body.dataset.page === 'play' && wanted) send({ type: 'sessions' });
      else {
        setPickerStatus('Loading your games…');
        requestLibrary();
      }
      break;
    case 'library':
      renderLibrary(message);
      break;
    case 'artwork':
      showArtwork(message);
      break;
    case 'launching':
      send({ type: 'sessions' });
      break;
    case 'sessions': {
      if (document.body.dataset.page !== 'play' || attached || attaching) break;
      // Prefer the library window; the game appears once it has finished starting.
      const game = message.games.find(item => item.session_id === sessionId) || message.games[0];
      if (game) {
        attaching = true;
        status.textContent = `Opening ${game.title || 'game'}…`;
        send({ type: 'connect', session: game.session_id, game: game.game_id });
      }
      break;
    }
    case 'attached':
      if (document.body.dataset.page !== 'play') {
        attaching = false;
        send({ type: 'disconnect' });
        break;
      }
      attached = true;
      attaching = false;
      autoPlayState = message.autoplay;
      renderAutoPlay();
      gba = message.platform === 'GBA';
      for (const button of buttons) {
        if (Number(button.dataset.key) > 255) button.disabled = !gba;
      }
      byId('game-title').textContent = message.title || 'Untitled game';
      byId('game-title').hidden = false;
      hint.textContent = 'Waiting for the picture…';
      showPause(message.paused);
      break;
    case 'pause':
      if (attached) showPause(message.paused);
      break;
    case 'autoplay':
      if (!attached) break;
      autoPlayState = message.autoplay;
      if (message.acknowledged) autoPlayPending = false;
      renderAutoPlay();
      renderPlayerStatus();
      break;
    case 'autoplay-error':
      if (!attached) break;
      autoPlayPending = false;
      renderAutoPlay();
      status.textContent = message.message;
      break;
    case 'detached':
      if (document.body.dataset.page === 'play') {
        returnToPicker(message.message ? 'The game closed on the PC. Choose a game to continue.' : undefined);
      }
      break;
    case 'error':
      attaching = false;
      if (document.body.dataset.page === 'play' && !attached) returnToPicker(message.message);
      else if (document.body.dataset.page === 'picker') setPickerStatus(message.message, true);
      else status.textContent = message.message;
      break;
  }
}

function connect() {
  socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/control`);
  socket.binaryType = 'blob';
  socket.onopen = () => send({ type: 'pair', token });
  socket.onmessage = ({ data }) => {
    if (typeof data === 'string') return handle(JSON.parse(data));
    if (!attached) return;
    const previous = frameURL;
    frameURL = URL.createObjectURL(new Blob([data], { type: 'image/png' }));
    screen.src = frameURL;
    if (previous) URL.revokeObjectURL(previous);
    screen.hidden = false;
    byId('waiting').hidden = true;
  };
  socket.onclose = ({ code }) => {
    paired = false;
    artBusy = false;
    clearGame();
    if (code === 4001) {
      sessionStorage.removeItem('willow-pair');
      document.body.dataset.page = 'picker';
      library.replaceChildren();
      resume.hidden = search.hidden = true;
      setPickerStatus('This pairing link has expired. In Willow on the PC, choose Pair iPhone and scan the new QR code.');
    } else if (code === 4002) {
      // A tab Safari just backgrounded can hold the slot for a few seconds.
      busySince ||= Date.now();
      if (Date.now() - busySince < 15000) {
        setPickerStatus('Waiting for the previous connection to close…');
        setTimeout(connect, 1500);
      } else {
        busySince = 0;
        setPickerStatus('Another Willow Mobile tab is connected. Close it, then tap Check again.', true);
      }
    } else {
      document.body.dataset.page = 'picker';
      setPickerStatus('Lost the connection to the PC. Reconnecting…');
      setTimeout(connect, retry);
      retry = Math.min(retry * 2, 5000);
    }
  };
}

// The D-pad tracks one finger as it slides between arrows and onto diagonals.
directions.onpointerdown = event => {
  if (!attached || paused || event.button !== 0) return;
  event.preventDefault();
  directions.setPointerCapture(event.pointerId);
  pointers.set(event.pointerId, directionAt(event));
  updateKeys();
};
directions.onpointermove = event => {
  if (!pointers.has(event.pointerId)) return;
  const bits = directionAt(event);
  if (pointers.get(event.pointerId) === bits) return;
  pointers.set(event.pointerId, bits);
  updateKeys();
};
directions.onpointerup = directions.onpointercancel = directions.onlostpointercapture = event => {
  if (pointers.delete(event.pointerId)) updateKeys();
};

for (const button of buttons) {
  button.onclick = event => {
    if (event.detail || !attached || paused || button.disabled || lastKeys) return;
    // VoiceOver and keyboard activation produce a click without a pointer event.
    send({ type: 'keys', keys: Number(button.dataset.key) });
    send({ type: 'keys', keys: 0 });
  };
  if (directions.contains(button)) continue;
  button.onpointerdown = event => {
    if (!attached || paused || button.disabled || event.button !== 0) return;
    event.preventDefault();
    button.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, Number(button.dataset.key));
    updateKeys();
  };
  button.onpointerup = button.onpointercancel = button.onlostpointercapture = event => {
    if (pointers.delete(event.pointerId)) updateKeys();
  };
}
gamepad.oncontextmenu = event => event.preventDefault();
window.addEventListener('keydown', event => {
  const bit = keyMap.get(event.code);
  if (!attached || paused || !bit || (bit > 255 && !gba) || event.target.closest('button, input')) return;
  event.preventDefault();
  keyboard.set(event.code, bit);
  updateKeys();
});
window.addEventListener('keyup', event => {
  if (keyboard.delete(event.code)) updateKeys();
});
window.addEventListener('blur', release);
window.addEventListener('pagehide', () => socket?.close());
window.addEventListener('pageshow', event => { if (event.persisted) location.reload(); });
// Opening a new pairing link in this tab only changes the fragment, so reload to read it.
window.addEventListener('hashchange', () => {
  if (new URLSearchParams(location.hash.slice(1)).has('pair')) location.reload();
});
document.addEventListener('visibilitychange', () => { if (document.hidden) release(); });
setInterval(() => { if (attached && !paused && !document.hidden) updateKeys(true); }, 150);
// Keep the picker current (running game, finished scans) and wait for a starting game.
setInterval(() => {
  if (!paired || document.hidden) return;
  if (document.body.dataset.page === 'picker') requestLibrary();
  else if (!attached && !attaching) send({ type: 'sessions' });
}, 2500);

if (token) connect();
else setPickerStatus('In Willow on the PC, choose Pair iPhone, then scan the QR code with Camera.');
