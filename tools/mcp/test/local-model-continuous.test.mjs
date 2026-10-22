import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { play } from '../../local-model/player.mjs';
import { KevClient } from '../../local-model/kev-client.mjs';
import { LunaClient } from '../../local-model/luna-client.mjs';
import { RouteMemory } from '../../local-model/recovery.mjs';

function world(x = 5, y = 5) {
  return { frame: 1, connected_clients: 1, game_state: {
    supported: true, available: true, adapter: 'emerald_en_v1', frame: 1,
    interaction: 'overworld', movement_available: true, map: { id: 260 },
    player: { x, y }, menu: { kind: 'none', input_ready: false }, recent_events: [],
  }, local_map: { available: true, map_id: 260,
    tiles: [[x - 1, y], [x, y - 1], [x + 1, y], [x, y + 1]].map(([x, y]) => ({
      x, y, known: true, occupied: false, special_traversal: false, collision: 0,
    })) } };
}
function harness(initial = world()) {
  const calls = [], logs = [];
  let state = initial;
  const request = async (method, args) => {
    calls.push({ method, args });
    if (method === 'move_to') state = world(args.x, args.y);
    return structuredClone(state);
  };
  return { calls, logs, set state(value) { state = value; },
    options: { request, goal: 'Explore nearby', maxSteps: 1,
      model: { choose: async () => ({ id: 'stop', probability: 1 }) },
      log: async record => logs.push(record) } };
}
const inputs = h => h.calls.filter(call => ['move_to', 'press', 'act'].includes(call.method));
async function serverFor(t, handler) {
  const server = http.createServer(handler);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => { server.closeAllConnections(); server.close(); });
  return `http://127.0.0.1:${server.address().port}`;
}

test('1500 consecutive turns through the real Kev HTTP client accept the recorded 26.4% winner', async t => {
  let requests = 0;
  const url = await serverFor(t, async (req, res) => {
    let data = '';
    for await (const chunk of req) data += chunk;
    const body = JSON.parse(data), state = body.state;
    assert.ok(state.recent_actions.length <= 12);
    assert.ok(state.route_memory.length <= 4);
    assert.equal(state.game_state.quest_flags, undefined);
    assert.ok(data.length < 16000);
    const id = `move_${state.game_state.player.x + 1}_5`;
    const ids = Object.keys(body.questions.action.criteria);
    assert.equal(ids.length, 6);
    const probabilities = Object.fromEntries(ids.map(key => [key, key === id ? 0.26449083343392565 : (1 - 0.26449083343392565) / 5]));
    requests++;
    res.end(JSON.stringify({ answers: { action: { type: 'choice', choice: id, probabilities } } }));
  });
  const h = harness();
  const result = await play({ ...h.options, model: new KevClient({ baseUrl: url }), maxSteps: 1500 });
  assert.equal(result.reason, 'step_budget');
  assert.equal(result.steps, 1500);
  assert.equal(requests, 1500);
  assert.equal(inputs(h).length, 1500);
  assert.equal(inputs(h).at(-1).args.x, 1505);
  assert.ok(h.logs.filter(r => r.type === 'decision').every(r => r.policy.accepted && r.policy.min_probability === 1 / 6));
});

test('explicit strict thresholds still reject the same six-way choice', async () => {
  const h = harness();
  const probabilities = { stop: 0.1, move_4_5: 0.18, move_5_4: 0.18, move_6_5: 0.26, move_5_6: 0.18, press_A: 0.1 };
  const result = await play({ ...h.options, minProbability: 0.6,
    model: { choose: async () => ({ id: 'move_6_5', probability: 0.26, probabilities }) } });
  assert.equal(result.reason, 'low_probability');
  assert.equal(inputs(h).length, 0);
});

