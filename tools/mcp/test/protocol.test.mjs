import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { Bridge, listSessions } from '../bridge.mjs';

async function fixture(t, handler) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'mgba-mcp-test-'));
  const directory = path.join(root, 'mgba-willow', 'ai', 'sessions');
  await mkdir(directory, { recursive: true });
  const id = randomUUID();
  const descriptor = { session_id: id, pipe: `mgba-willow-ai-${id}`, protocol: 1 };
  const endpoint = process.platform === 'win32' ? `\\\\.\\pipe\\${descriptor.pipe}` : path.join(os.tmpdir(), descriptor.pipe);
  const sockets = new Set();
  const server = net.createServer(socket => {
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
        handler(socket, req, id);
      }
    });
  });
  await new Promise(resolve => server.listen(endpoint, resolve));
  await writeFile(path.join(directory, id + '.json'), JSON.stringify(descriptor));
  t.after(async () => {
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => server.close(resolve));
    await rm(root, { recursive: true, force: true });
  });
  return { root, directory, id, descriptor };
}

test('bridge correlates fragmented responses and propagates errors', async t => {
  const f = await fixture(t, (socket, req) => {
    const message = JSON.stringify(req.method === 'fail' ? { id: req.id, error: 'Rejected' } : { id: req.id, result: { method: req.method } }) + '\n';
    socket.write(message.slice(0, 5));
    setImmediate(() => socket.write(message.slice(5)));
  });
  const bridge = await Bridge.open(f.descriptor);
  t.after(() => bridge.close());
  assert.deepEqual(await bridge.request('observe'), { method: 'observe' });
  await assert.rejects(bridge.request('fail'), /Rejected/);
  assert.deepEqual(await bridge.request('observe'), { method: 'observe' });
});

test('timeouts close the connection and reject pending calls', async t => {
  const f = await fixture(t, () => {});
  const bridge = await Bridge.open(f.descriptor);
  bridge.timeout = 30;
  await assert.rejects(bridge.request('act'), /timed out/);
  assert.equal(bridge.socket.destroyed, true);
});

test('malformed responses reject requests rather than crashing', async t => {
  const f = await fixture(t, socket => socket.write('invalid-json\n'));
  const bridge = await Bridge.open(f.descriptor);
  await assert.rejects(bridge.request('observe'), /Malformed/);
});

test('session discovery ignores stale and invalid descriptors', async t => {
  const f = await fixture(t, (socket, req, id) => socket.write(JSON.stringify({ id: req.id, result: { session_id: id, started: true } }) + '\n'));
  const staleId = randomUUID();
  await writeFile(path.join(f.directory, staleId + '.json'), JSON.stringify({ session_id: staleId, pipe: `mgba-willow-ai-${staleId}`, protocol: 1 }));
  await writeFile(path.join(f.directory, randomUUID() + '.json'), 'bad');
  assert.deepEqual(await listSessions(f.directory), [{ session_id: f.id, started: true }]);
});

