import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { mkdtemp, mkdir, writeFile, readFile, readdir, rm } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { KevClient } from '../../local-model/kev-client.mjs';
import { candidatesFor, play } from '../../local-model/player.mjs';

function snapshot() {
  return { frame: 10, connected_clients: 1, game_state: {
    supported: true, available: true, adapter: 'emerald_en_v1', frame: 10,
    interaction: 'overworld', movement_available: true, map: { id: 1 },
    player: { x: 4, y: 4 }, menu: { kind: 'none', input_ready: false }, recent_events: [],
  }, local_map: { available: true, map_id: 1, tiles: [
    { x: 5, y: 4, known: true, collision: 0, occupied: false, special_traversal: false, trainer_risk: true },
    { x: 3, y: 4, known: false }, { x: 4, y: 3, known: true, collision: 1 },
    { x: 4, y: 5, known: true, collision: 0, occupied: true },
  ] } };
}
function harness({ choose = 'move_5_4', probability = 0.9, changeFresh = false, initial = snapshot() } = {}) {
  const calls = [], logs = [];
  let choices = 0;
  const request = async (method, args) => {
    calls.push({ method, args });
    const result = structuredClone(initial);
    if (changeFresh && method === 'get_game_state') result.game_state.player.x++;
    return result;
  };
  return { calls, logs, get choices() { return choices; },
    options: { request, model: { choose: async () => { choices++; return { id: choose, probability }; } },
      goal: 'Explore nearby tiles', maxSteps: 1, log: async record => logs.push(record) } };
}
const inputs = h => h.calls.filter(call => ['press', 'act', 'move_to'].includes(call.method));

test('local adapter uses known adjacent tiles, allows encounters, and refuses unknown prompts', () => {
  const state = snapshot();
  assert.deepEqual(candidatesFor(state).map(action => action.id), ['stop', 'move_5_4', 'press_A']);
  assert.equal(candidatesFor(state)[1].args.avoid_trainers, false);
  state.game_state.interaction = 'dialogue';
  assert.deepEqual(candidatesFor(state).map(action => action.id), ['stop']);
  state.game_state.menu = { kind: 'save' };
  assert.equal(candidatesFor(state).length, 1);
});

test('battle confirms only Fight or a live selected move with PP, not Run or empty moves', () => {
  const result = snapshot();
  result.game_state.interaction = 'battle';
  result.game_state.battle = { available: true };
  result.game_state.menu = { kind: 'battle_action', input_ready: true, cursor: 3 };
  assert.ok(!candidatesFor(result).some(action => action.id === 'press_A'));
  result.game_state.menu.cursor = 0;
  assert.ok(candidatesFor(result).some(action => action.id === 'press_A'));
  result.game_state.menu = { kind: 'battle_move', input_ready: true, cursor: 0, selected_move: { id: 2, pp: 0 } };
  assert.ok(!candidatesFor(result).some(action => action.id === 'press_A'));
  result.game_state.menu.selected_move.pp = 1;
  const confirm = candidatesFor(result).find(action => action.id === 'press_A');
  assert.deepEqual(confirm.args.expected_menu, { kind: 'battle_move', cursor: 0 });
});

test('one model action is rechecked and followed by full evidence logging', async () => {
  const h = harness();
  assert.equal((await play(h.options)).reason, 'step_budget');
  assert.equal(inputs(h).length, 1);
  assert.equal(inputs(h)[0].method, 'move_to');
  assert.equal(h.logs.at(-1).type, 'handoff');
  assert.ok(h.logs.some(record => record.type === 'action' && record.result.game_state));
  const index = h.calls.findIndex(call => call.method === 'move_to');
  assert.equal(h.calls[index - 1].method, 'get_game_state');
});

test('below-threshold probability, stale state, other clients, pause and human keys send no buttons', async () => {
  const cases = [
    { probability: 0.29 }, { changeFresh: true },
    { initial: { ...snapshot(), connected_clients: 2 } },
    { initial: { ...snapshot(), paused: true } },
    { initial: { ...snapshot(), human_keys: 1 } },
  ];
  for (const options of cases) {
    const h = harness(options);
    await play(h.options);
    assert.equal(inputs(h).length, 0);
  }
});