test('long battle animations wait deterministically, then return to Kev without a false 64-frame stall', async () => {
  const h = harness();
  let frames = 0, decisions = 0;
  const request = async (method, args) => {
    if (method === 'act') { assert.deepEqual(args.buttons, []); frames += args.frames; }
    const state = world();
    if (frames < 400) {
      state.game_state.interaction = 'battle';
      state.game_state.battle = { available: true };
    }
    return state;
  };
  const result = await play({ ...h.options, request, maxSteps: 100,
    model: { choose: async () => { decisions++; return { id: 'stop', probability: 1 }; } } });
  assert.equal(result.reason, 'model_stop');
  assert.equal(frames, 400);
  assert.equal(decisions, 1);
});

test('route loops can escape to an unseen live adjacent tile without a paid call', async () => {
  const h = harness();
  const model = { choose: async state => ({ id: `move_${state.game_state.player.x === 5 ? 6 : 5}_5`, probability: 0.9 }) };
  const result = await play({ ...h.options, model, maxSteps: 8 });
  assert.equal(result.reason, 'step_budget');
  assert.ok(h.logs.some(r => r.type === 'action' && r.source === 'route_recovery'));
  assert.ok(inputs(h).some(call => call.args.x === 4));
});

test('route memory stays bounded over thousands of unique tiles', () => {
  const memory = new RouteMemory();
  for (let x = 0; x < 5000; x++) memory.observe(world(x));
  assert.equal(memory.visits.size, 2048);
  assert.equal(memory.positions.length, 16);
});

test('stuck Kev asks Luna for one approved action, then resumes local inference', async () => {
  const h = harness();
  let assists = 0, local = 0;
  const luna = { choose: async (state, candidates) => {
    assists++;
    assert.equal(state.stuck_reason, 'model_stop');
    assert.ok(candidates.some(action => action.id === 'move_6_5'));
    return { id: 'move_6_5', guidance: 'Explore east, then inspect.' };
  } };
  const model = { choose: async state => {
    local++;
    if (local === 1) return { id: 'stop', probability: 0.9 };
    assert.equal(state.supervisor_guidance, 'Explore east, then inspect.');
    return { id: `move_${state.game_state.player.x + 1}_5`, probability: 0.9 };
  } };
  assert.equal((await play({ ...h.options, luna, model, maxSteps: 3 })).reason, 'step_budget');
  assert.equal(assists, 1);
  assert.equal(local, 3);
  assert.deepEqual(h.logs.filter(r => r.type === 'action').map(r => r.source), ['luna', 'kev', 'kev']);
});

test('Luna has bounded calls and cannot invent actions, retry confirmations or bypass unsupported prompts', async () => {
  for (const id of ['reset', 'press_A']) {
    const initial = world();
    initial.game_state.interaction = 'dialogue';
    const h = harness(initial);
    const result = await play({ ...h.options, luna: { choose: async () => ({ id }) } });
    assert.equal(result.reason, 'needs_human_inspection');
    assert.equal(inputs(h).length, 0);
    assert.ok(h.logs.some(r => r.type === 'assistance_error'));
  }
  const h = harness();
  const result = await play({ ...h.options, maxSteps: 10, maxAssists: 2,
    luna: { choose: async state => ({ id: `move_${state.game_state.player.x + 1}_5`, guidance: 'Inspect.' }) } });
  assert.equal(result.reason, 'model_stop');
  assert.equal(result.details.assists, 2);
  assert.equal(inputs(h).length, 2);

  const confirm = harness();
  const confirmed = await play({ ...confirm.options, maxSteps: 30,
    model: { choose: async () => ({ id: 'press_A', probability: 1 }) },
    luna: { choose: async () => ({ id: 'press_A', guidance: 'Retry.' }) } });
  assert.equal(confirmed.reason, 'unverified_confirmation');
  assert.equal(inputs(confirm).filter(c => c.method === 'press').length, 1);
});

test('healthy local turns and interrupted/unsupported games never call the remote adviser', async () => {
  const luna = { choose: async () => { assert.fail('Unexpected remote state sharing'); } };
  const healthy = harness();
  await play({ ...healthy.options, luna, maxSteps: 3,
    model: { choose: async state => ({ id: `move_${state.game_state.player.x + 1}_5`, probability: 0.9 }) } });
  for (const patch of [{ paused: true }, { human_keys: 1 }, { connected_clients: 2 }, { game_state: { supported: false } }]) {
    const h = harness({ ...world(), ...patch });
    await play({ ...h.options, luna });
    assert.equal(inputs(h).length, 0);
  }
});