test('MCP handshake, tools, schema validation, images, and reconnect', async t => {
  let frame = 10;
  let speed = 4;
  const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';
  const calls = [];
  const f = await fixture(t, (socket, req, id) => {
    calls.push(req);
    const start = frame;
    let steps;
    if (req.method === 'act') frame += req.args.frames;
    if (req.method === 'act_sequence') steps = req.args.actions.map(action => {
      const begin = frame; frame += action.frames;
      return { action_start_frame: begin, action_end_frame: frame };
    });
    if (req.method === 'set_speed') speed = req.args.multiplier;
    const result = req.method === 'session' ? { session_id: id, started: true, title: 'Test', mcp_enabled: true, ai_speed: speed }
      : req.method === 'disconnect' ? { paused: false }
      : req.method === 'read_memory' ? { hex: '0001' }
      : { ...(req.args.screenshot === false ? {} : { png }), frame, paused: false, ai_speed: speed,
        human_keys: 0, ai_keys: 0, effective_keys: 0,
        ...(['act', 'act_sequence'].includes(req.method) ? { action_start_frame: start, action_end_frame: frame, cancelled: false } : {}),
        ...(steps ? { steps } : {}) };
    socket.write(JSON.stringify({ id: req.id, result }) + '\n');
  });
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [path.resolve('server.mjs')],
    env: { ...process.env, LOCALAPPDATA: f.root },
    stderr: 'pipe',
  });
  let errors = '';
  transport.stderr?.on('data', chunk => { errors += chunk; });
  const client = new Client({ name: 'mgba-tests', version: '1.0' });
  t.after(() => client.close());
  await client.connect(transport);
  const { tools } = await client.listTools();
  assert.deepEqual(tools.map(tool => tool.name).sort(), ['act', 'act_sequence', 'set_speed', 'connect', 'disconnect', 'list_sessions', 'load_checkpoint', 'memory_map', 'observe', 'read_memory', 'save_checkpoint', 'get_game_state', 'get_handoff', 'get_local_map', 'move_to', 'press', 'wait_until'].sort());
  const call = (name, args = {}) => client.callTool({ name, arguments: args });
  assert.equal((await call('observe')).isError, true);
  assert.equal((await call('list_sessions')).structuredContent.sessions[0].session_id, f.id);
  const connected = await call('connect', { session_id: f.id });
  const handoff = await call('get_handoff', { screenshot: false });
  assert.equal(handoff.structuredContent.handoff.available, false);
  assert.equal(calls.at(-1).method, 'get_game_state');
  assert.equal(tools.find(tool => tool.name === 'get_handoff').annotations.readOnlyHint, true);
  assert.equal(connected.content[1].type, 'image');
  assert.equal(connected.content[1].mimeType, 'image/png');
  assert.equal(connected.structuredContent.paused, false);
  assert.equal(connected.structuredContent.png, undefined);
  const result = await call('act', { buttons: ['A', 'Up'], frames: 3 });
  assert.equal(result.structuredContent.frame, 13);
  for (const args of [{ buttons: ['X'], frames: 1 }, { buttons: [], frames: 0 }, { buttons: [], frames: 601 }, { buttons: [], frames: 1.5 }]) {
    assert.equal((await call('act', args)).isError, true);
  }
  assert.equal(calls.filter(call => call.method === 'act').length, 1);
  const noImage = await call('act', { buttons: [], frames: 1, screenshot: false });
  assert.equal(noImage.content.length, 1);
  assert.equal(noImage.structuredContent.action_end_frame - noImage.structuredContent.action_start_frame, 1);
  const sequenceCalls = calls.length;
  const sequence = await call('act_sequence', { actions: [{ buttons: ['Right'], frames: 5 }, { buttons: ['A'], frames: 2 }, { buttons: [], frames: 1 }] });
  assert.equal(calls.length, sequenceCalls + 1, 'Batch should make exactly one bridge call');
  assert.equal(sequence.content[1].type, 'image');
  assert.deepEqual(sequence.structuredContent.steps, [
    { action_start_frame: 14, action_end_frame: 19 },
    { action_start_frame: 19, action_end_frame: 21 },
    { action_start_frame: 21, action_end_frame: 22 },
  ]);
  assert.equal((await call('act_sequence', { actions: [{ buttons: [], frames: 600 }], screenshot: false })).content.length, 1);
  const longStart = frame;
  const longCalls = calls.length;
  const longSequence = await call('act_sequence', {
    actions: Array.from({ length: 200 }, (_, i) => ({ buttons: [i % 2 ? 'Left' : 'Right'], frames: 3 })),
  });
  assert.equal(longSequence.isError, undefined);
  assert.equal(calls.length, longCalls + 1, '200 moves should make exactly one bridge call');
  assert.equal(longSequence.content[1].type, 'image');
  assert.equal(longSequence.structuredContent.steps.length, 200);
  assert.equal(longSequence.structuredContent.action_end_frame - longStart, 600);
  assert.deepEqual(longSequence.structuredContent.steps[199], {
    action_start_frame: longStart + 597, action_end_frame: longStart + 600,
  });
  const count = calls.length;
  for (const args of [
    { actions: [] },
    { actions: Array.from({ length: 201 }, () => ({ buttons: [], frames: 1 })) },
    { actions: [{ buttons: [], frames: 600 }, { buttons: [], frames: 1 }] },
    { actions: [{ buttons: ['X'], frames: 1 }] },
    { actions: [{ buttons: [], frames: 0 }] },
    { actions: [{ buttons: [], frames: 1 }], screenshot: 'false' },
  ]) assert.equal((await call('act_sequence', args)).isError, true);
  assert.equal(calls.length, count, 'Invalid sequences must never reach the bridge');
  for (const multiplier of [1, 2, 4, 8, 'maximum']) {
    assert.equal((await call('set_speed', { multiplier })).structuredContent.ai_speed, multiplier);
  }
  const speedCalls = calls.length;
  for (const multiplier of [0, 3, 16, -1, '4', 'unbounded']) {
    assert.equal((await call('set_speed', { multiplier })).isError, true);
  }
  assert.equal(calls.length, speedCalls);
  assert.equal((await call('list_sessions')).structuredContent.sessions[0].ai_speed, 'maximum');
  assert.equal((await call('read_memory', { address: -1, length: 1 })).isError, true);
  assert.equal((await call('read_memory', { address: 0, length: 4097 })).isError, true);
  assert.equal((await call('save_checkpoint', { name: '../escape' })).isError, true);
  assert.equal((await call('disconnect')).structuredContent.paused, false);
  assert.equal((await call('observe')).isError, true);
  assert.equal((await call('connect', { session_id: f.id })).isError, undefined);
  for (const name of ['get_game_state', 'get_local_map', 'press', 'move_to', 'wait_until']) {
    assert.equal(tools.find(tool => tool.name === name).annotations.readOnlyHint, ['get_game_state', 'get_local_map'].includes(name));
  }
  for (const [name, args] of [
    ['get_local_map', { radius: 17 }], ['move_to', { x: -1, y: 0 }], ['move_to', { x: 0, y: 0, max_frames: 601 }],
    ['wait_until', { condition: 'guessed' }], ['wait_until', { condition: 'dialogue', stable_frames: 0 }],
    ['press', { button: 'X' }], ['press', { button: 'A', release_frames: 0 }],
    ['get_game_state', { screenshot: 'true' }],
  ]) {
    const before = calls.length;
    assert.equal((await call(name, args)).isError, true);
    assert.equal(calls.length, before, 'Invalid state/action input must not reach the bridge');
  }
  for (const [name, args] of [
    ['get_game_state', { full_state: true, screenshot: false }], ['get_local_map', { radius: 4, screenshot: false }],
    ['press', { button: 'A', hold_frames: 1, release_frames: 2, screenshot: false }],
    ['move_to', { x: 3, y: 4, max_frames: 60, screenshot: false }],
    ['wait_until', { condition: 'battle_menu_ready', max_frames: 60, stable_frames: 2, screenshot: false }],
    ['wait_until', { condition: 'battle_move_ready', max_frames: 60, screenshot: false }],
    ['wait_until', { condition: 'battle_target_ready', max_frames: 60, screenshot: false }],
  ]) {
    assert.equal((await call(name, args)).isError, undefined);
    assert.deepEqual(calls.at(-1).args, args);
    assert.equal(calls.at(-1).method, name);
  }
  await call('disconnect');
  assert.equal(errors, '');
});