test('recorded 33–38 percent navigation choices no longer stop at step zero', async () => {
  for (const probability of [0.33058912595950146, 0.3367120458217007, 0.3382797707123322, 0.3802431790326186]) {
    const h = harness({ probability });
    const result = await play(h.options);
    assert.equal(result.reason, 'step_budget');
    assert.equal(inputs(h).length, 1);
    assert.equal(inputs(h)[0].method, 'move_to');
    const decision = h.logs.find(record => record.type === 'decision');
    assert.deepEqual(decision.policy, { category: 'navigation', min_probability: 0.3, accepted: true });
  }
});

test('interactions keep the 60 percent gate and an explicit global threshold still applies to movement', async () => {
  const confirm = harness({ choose: 'press_A', probability: 0.38 });
  const stopped = await play(confirm.options);
  assert.equal(stopped.reason, 'low_probability');
  assert.equal(inputs(confirm).length, 0);
  assert.equal(stopped.details.action, 'press_A');
  assert.equal(stopped.details.min_probability, 0.6);
  assert.equal(stopped.details.category, 'confirmation');
  assert.deepEqual(confirm.logs.at(-1).details, stopped.details);
  const strict = harness({ probability: 0.38 });
  assert.equal((await play({ ...strict.options, minProbability: 0.6 })).reason, 'low_probability');
  assert.equal(inputs(strict).length, 0);
});

test('bounded waits and decoded battle cursor movement use navigation thresholds, not confirmation thresholds', async () => {
  const waiting = snapshot();
  waiting.game_state.movement_available = false;
  const wait = harness({ initial: waiting, choose: 'wait', probability: 0.55 });
  assert.equal((await play(wait.options)).reason, 'step_budget');
  assert.deepEqual(inputs(wait).map(call => [call.method, call.args.buttons]), [['act', []]]);

  const battle = snapshot();
  battle.game_state.interaction = 'battle';
  battle.game_state.battle = { available: true };
  battle.game_state.menu = { kind: 'battle_action', input_ready: true, cursor: 0 };
  const cursor = harness({ initial: battle, choose: 'press_Down', probability: 0.35 });
  assert.equal((await play(cursor.options)).reason, 'step_budget');
  assert.equal(inputs(cursor).length, 1);
  assert.deepEqual(inputs(cursor)[0].args.expected_menu, { kind: 'battle_action', cursor: 0 });
  const confirm = harness({ initial: battle, choose: 'press_A', probability: 0.55 });
  assert.equal((await play(confirm.options)).reason, 'low_probability');
  assert.equal(inputs(confirm).length, 0);
});

test('navigation override, model stops and invalid scores are handled without disabling safety gates', async () => {
  const h = harness({ probability: 0.38 });
  assert.equal((await play({ ...h.options, minNavigationProbability: 0.5 })).reason, 'low_probability');
  assert.equal(inputs(h).length, 0);
  const stopped = harness({ choose: 'stop', probability: 0.35 });
  assert.equal((await play(stopped.options)).reason, 'model_stop');
  assert.equal(inputs(stopped).length, 0);
  for (const probability of [-0.1, 1.1, NaN, Infinity]) {
    const invalid = harness({ probability });
    await assert.rejects(play(invalid.options), /Invalid model decision/);
    assert.equal(inputs(invalid).length, 0);
  }
  for (const settings of [{ minProbability: NaN }, { minNavigationProbability: -1 }, { minNavigationProbability: 1.1 }]) {
    await assert.rejects(play({ ...h.options, ...settings }), /Invalid local-player limits/);
  }
});

test('unsupported ROM never invokes inference or sends input', async () => {
  const initial = snapshot();
  initial.game_state = { supported: false };
  const h = harness({ initial });
  assert.equal((await play(h.options)).reason, 'unsupported_or_unavailable_state');
  assert.equal(h.choices, 0);
  assert.equal(inputs(h).length, 0);
});