test('failed Luna advice returns control with its diagnostic instead of sending a fallback input', async () => {
  const h = harness();
  const result = await play({ ...h.options, luna: { choose: async () => { throw new Error('Luna HTTP 429'); } } });
  assert.equal(result.reason, 'model_stop');
  assert.equal(result.details.assists, 1);
  assert.equal(inputs(h).length, 0);
  assert.ok(h.logs.some(r => r.type === 'assistance_error' && /429/.test(r.message)));
});

test('Luna actions are discarded after state changes, human interruption or cancellation', async () => {
  for (const change of ['stale', 'human', 'abort']) {
    const h = harness(), controller = new AbortController();
    const luna = { choose: async () => {
      if (change === 'abort') controller.abort();
      else if (change === 'human') h.state = { ...world(), human_keys: 1 };
      else h.state = world(8, 8);
      return { id: 'move_6_5', guidance: 'Go east.' };
    } };
    await play({ ...h.options, luna, signal: controller.signal });
    assert.equal(inputs(h).length, 0, change);
  }
});

test('transient inference errors retry without retrying controls; persistent errors can ask Luna', async () => {
  const h = harness();
  let attempts = 0;
  const model = { choose: async () => {
    if (++attempts < 3) throw Object.assign(new Error('busy'), { retryable: true });
    return { id: 'move_6_5', probability: 0.9 };
  } };
  await play({ ...h.options, model });
  assert.equal(attempts, 3);
  assert.equal(inputs(h).length, 1);
  assert.equal(h.logs.filter(r => r.type === 'model_retry').length, 2);

  const failed = harness();
  await play({ ...failed.options,
    model: { choose: async () => { throw new Error('offline'); } },
    luna: { choose: async state => {
      assert.equal(state.stuck_reason, 'model_unavailable');
      return { id: 'move_6_5' };
    } } });
  assert.equal(inputs(failed).length, 1);
});

test('Luna HTTP contract uses explicit provider model ID, validates JSON and never follows redirects', async t => {
  let received, mode = 'ok';
  const url = await serverFor(t, async (req, res) => {
    let text = '';
    for await (const chunk of req) text += chunk;
    received = { url: req.url, auth: req.headers.authorization, body: JSON.parse(text) };
    if (mode === 'redirect') { res.writeHead(302, { Location: 'https://example.invalid' }); res.end(); return; }
    if (mode === 'error') { res.writeHead(401); res.end('secret provider diagnostics'); return; }
    res.end(JSON.stringify({ choices: [{ finish_reason: mode === 'truncated' ? 'length' : 'stop', message: {
      content: JSON.stringify({ action_id: mode === 'bad' ? 'shell_command' : 'stop', guidance: 'Needs human help.' }),
    } }] }));
  });
  const client = new LunaClient({ baseUrl: `${url}/v1`, apiKey: 'test-key', model: 'provider-gpt6-luna-id' });
  const candidates = [{ id: 'stop', description: 'Hand back control.' }];
  assert.equal((await client.choose({ goal: 'Test' }, candidates)).id, 'stop');
  assert.equal(received.url, '/v1/chat/completions');
  assert.equal(received.auth, 'Bearer test-key');
  assert.equal(received.body.model, 'provider-gpt6-luna-id');
  assert.equal(received.body.max_completion_tokens, 1024);
  for (mode of ['bad', 'truncated', 'redirect', 'error']) {
    await assert.rejects(client.choose({}, candidates));
  }
  assert.throws(() => new LunaClient({ apiKey: 'x' }), /LUNA_MODEL/);
  for (const baseUrl of ['http://example.com/v1', 'https://user:pass@example.com', 'https://example.com/?key=secret']) {
    assert.throws(() => new LunaClient({ baseUrl, apiKey: 'x', model: 'luna' }), /LUNA_BASE_URL/);
  }
});