test('verified Emerald MCP rejects batch confirmation and refreshes before a tap', async t => {
  const calls = [];
  const game_state = { supported: true, available: true, adapter: 'emerald_en_v1', frame: 10,
    interaction: 'battle', menu: { kind: 'battle_action', cursor: 0 }, battle: { battlers: [{ hp: 7 }] } };
  const f = await fixture(t, (socket, req, id) => {
    calls.push(req);
    socket.write(JSON.stringify({ id: req.id, result: req.method === 'session'
      ? { session_id: id, started: true } : { game_state } }) + '\n');
  });
  const transport = new StdioClientTransport({ command: process.execPath,
    args: [path.resolve('server.mjs')], env: { ...process.env, LOCALAPPDATA: f.root }, stderr: 'pipe' });
  const client = new Client({ name: 'verified-control-tests', version: '1.0' });
  t.after(() => client.close());
  await client.connect(transport);
  await client.callTool({ name: 'connect', arguments: { session_id: f.id } });
  const rejected = await client.callTool({ name: 'act_sequence', arguments: {
    actions: [{ buttons: ['A'], frames: 1 }, { buttons: ['A'], frames: 1 }],
  } });
  assert.equal(rejected.isError, true);
  assert.equal(calls.at(-1).method, 'get_game_state');
  assert.ok(!calls.some(call => ['act', 'act_sequence', 'press'].includes(call.method)));
  const tapped = await client.callTool({ name: 'press', arguments: { button: 'A', screenshot: false } });
  assert.equal(calls.at(-2).method, 'get_game_state');
  assert.equal(calls.at(-1).method, 'press');
  assert.equal(tapped.structuredContent.state_format, 'full');
  assert.equal(tapped.structuredContent.game_state.battle.battlers[0].hp, 7);
});