test('confirmation with no verified change waits, then stops instead of retrying A', async () => {
  const h = harness({ choose: 'press_A' });
  const result = await play({ ...h.options, maxSteps: 30 });
  assert.equal(result.reason, 'unverified_confirmation');
  assert.equal(inputs(h).filter(call => call.method === 'press').length, 1);
  assert.ok(inputs(h).filter(call => call.method === 'act').every(call => call.args.buttons.length === 0));
});

test('abort during inference and invalid decisions never reach controls', async () => {
  const controller = new AbortController();
  const h = harness();
  const model = { choose: async () => { controller.abort(); return { id: 'press_A', probability: 1 }; } };
  assert.equal((await play({ ...h.options, model, signal: controller.signal })).reason, 'cancelled');
  assert.equal(inputs(h).length, 0);
  const invalid = harness({ choose: 'reset_game' });
  await assert.rejects(play(invalid.options), /Invalid model decision/);
  assert.equal(inputs(invalid).length, 0);
  assert.equal(invalid.logs.at(-1).reason, 'error');
});

test('wall-clock budget aborts a slow inference request', async () => {
  const h = harness();
  const model = { choose: (_state, _candidates, signal) => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => reject(signal.reason), { once: true });
  }) };
  // Keep the test process alive: AbortSignal.timeout timers are unref'ed.
  const keepAlive = setTimeout(() => {}, 1000);
  try {
    assert.equal((await play({ ...h.options, model, maxMs: 20 })).reason, 'time_budget');
    assert.equal(inputs(h).length, 0);
  } finally { clearTimeout(keepAlive); }
});

test('Kev HTTP contract uses answers.action and selected probability, not adjusted confidence', async t => {
  let received, status = 200;
  let answer = { type: 'choice', choice: 'stop', confidence: 0.6, probabilities: { stop: 0.8, wait: 0.2 } };
  const server = http.createServer(async (req, res) => {
    let body = '';
    for await (const chunk of req) body += chunk;
    received = { path: req.url, auth: req.headers.authorization, body: JSON.parse(body) };
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ answers: { action: answer } }));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => { server.closeAllConnections(); server.close(); });
  const client = new KevClient({ baseUrl: `http://127.0.0.1:${server.address().port}`, apiKey: 'local-test' });
  const candidates = [{ id: 'stop', description: 'Stop' }, { id: 'wait', description: 'Wait' }];
  assert.equal((await client.choose({ goal: 'Test' }, candidates)).probability, 0.8);
  assert.equal(received.path, '/v1/systemone');
  assert.equal(received.auth, 'Bearer local-test');
  assert.equal(received.body.questions.action.type, 'choice');
  assert.deepEqual(received.body.questions.action.criteria, { stop: 'Stop', wait: 'Wait' });
  answer = { ...answer, choice: 'shell_command' };
  await assert.rejects(client.choose({}, candidates), /Malformed/);
  answer = { type: 'choice', choice: 'stop', probabilities: { stop: 0.9, wait: 0.9 } };
  await assert.rejects(client.choose({}, candidates), /Malformed/);
  status = 503;
  await assert.rejects(client.choose({}, candidates), /Kev HTTP 503/);
});

test('CLI connects, records a model stop and disconnects using isolated fake servers', async t => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'mgba-local-test-'));
  const directory = path.join(root, 'mgba-willow', 'ai', 'sessions');
  await mkdir(directory, { recursive: true });
  const id = randomUUID(), gameId = randomUUID(), pipe = `mgba-willow-ai-${id}`, calls = [], sockets = new Set();
  const endpoint = process.platform === 'win32' ? `\\\\.\\pipe\\${pipe}` : path.join(os.tmpdir(), pipe);
  const bridge = net.createServer(socket => {
    sockets.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    let buffer = '';
    socket.on('data', chunk => {
      buffer += chunk;
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const req = JSON.parse(buffer.slice(0, end));
        buffer = buffer.slice(end + 1);
        calls.push(req.method);
        const result = req.method === 'set_objective' ? { objective: req.args.text } : {
          ...(req.method === 'session'
            ? { session_id: id, title: 'Synthetic Emerald', started: true, connected_clients: 0 } : snapshot()),
          game_id: gameId,
        };
        socket.write(JSON.stringify({ id: req.id, result }) + '\n');
      }
    });
  });
  let navigationRun = false, modelRequests = 0;
  const kev = http.createServer(async (req, res) => {
    let body = '';
    for await (const chunk of req) body += chunk;
    const criteria = JSON.parse(body).questions.action.criteria;
    if (navigationRun && modelRequests++ === 0) {
      res.end(JSON.stringify({ answers: { action: { type: 'choice', choice: 'move_5_4',
        probabilities: { stop: 0.30, move_5_4: 0.38, press_A: 0.32 } } } }));
      return;
    }
    res.end(JSON.stringify({ answers: { action: { type: 'choice', choice: 'stop',
      probabilities: Object.fromEntries(Object.keys(criteria).map(key => [key, key === 'stop' ? 1 : 0])) } } }));
  });
  await new Promise(resolve => bridge.listen(endpoint, resolve));
  await new Promise(resolve => kev.listen(0, '127.0.0.1', resolve));
  t.after(async () => {
    for (const socket of sockets) socket.destroy();
    kev.closeAllConnections();
    await Promise.all([new Promise(resolve => bridge.close(resolve)), new Promise(resolve => kev.close(resolve))]);
    await rm(root, { recursive: true, force: true });
  });
  await writeFile(path.join(directory, `${id}.json`), JSON.stringify({ session_id: id, pipe, protocol: 1 }));
  const logDir = path.join(root, 'logs');
  const childEnv = { ...process.env, LOCALAPPDATA: root, LUNA_HELP: '0' };
  const child = spawn(process.execPath, [fileURLToPath(new URL('../../local-model/cli.mjs', import.meta.url)),
    '--goal', 'Test stop', '--game-id', gameId, '--log-dir', logDir, '--url', `http://127.0.0.1:${kev.address().port}`],
  { env: childEnv });
  t.after(() => child.kill());
  let output = '';
  child.stdout.on('data', chunk => { output += chunk; });
  child.stderr.on('data', chunk => { output += chunk; });
  const code = await new Promise((resolve, reject) => { child.once('exit', resolve); child.once('error', reject); });
  assert.equal(code, 0, output);
  assert.match(output, /Stopped: model_stop/);
  assert.equal(calls.at(-1), 'disconnect');
  assert.ok(!calls.some(method => ['press', 'move_to', 'act', 'load_checkpoint'].includes(method)));
  const handoff = JSON.parse(await readFile(path.join(logDir, 'latest-handoff.json'), 'utf8'));
  assert.equal(handoff.reason, 'model_stop');
  assert.equal(handoff.handoff.available, true);
  assert.ok((await readdir(logDir)).some(name => name.endsWith('.jsonl')));

  // A goal from an earlier game must not even connect to take AI control.
  const callCount = calls.length;
  const stale = spawn(process.execPath, [fileURLToPath(new URL('../../local-model/cli.mjs', import.meta.url)),
    '--goal', 'Stale game goal', '--session', id, '--game-id', randomUUID(), '--log-dir', logDir,
    '--url', `http://127.0.0.1:${kev.address().port}`], { env: childEnv });
  t.after(() => stale.kill());
  let staleOutput = '';
  stale.stdout.on('data', chunk => { staleOutput += chunk; });
  stale.stderr.on('data', chunk => { staleOutput += chunk; });
  const staleCode = await new Promise((resolve, reject) => { stale.once('exit', resolve); stale.once('error', reject); });
  assert.equal(staleCode, 1, staleOutput);
  assert.match(staleOutput, /selected game changed/);
  assert.deepEqual(calls.slice(callCount), ['session']);

  await t.test('CLI can call configured Luna once and resume the local player', async () => {
    let assists = 0;
    const adviser = http.createServer(async (req, res) => {
      let text = '';
      for await (const chunk of req) text += chunk;
      const body = JSON.parse(text);
      assert.equal(body.model, 'test-luna-model');
      assists++;
      res.end(JSON.stringify({ choices: [{ finish_reason: 'stop', message: {
        content: JSON.stringify({ action_id: 'move_5_4', guidance: 'One safe tile, then inspect.' }),
      } }] }));
    });
    await new Promise(resolve => adviser.listen(0, '127.0.0.1', resolve));
    t.after(() => { adviser.closeAllConnections(); adviser.close(); });
    const directory = path.join(root, 'luna-logs');
    const runner = spawn(process.execPath, [fileURLToPath(new URL('../../local-model/cli.mjs', import.meta.url)),
      '--goal', 'Explore nearby tiles', '--session', id, '--game-id', gameId, '--log-dir', directory,
      '--url', `http://127.0.0.1:${kev.address().port}`, '--luna-help', '--max-assists', '1'],
    { env: { ...childEnv, LUNA_API_KEY: 'synthetic-key', LUNA_MODEL: 'test-luna-model',
      LUNA_BASE_URL: `http://127.0.0.1:${adviser.address().port}/v1` } });
    t.after(() => runner.kill());
    let text = '';
    runner.stdout.on('data', chunk => { text += chunk; });
    runner.stderr.on('data', chunk => { text += chunk; });
    const start = calls.length;
    const code = await new Promise((resolve, reject) => { runner.once('exit', resolve); runner.once('error', reject); });
    assert.equal(code, 0, text);
    assert.equal(assists, 1);
    assert.match(text, /Asking GPT-6 Luna for help: model_stop/);
    assert.match(text, /Luna: move_5_4/);
    assert.deepEqual(calls.slice(start).filter(method => ['move_to', 'act', 'press'].includes(method)), ['move_to']);
    assert.equal(calls.at(-1), 'disconnect');
    const saved = JSON.parse(await readFile(path.join(directory, 'latest-handoff.json'), 'utf8'));
    assert.equal(saved.details.assists, 1);
    assert.equal(saved.details.guidance, 'One safe tile, then inspect.');
  });

  for (const strict of [false, true]) {
    await t.test(`CLI ${strict ? 'explicit global override' : 'default navigation gate'} handles a 38 percent choice`, async () => {
      navigationRun = true;
      modelRequests = 0;
      const callStart = calls.length;
      const directory = path.join(root, strict ? 'strict-logs' : 'navigation-logs');
      const runner = spawn(process.execPath, [fileURLToPath(new URL('../../local-model/cli.mjs', import.meta.url)),
        '--goal', 'Explore nearby tiles', '--session', id, '--game-id', gameId, '--log-dir', directory,
        '--url', `http://127.0.0.1:${kev.address().port}`, ...(strict ? ['--min-probability', '0.6'] : [])],
      { env: childEnv });
      t.after(() => runner.kill());
      let text = '';
      runner.stdout.on('data', chunk => { text += chunk; });
      runner.stderr.on('data', chunk => { text += chunk; });
      const code = await new Promise((resolve, reject) => { runner.once('exit', resolve); runner.once('error', reject); });
      assert.equal(code, 0, text);
      const sent = calls.slice(callStart).filter(method => ['move_to', 'act', 'press'].includes(method));
      assert.deepEqual(sent, strict ? [] : ['move_to']);
      const saved = JSON.parse(await readFile(path.join(directory, 'latest-handoff.json'), 'utf8'));
      assert.equal(saved.reason, strict ? 'low_probability' : 'model_stop');
      assert.ok(saved.explanation);
      if (strict) {
        assert.match(text, /38\.0%.*60\.0% navigation threshold/);
        assert.equal(saved.details.min_probability, 0.6);
      } else {
        assert.match(text, /navigation minimum=0\.30, accepted/);
        assert.match(text, /does not establish goal completion/);
      }
    });
  }
});

test('local model endpoint refuses remote origins and URL credentials', () => {
  for (const baseUrl of ['https://example.com', 'http://192.168.1.2:8008', 'http://user:pass@localhost:8008', 'http://localhost:8008/v1']) {
    assert.throws(() => new KevClient({ baseUrl }), /local HTTP origin/);
  }
});
